#include "config_server.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>

#include <atomic>

#include <Arduino.h>
#include <ArduinoJson.h>
#include <FS.h>
#include <LittleFS.h>
#include <Network.h>
#include <Preferences.h>
#include <WiFi.h>
#include <esp_heap_caps.h>
#include <esp_random.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <lwip/sockets.h>

#include "chords.h"
#include "config_files.h"
#include "config_http.h"
#include "config_job.h"
#include "console_log.h"
#include "pin_map.h"

#undef write
#undef read
#undef send
#undef recv

#if __has_include("secrets.h")
#include "secrets.h"
#endif

#ifndef WIFI_AP_SSID
#define WIFI_AP_SSID "Kanade"
#endif
#ifndef WIFI_AP_PASSWORD
#define WIFI_AP_PASSWORD "replace-with-8-or-more-chars"
#endif
#ifndef WIFI_API_TOKEN
#define WIFI_API_TOKEN ""
#endif

static const char kApSsid[] = WIFI_AP_SSID;
static const char kApPassword[] = WIFI_AP_PASSWORD;
static const char kApiToken[] = WIFI_API_TOKEN;
// Same NVS namespace as the chord fire mode. The key is separate.
static constexpr char kApiTokenNs[] = "jis";
static constexpr char kApiTokenKey[] = "apitoken";
static constexpr size_t kApiTokenRandomBytes = 16;
// Same cap as HttpRequest::token, so a handwritten token is not truncated.
static char g_api_token[128];
static constexpr char kApPlaceholder[] = "replace-with-8-or-more-chars";

static constexpr char kJsonType[] = "application/json; charset=utf-8";
static constexpr char kFallbackJson[] = "{\"ok\":false,\"error\":\"失敗\"}";

static bool g_http = false;
static bool g_switch_raw = false;
static bool g_switch_stable = false;
static uint32_t g_switch_change_ms = 0;
static bool g_start_failed = false;

static WiFiServer g_server(80);
static WiFiClient g_client;

enum class Conn : uint8_t { Idle, Header, Body, Drain, Wait, Send };

static Conn g_conn = Conn::Idle;
static uint32_t g_io_ms = 0;
static char g_header[kHttpHeaderMax];
static size_t g_header_len = 0;
static char *g_body = nullptr;
static size_t g_body_len = 0;
static size_t g_body_got = 0;
static bool g_cors = false;

enum class Route : uint8_t { None, Page, Chords, Reset, Fire };
static Route g_route = Route::None;

enum class After : uint8_t { Error, Status, Reset };
static After g_after = After::Error;
static int g_after_code = 400;
static const char *g_after_msg = "失敗";
static size_t g_drain_left = 0;

struct Outgoing {
  char hdr[512];
  size_t hdr_len;
  size_t hdr_sent;
  const char *body;
  size_t body_len;
  size_t body_sent;
  char *owned;
};

static Outgoing g_out = {};

static std::atomic<uint8_t> g_job_phase{static_cast<uint8_t>(ConfigJobPhase::Idle)};
static ConfigJobKind g_job_kind = ConfigJobKind::None;
static char *g_job_body = nullptr;
static size_t g_job_len = 0;
static int g_job_code = 500;
static char *g_job_resp = nullptr;
static bool g_job_main_ok = false;
static unsigned g_job_count = 0;
static TaskHandle_t g_worker = nullptr;
static bool g_drop_result = false;

static const char kConfigPage[] PROGMEM =
#include "config_page.inc"
    ;

static char *alloc_chars(size_t n) {
  void *pointer = ps_malloc(n);
  if (pointer == nullptr) pointer = malloc(n);
  return static_cast<char *>(pointer);
}

static bool api_path(const char *path) { return strncmp(path, "/api/", 5) == 0; }

static bool known_path(const char *path) {
  return strcmp(path, "/") == 0 || strcmp(path, "/api/chords") == 0 ||
         strcmp(path, "/api/chords/reset") == 0 || strcmp(path, "/api/fire") == 0;
}

static Route route_of(const char *path) {
  if (strcmp(path, "/") == 0) return Route::Page;
  if (strcmp(path, "/api/chords") == 0) return Route::Chords;
  if (strcmp(path, "/api/chords/reset") == 0) return Route::Reset;
  if (strcmp(path, "/api/fire") == 0) return Route::Fire;
  return Route::None;
}

static const char *status_text(int code) {
  switch (code) {
    case 200: return "OK";
    case 204: return "No Content";
    case 400: return "Bad Request";
    case 401: return "Unauthorized";
    case 403: return "Forbidden";
    case 404: return "Not Found";
    case 413: return "Payload Too Large";
    case 431: return "Request Header Fields Too Large";
    case 500: return "Internal Server Error";
    case 503: return "Service Unavailable";
    default: return "Error";
  }
}

static char *json_error(const char *msg) {
  JsonDocument doc;
  doc["ok"] = false;
  doc["error"] = msg != nullptr ? msg : "失敗";
  const size_t need = measureJson(doc) + 1;
  char *buf = alloc_chars(need);
  if (buf == nullptr) return nullptr;
  const size_t wrote = serializeJson(doc, buf, need);
  if (wrote == 0 || wrote >= need) {
    chords_release(buf);
    return nullptr;
  }
  return buf;
}

static void release_out() {
  chords_release(g_out.owned);
  g_out.owned = nullptr;
  g_out.body = nullptr;
  g_out.body_len = 0;
}

static void begin_send(int code, const char *type, const char *body, size_t len, char *owned, bool cors) {
  release_out();
  g_out.body = body != nullptr ? body : "";
  g_out.body_len = len;
  g_out.body_sent = 0;
  g_out.owned = owned;
  g_out.hdr_sent = 0;
  int wrote = snprintf(g_out.hdr, sizeof(g_out.hdr),
                       "HTTP/1.1 %d %s\r\nContent-Type: %s\r\nContent-Length: %u\r\n"
                       "Cache-Control: no-store\r\nConnection: close\r\n",
                       code, status_text(code), type, static_cast<unsigned>(len));
  if (cors && wrote > 0 && static_cast<size_t>(wrote) < sizeof(g_out.hdr)) {
    const int extra = snprintf(
        g_out.hdr + wrote, sizeof(g_out.hdr) - static_cast<size_t>(wrote),
        "Access-Control-Allow-Origin: %s\r\nAccess-Control-Allow-Methods: GET, POST, OPTIONS\r\n"
        "Access-Control-Allow-Headers: Content-Type, X-Api-Token\r\nVary: Origin\r\n",
        kConfigOrigin);
    if (extra > 0) wrote += extra;
  }
  if (wrote > 0 && static_cast<size_t>(wrote) + 2 < sizeof(g_out.hdr)) {
    memcpy(g_out.hdr + wrote, "\r\n", 2);
    wrote += 2;
    g_out.hdr_len = static_cast<size_t>(wrote);
  } else {
    g_out.hdr_len = 0;
  }
  g_conn = Conn::Send;
  g_io_ms = millis();
}

static void begin_error(int code, const char *msg, bool cors) {
  char *body = json_error(msg);
  if (body == nullptr) {
    begin_send(code, kJsonType, kFallbackJson, sizeof(kFallbackJson) - 1, nullptr, cors);
    return;
  }
  begin_send(code, kJsonType, body, strlen(body), body, cors);
}

static int nb_send(int fd, const void *data, size_t len) {
  if (fd < 0) return -1;
  if (len > kHttpSlice) len = kHttpSlice;
  const int n = lwip_send(fd, data, len, MSG_DONTWAIT);
  if (n < 0) {
    if (errno == EAGAIN || errno == EWOULDBLOCK) return 0;
    return -1;
  }
  return n;
}

static bool peer_closed(int fd) {
  if (fd < 0) return true;
  char dummy;
  const int n = lwip_recv(fd, &dummy, 1, MSG_DONTWAIT | MSG_PEEK);
  if (n == 0) return true;
  if (n < 0 && errno != EAGAIN && errno != EWOULDBLOCK) return true;
  return false;
}

static int nb_read(uint8_t *dst, size_t cap) {
  if (cap > kHttpSlice) cap = kHttpSlice;
  const int avail = g_client.available();
  if (avail <= 0) return 0;
  size_t n = static_cast<size_t>(avail);
  if (n > cap) n = cap;
  return g_client.read(dst, n);
}

static void drop_conn() {
  release_out();
  chords_release(g_body);
  g_body = nullptr;
  g_client.stop();
  g_conn = Conn::Idle;
}

static void complete_drain();

static void arm_drain(After after, int code, const char *msg, bool cors, size_t already, long content_length) {
  g_after = after;
  g_after_code = code;
  g_after_msg = msg != nullptr ? msg : "失敗";
  g_cors = cors;
  size_t left = 0;
  if (content_length > 0 && content_length <= kDrainMax &&
      static_cast<size_t>(content_length) > already) {
    left = static_cast<size_t>(content_length) - already;
  }
  if (left == 0) {
    g_drain_left = 0;
    complete_drain();
    return;
  }
  g_drain_left = left;
  g_conn = Conn::Drain;
  g_io_ms = millis();
}

static bool remove_present(const char *path) {
  if (!LittleFS.exists(path)) return true;
  return LittleFS.remove(path);
}

static bool lfs_exists(void *, const char *path) { return LittleFS.exists(path); }

static bool lfs_remove(void *, const char *path) { return LittleFS.remove(path); }

static bool lfs_rename(void *, const char *from, const char *to) { return LittleFS.rename(from, to); }

// flush() is fflush plus fsync. close() alone can leave the new bytes in a
// buffer, and the next step renames the previous chords.json away.
static bool lfs_write(void *, const char *path, const uint8_t *data, size_t len) {
  File file = LittleFS.open(path, "w");
  if (!file) return false;
  const size_t wrote = file.write(data, len);
  file.flush();
  file.close();
  return wrote == len;
}

static ChordFileOps lfs_ops() {
  return ChordFileOps{nullptr, lfs_exists, lfs_remove, lfs_rename, lfs_write};
}

static bool write_chords_file(const uint8_t *data, size_t len) {
  const ChordFileOps ops = lfs_ops();
  return chord_files_replace(&ops, data, len, kChordsPath, kChordsTmpPath, kChordsBakPath);
}

static bool restore_previous_chords() {
  const ChordFileOps ops = lfs_ops();
  return chord_files_restore(&ops, kChordsPath, kChordsBakPath);
}

// kind and main_ok are published before the phase store, and read only after
// an acquire load of the phase. See config_job.h.
static ConfigJobView load_job() {
  ConfigJobView view;
  view.phase = static_cast<ConfigJobPhase>(g_job_phase.load(std::memory_order_acquire));
  view.kind = g_job_kind;
  view.main_ok = g_job_main_ok;
  return view;
}

static void publish_job(const ConfigJobView &view) {
  g_job_kind = view.kind;
  g_job_main_ok = view.main_ok;
  g_job_phase.store(static_cast<uint8_t>(view.phase), std::memory_order_release);
}

static void mark_done() {
  ConfigJobView view = load_job();
  if (!config_job_finish(&view)) return;
  publish_job(view);
}

static void mark_need_main() {
  ConfigJobView view = load_job();
  if (!config_job_need_main(&view)) return;
  publish_job(view);
}

static void remember_error(int code, const char *msg) {
  g_job_code = code;
  chords_release(g_job_resp);
  g_job_resp = json_error(msg);
  mark_done();
}

static void run_queued() {
  if (g_job_kind == ConfigJobKind::Get) {
    char *json = chords_format_json();
    if (json == nullptr) {
      remember_error(500, "記憶體不足");
      return;
    }
    g_job_resp = json;
    g_job_code = 200;
    mark_done();
    return;
  }

  if (g_job_kind == ConfigJobKind::Reset) {
    if (!chords_fs_ready() || !remove_present(kChordsBakPath) || !remove_present(kChordsTmpPath) ||
        !remove_present(kChordsPath)) {
      remember_error(500, chords_fs_ready() ? "存檔失敗" : "檔案系統還沒備好");
      return;
    }
    mark_need_main();
    return;
  }
  remember_error(500, "失敗");
}

static void run_post() {
  if (!chords_fs_ready()) {
    chords_release(g_job_body);
    g_job_body = nullptr;
    remember_error(500, "檔案系統還沒備好");
    return;
  }
  char err[160];
  err[0] = '\0';
  const ChordStageStatus staged = chords_stage_json(g_job_body, g_job_len, err, sizeof(err));
  if (staged != ChordStageStatus::Ok) {
    chords_drop_staged();
    chords_release(g_job_body);
    g_job_body = nullptr;
    remember_error(staged == ChordStageStatus::NoMemory ? 500 : 400,
                   err[0] != '\0' ? err : "JSON 格式不對");
    return;
  }
  const bool wrote =
      write_chords_file(reinterpret_cast<const uint8_t *>(g_job_body), g_job_len);
  chords_release(g_job_body);
  g_job_body = nullptr;
  if (!wrote) {
    chords_drop_staged();
    remember_error(500, "存檔失敗");
    return;
  }
  mark_need_main();
}

static void run_resume() {
  if (g_job_kind == ConfigJobKind::Post) {
    if (!g_job_main_ok) {
      restore_previous_chords();
      remember_error(500, "存檔失敗");
      return;
    }
    remove_present(kChordsBakPath);
    char *buf = alloc_chars(80);
    if (buf == nullptr) {
      remember_error(500, "記憶體不足");
      return;
    }
    snprintf(buf, 80, "{\"ok\":true,\"source\":\"saved\",\"count\":%u}", g_job_count);
    g_job_resp = buf;
    g_job_code = 200;
    mark_done();
    return;
  }
  if (!g_job_main_ok) {
    remember_error(500, "記憶體不足");
    return;
  }
  char *json = chords_format_json();
  if (json == nullptr) {
    remember_error(500, "記憶體不足");
    return;
  }
  g_job_resp = json;
  g_job_code = 200;
  mark_done();
}

static void worker_fn(void *) {
  for (;;) {
    ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
    switch (config_job_worker_action(load_job())) {
      case ConfigJobAction::RunPost:
        run_post();
        break;
      case ConfigJobAction::RunGet:
      case ConfigJobAction::RunReset:
        run_queued();
        break;
      case ConfigJobAction::RunResume:
        run_resume();
        break;
      case ConfigJobAction::None:
        break;
    }
  }
}

static void apply_main(bool wake) {
  ConfigJobView view = load_job();
  bool ok = false;
  if (view.kind == ConfigJobKind::Post) {
    ok = chords_commit_staged();
    if (!ok) chords_drop_staged();
    else g_job_count = static_cast<unsigned>(chords_count());
  } else {
    ok = chords_load_factory();
  }
  if (!config_job_applied(&view, ok)) return;
  publish_job(view);
  if (wake && g_worker != nullptr) xTaskNotifyGive(g_worker);
}

static ConfigJobQueue queue_job(ConfigJobKind kind, char *body, size_t len) {
  ConfigJobView view = load_job();
  const ConfigJobQueue result = config_job_queue(&view, kind, g_worker != nullptr);
  if (result != ConfigJobQueue::Ok) {
    chords_release(body);
    return result;
  }
  g_job_body = body;
  g_job_len = len;
  g_job_resp = nullptr;
  g_job_code = 500;
  publish_job(view);
  g_conn = Conn::Wait;
  g_io_ms = millis();
  xTaskNotifyGive(g_worker);
  return ConfigJobQueue::Ok;
}

static void queue_or_error(ConfigJobKind kind, char *body, size_t len) {
  switch (queue_job(kind, body, len)) {
    case ConfigJobQueue::Ok:
      return;
    case ConfigJobQueue::NoWorker:
      begin_error(503, "設定服務還沒備好", g_cors);
      return;
    case ConfigJobQueue::Busy:
      begin_error(500, "失敗", g_cors);
      return;
  }
}

static void service_job() {
  const ConfigJobPhase phase = load_job().phase;
  if (phase == ConfigJobPhase::NeedMain) {
    apply_main(true);
    return;
  }
  if (phase != ConfigJobPhase::Done) return;
  char *resp = g_job_resp;
  g_job_resp = nullptr;
  const int code = g_job_code;
  ConfigJobView view = load_job();
  if (config_job_release(&view)) publish_job(view);
  if (g_drop_result || g_conn != Conn::Wait) {
    chords_release(resp);
    g_drop_result = false;
    if (g_conn == Conn::Wait) {
      g_client.stop();
      g_conn = Conn::Idle;
    }
    return;
  }
  if (resp == nullptr) {
    begin_error(code != 0 ? code : 500, "失敗", g_cors);
    return;
  }
  begin_send(code, kJsonType, resp, strlen(resp), resp, g_cors);
}

static void handle_fire(char *body, size_t len) {
  JsonDocument doc;
  const DeserializationError error = deserializeJson(doc, body, len);
  const char *mode = nullptr;
  if (!error && doc["mode"].is<const char *>()) mode = doc["mode"].as<const char *>();
  // Copy the mode off the document before releasing the body. The document
  // borrows nothing from body when the input is a bounded buffer, but the
  // pointer is only used after this check returns.
  char mode_buf[8];
  mode_buf[0] = '\0';
  if (mode != nullptr) {
    strncpy(mode_buf, mode, sizeof(mode_buf) - 1);
    mode_buf[sizeof(mode_buf) - 1] = '\0';
  }
  chords_release(body);
  if (mode_buf[0] == '\0' || !chords_set_fire_mode_name(mode_buf, false)) {
    begin_error(400, "模式只能是 release 或 first", g_cors);
    return;
  }
  char *ok = alloc_chars(48);
  if (ok == nullptr) {
    begin_error(500, "記憶體不足", g_cors);
    return;
  }
  snprintf(ok, 48, "{\"ok\":true,\"mode\":\"%s\"}", chords_fire_mode_name());
  begin_send(200, kJsonType, ok, strlen(ok), ok, g_cors);
}

static void finish_body() {
  char *body = g_body;
  const size_t len = g_body_len;
  g_body = nullptr;
  if (body != nullptr && len <= g_body_len) body[len] = '\0';
  if (g_route == Route::Fire) {
    handle_fire(body, len);
    return;
  }
  if (g_route == Route::Chords) {
    queue_or_error(ConfigJobKind::Post, body, len);
    return;
  }
  chords_release(body);
  begin_error(404, "not found", g_cors);
}

static void complete_drain() {
  if (g_after == After::Status) {
    begin_send(g_after_code, "text/plain; charset=utf-8", "", 0, nullptr, g_cors);
    return;
  }
  if (g_after == After::Reset) {
    queue_or_error(ConfigJobKind::Reset, nullptr, 0);
    return;
  }
  begin_error(g_after_code, g_after_msg, g_cors);
}

static bool authorized(const HttpRequest &req) {
  if (req.token_overflow) return false;
  // token_matches treats an empty expected token as allow-all. An unresolved
  // token must deny /api/* instead.
  if (g_api_token[0] == '\0') return false;
  if (!req.has_token) return false;
  return token_matches(g_api_token, req.token);
}

static void clear_api_token() { g_api_token[0] = '\0'; }

static bool copy_runtime_token(const char *src) {
  if (src == nullptr) return false;
  const size_t n = strlen(src);
  if (n == 0 || n >= sizeof(g_api_token)) return false;
  memcpy(g_api_token, src, n + 1);
  return true;
}

static void token_store_failed() {
  clear_api_token();
  console_println("API token was not stored");
}

static void load_api_token() {
  clear_api_token();
  if (api_token_action(kApiToken, nullptr) == ApiTokenAction::UseCompile) {
    if (!copy_runtime_token(kApiToken)) console_println("API token unavailable");
    return;
  }

  char stored[sizeof(g_api_token)];
  stored[0] = '\0';
  Preferences prefs;
  // Read-only open fails when the namespace does not exist yet. That is a
  // first boot, not a broken store: fall through and create the key.
  if (prefs.begin(kApiTokenNs, true)) {
    prefs.getString(kApiTokenKey, stored, sizeof(stored));
    prefs.end();
  }

  if (api_token_action(kApiToken, stored) == ApiTokenAction::UseStored) {
    if (!copy_runtime_token(stored)) console_println("API token unavailable");
    return;
  }

  uint8_t raw[kApiTokenRandomBytes];
  esp_fill_random(raw, sizeof(raw));
  const bool formatted = format_token_hex(raw, sizeof(raw), g_api_token, sizeof(g_api_token));
  memset(raw, 0, sizeof(raw));
  if (!formatted) {
    token_store_failed();
    return;
  }

  if (!prefs.begin(kApiTokenNs, false)) {
    token_store_failed();
    return;
  }
  const size_t wrote = prefs.putString(kApiTokenKey, g_api_token);
  prefs.end();
  if (wrote != strlen(g_api_token)) {
    token_store_failed();
    return;
  }

  char line[160];
  snprintf(line, sizeof(line), "API token（只印這一次）: %s", g_api_token);
  console_println(line);
}

static void on_headers(size_t end) {
  HttpRequest req;
  const size_t already = g_header_len - end;
  if (!parse_http_request(g_header, end, &req)) {
    arm_drain(After::Error, 400, "請求格式不對", false, already, -1);
    return;
  }
  if (req.content_length >= 0 && already > static_cast<size_t>(req.content_length)) {
    arm_drain(After::Error, 400, "請求格式不對", false, 0, -1);
    return;
  }

  const bool api = api_path(req.path);
  g_cors = api && req.has_origin && !req.origin_overflow && origin_allowed(req.origin, true);
  g_route = route_of(req.path);

  if (strcmp(req.method, "OPTIONS") == 0) {
    if (!known_path(req.path)) {
      arm_drain(After::Error, 404, "not found", g_cors, already, req.content_length);
      return;
    }
    arm_drain(After::Status, 204, nullptr, g_cors, already, req.content_length);
    return;
  }
  if (g_route == Route::None) {
    arm_drain(After::Error, 404, "not found", false, already, req.content_length);
    return;
  }
  if (api && (req.origin_overflow || !origin_allowed(req.origin, req.has_origin))) {
    arm_drain(After::Error, 403, "來源不符", false, already, req.content_length);
    return;
  }
  if (api && !authorized(req)) {
    arm_drain(After::Error, 401, "請輸入 API token", g_cors, already, req.content_length);
    return;
  }
  if (req.chunked) {
    arm_drain(After::Error, 400, "不支援分段上傳", g_cors, already, req.content_length);
    return;
  }

  if (g_route == Route::Page) {
    if (strcmp(req.method, "GET") != 0) {
      arm_drain(After::Error, 404, "not found", false, already, req.content_length);
      return;
    }
    begin_send(200, "text/html; charset=utf-8", kConfigPage, sizeof(kConfigPage) - 1, nullptr, false);
    return;
  }

  if (req.content_length < 0) req.content_length = 0;

  if (g_route == Route::Reset) {
    if (strcmp(req.method, "POST") != 0) {
      arm_drain(After::Error, 404, "not found", g_cors, already, req.content_length);
      return;
    }
    if (req.content_length > kDrainMax) {
      arm_drain(After::Error, 413, "內容太大", g_cors, 0, -1);
      return;
    }
    arm_drain(After::Reset, 0, nullptr, g_cors, already, req.content_length);
    return;
  }

  if (g_route == Route::Fire && strcmp(req.method, "GET") == 0) {
    if (req.content_length > 0) {
      arm_drain(After::Error, 400, "請求格式不對", g_cors, already, req.content_length);
      return;
    }
    char *body = alloc_chars(32);
    if (body == nullptr) {
      begin_error(500, "記憶體不足", g_cors);
      return;
    }
    snprintf(body, 32, "{\"mode\":\"%s\"}", chords_fire_mode_name());
    begin_send(200, kJsonType, body, strlen(body), body, g_cors);
    return;
  }

  const bool post_chords = g_route == Route::Chords && strcmp(req.method, "POST") == 0;
  const bool post_fire = g_route == Route::Fire && strcmp(req.method, "POST") == 0;
  const bool get_chords = g_route == Route::Chords && strcmp(req.method, "GET") == 0;
  if (get_chords) {
    if (req.content_length > 0) {
      arm_drain(After::Error, 400, "請求格式不對", g_cors, already, req.content_length);
      return;
    }
    queue_or_error(ConfigJobKind::Get, nullptr, 0);
    return;
  }
  if (!post_chords && !post_fire) {
    arm_drain(After::Error, 404, "not found", g_cors, already, req.content_length);
    return;
  }
  if (req.content_length == 0) {
    arm_drain(After::Error, 400, "沒有內容", g_cors, already, 0);
    return;
  }
  const long cap = post_fire ? static_cast<long>(kFireBodyMax) : static_cast<long>(kChordsJsonMax);
  if (req.content_length > cap) {
    arm_drain(After::Error, 413, "內容太大", g_cors, already, req.content_length);
    return;
  }

  g_body = alloc_chars(static_cast<size_t>(req.content_length) + 1);
  if (g_body == nullptr) {
    arm_drain(After::Error, 500, "記憶體不足", g_cors, already, req.content_length);
    return;
  }
  g_body_len = static_cast<size_t>(req.content_length);
  g_body_got = already;
  if (already > 0) memcpy(g_body, g_header + end, already);
  g_body[g_body_got] = '\0';
  if (g_body_got == g_body_len) {
    finish_body();
    return;
  }
  g_conn = Conn::Body;
  g_io_ms = millis();
}

static void pump_drain() {
  uint8_t buf[kHttpSlice];
  const size_t cap = g_drain_left < kHttpSlice ? g_drain_left : kHttpSlice;
  const int n = nb_read(buf, cap);
  if (n < 0) {
    drop_conn();
    return;
  }
  if (n == 0) {
    if (peer_closed(g_client.fd()) || millis() - g_io_ms > kHttpStallMs) {
      if (peer_closed(g_client.fd())) drop_conn();
      else complete_drain();
    }
    return;
  }
  g_drain_left -= static_cast<size_t>(n);
  g_io_ms = millis();
  if (g_drain_left == 0) complete_drain();
}

static void pump_send() {
  const int fd = g_client.fd();
  if (fd < 0) {
    drop_conn();
    return;
  }
  const uint8_t *data = nullptr;
  size_t left = 0;
  size_t *cursor = nullptr;
  if (g_out.hdr_sent < g_out.hdr_len) {
    data = reinterpret_cast<const uint8_t *>(g_out.hdr) + g_out.hdr_sent;
    left = g_out.hdr_len - g_out.hdr_sent;
    cursor = &g_out.hdr_sent;
  } else if (g_out.body_sent < g_out.body_len) {
    data = reinterpret_cast<const uint8_t *>(g_out.body) + g_out.body_sent;
    left = g_out.body_len - g_out.body_sent;
    cursor = &g_out.body_sent;
  } else {
    release_out();
    g_client.stop();
    g_conn = Conn::Idle;
    return;
  }
  const int n = nb_send(fd, data, left);
  if (n < 0 || (n == 0 && (peer_closed(fd) || millis() - g_io_ms > kHttpStallMs))) {
    drop_conn();
    return;
  }
  if (n > 0) {
    *cursor += static_cast<size_t>(n);
    g_io_ms = millis();
  }
}

static void read_header() {
  if (g_header_len >= sizeof(g_header)) {
    arm_drain(After::Error, 431, "請求標頭太大", false, 0, -1);
    return;
  }
  const int n = nb_read(reinterpret_cast<uint8_t *>(g_header) + g_header_len,
                        sizeof(g_header) - g_header_len);
  if (n < 0) {
    drop_conn();
    return;
  }
  if (n == 0) {
    if (peer_closed(g_client.fd()) || millis() - g_io_ms > kHttpStallMs) drop_conn();
    return;
  }
  g_header_len += static_cast<size_t>(n);
  g_io_ms = millis();
  const size_t end = http_header_end(g_header, g_header_len);
  if (end == 0) {
    if (g_header_len >= sizeof(g_header)) arm_drain(After::Error, 431, "請求標頭太大", false, 0, -1);
    return;
  }
  on_headers(end);
}

static void read_body() {
  if (g_body == nullptr || g_body_got > g_body_len) {
    drop_conn();
    return;
  }
  const int n = nb_read(reinterpret_cast<uint8_t *>(g_body) + g_body_got, g_body_len - g_body_got);
  if (n < 0) {
    drop_conn();
    return;
  }
  if (n == 0) {
    if (peer_closed(g_client.fd()) || millis() - g_io_ms > kHttpStallMs) drop_conn();
    return;
  }
  g_body_got += static_cast<size_t>(n);
  g_body[g_body_got] = '\0';
  g_io_ms = millis();
  if (g_body_got == g_body_len) finish_body();
}

static void make_nonblock(WiFiClient &client) {
  const int fd = client.fd();
  if (fd < 0) return;
  const int flags = fcntl(fd, F_GETFL, 0);
  if (flags >= 0) fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

static void accept_client() {
  if (g_conn != Conn::Idle) return;
  if (load_job().phase != ConfigJobPhase::Idle) return;
  WiFiClient client = g_server.accept();
  if (!client) return;
  make_nonblock(client);
  g_client = client;
  g_header_len = 0;
  g_conn = Conn::Header;
  g_io_ms = millis();
}

static void service_conn() {
  if (g_conn == Conn::Idle) accept_client();
  switch (g_conn) {
    case Conn::Idle:
    case Conn::Wait:
      break;
    case Conn::Header:
      read_header();
      break;
    case Conn::Body:
      read_body();
      break;
    case Conn::Drain:
      pump_drain();
      break;
    case Conn::Send:
      pump_send();
      break;
  }
}

static bool switch_closed_raw() { return digitalRead(pin_gpio(PinFn::WifiEnable)) == LOW; }

static void poll_switch() {
  const uint32_t now = millis();
  const bool closed = switch_closed_raw();
  if (closed != g_switch_raw) {
    g_switch_raw = closed;
    g_switch_change_ms = now;
  }
  if ((now - g_switch_change_ms) >= kWifiSwitchDebounceMs && g_switch_stable != g_switch_raw) {
    g_switch_stable = g_switch_raw;
    if (!g_switch_stable) g_start_failed = false;
  }
}

static void start_ap() {
  if (g_http || g_start_failed) return;
  if (!ap_credentials_ok(kApSsid, kApPassword, kApPlaceholder)) {
    console_println("AP stayed off: set WIFI_AP_PASSWORD in include/secrets.h");
    g_start_failed = true;
    return;
  }

  WiFi.persistent(false);
  WiFi.mode(WIFI_AP);
  WiFi.setSleep(false);

  const IPAddress ip(kApIp0, kApIp1, kApIp2, kApIp3);
  WiFi.softAPConfig(ip, ip, IPAddress(255, 255, 255, 0));
  const bool up = WiFi.softAP(kApSsid, kApPassword, 1, 0, 4);

  char line[96];
  if (!up) {
    snprintf(line, sizeof(line), "AP %s failed to start", kApSsid);
    console_println(line);
    WiFi.softAPdisconnect(true);
    WiFi.mode(WIFI_OFF);
    g_start_failed = true;
    return;
  }

  snprintf(line, sizeof(line), "AP %s", kApSsid);
  console_println(line);
  snprintf(line, sizeof(line), "http://%u.%u.%u.%u/", static_cast<unsigned>(kApIp0),
           static_cast<unsigned>(kApIp1), static_cast<unsigned>(kApIp2),
           static_cast<unsigned>(kApIp3));
  console_println(line);
  g_server.begin();
  g_http = true;
}

static void stop_ap() {
  if (!g_http) return;
  g_http = false;
  const ConfigJobPhase phase = load_job().phase;
  g_drop_result = g_conn == Conn::Wait || phase != ConfigJobPhase::Idle;
  if (g_conn != Conn::Wait) drop_conn();
  else g_client.stop();
  g_server.stop();
  WiFi.softAPdisconnect(true);
  WiFi.mode(WIFI_OFF);
  console_println("AP off");
}

void config_server_begin() {
  console_ensure();
  load_api_token();
  pinMode(pin_gpio(PinFn::WifiEnable), INPUT_PULLUP);
  g_switch_raw = switch_closed_raw();
  g_switch_stable = g_switch_raw;
  g_switch_change_ms = millis();
  if (g_worker == nullptr) {
    const BaseType_t ok =
        xTaskCreatePinnedToCore(worker_fn, "cfghttp", 16384, nullptr, 1, &g_worker, 0);
    if (ok != pdPASS) {
      g_worker = nullptr;
      console_println("HTTP worker failed; chord save and load stay off");
    }
  }
  char line[48];
  snprintf(line, sizeof(line), "WiFi switch GPIO%d, closed to GND", pin_gpio(PinFn::WifiEnable));
  console_println(line);
  if (g_switch_stable) start_ap();
}

void config_server_poll() {
  poll_switch();
  service_job();
  if (g_switch_stable && !g_http) start_ap();
  else if (!g_switch_stable && g_http) stop_ap();
  if (g_http) service_conn();
  service_job();
}

bool config_ap_is_up() { return g_http; }
