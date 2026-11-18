#include "config_runtime.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>

#include <Arduino.h>
#include <ArduinoJson.h>
#include <Network.h>
#include <WiFi.h>
#include <esp_heap_caps.h>
#include <lwip/sockets.h>

#include "chords.h"
#include "config_route.h"

#undef write
#undef read
#undef send
#undef recv

static_assert(static_cast<unsigned>(ConfigRouteDrain::Error) == static_cast<unsigned>(After::Error),
              "drain error shares After::Error");
static_assert(static_cast<unsigned>(ConfigRouteDrain::Status) == static_cast<unsigned>(After::Status),
              "drain status shares After::Status");
static_assert(static_cast<unsigned>(ConfigRouteDrain::Reset) == static_cast<unsigned>(After::Reset),
              "drain reset shares After::Reset");
static_assert(static_cast<unsigned>(ConfigRouteDrain::TimingReset) ==
                  static_cast<unsigned>(After::TimingReset),
              "drain timing reset shares After::TimingReset");

static const char kJsonType[] = "application/json; charset=utf-8";
static const char kFallbackJson[] = "{\"ok\":false,\"error\":\"失敗\"}";

static WiFiServer g_server(80);
static WiFiClient g_client;

static const char kConfigPage[] PROGMEM =
#include "config_page.inc"
    ;

char *alloc_chars(size_t n) {
  void *pointer = ps_malloc(n);
  if (pointer == nullptr) pointer = malloc(n);
  return static_cast<char *>(pointer);
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

char *json_error(const char *msg) {
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

void begin_send(int code, const char *type, const char *body, size_t len, char *owned, bool cors) {
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

void begin_error(int code, const char *msg, bool cors) {
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

void drop_conn() {
  release_out();
  chords_release(g_body);
  g_body = nullptr;
  g_client.stop();
  g_conn = Conn::Idle;
}

static void complete_drain();

static void arm_drain(After after, int code, const char *msg, bool cors, size_t already,
                      long content_length) {
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

static ConfigJobView load_job() {
  ConfigJobView view;
  view.phase = static_cast<ConfigJobPhase>(g_job_phase.load(std::memory_order_acquire));
  view.kind = g_job_kind;
  view.main_ok = g_job_main_ok;
  return view;
}

static void handle_fire(char *body, size_t len) {
  (void)len;
  chords_release(body);
  begin_error(409, "送出模式由開關決定", g_cors);
}

static void finish_body() {
  char *body = g_body;
  const size_t len = g_body_len;
  g_body = nullptr;
  if (body != nullptr && len <= g_body_len) body[len] = '\0';
  if (g_route == ConfigRouteId::Fire) {
    handle_fire(body, len);
    return;
  }
  if (g_route == ConfigRouteId::Timing) {
    queue_or_error(ConfigJobKind::TimingPost, body, len);
    return;
  }
  if (g_route == ConfigRouteId::Chords) {
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
  if (g_after == After::TimingReset) {
    queue_or_error(ConfigJobKind::TimingReset, nullptr, 0);
    return;
  }
  begin_error(g_after_code, g_after_msg, g_cors);
}

static void apply_decision(const HttpRequest &req, size_t end, size_t already,
                           const ConfigRouteDecision &decision) {
  g_cors = decision.cors;
  if (decision.step == ConfigRouteStep::Drain) {
    const size_t got = decision.drain_body ? already : 0;
    const long length = decision.drain_body ? req.content_length : -1;
    arm_drain(static_cast<After>(decision.drain), decision.code, decision.message, decision.cors, got,
              length);
    return;
  }
  if (decision.step == ConfigRouteStep::SendPage) {
    begin_send(200, "text/html; charset=utf-8", kConfigPage, sizeof(kConfigPage) - 1, nullptr, false);
    return;
  }
  if (decision.step == ConfigRouteStep::SendFire) {
    char *body = alloc_chars(32);
    if (body == nullptr) {
      begin_error(500, "記憶體不足", g_cors);
      return;
    }
    snprintf(body, 32, "{\"mode\":\"%s\"}", chords_fire_mode_name());
    begin_send(200, kJsonType, body, strlen(body), body, g_cors);
    return;
  }
  if (decision.step == ConfigRouteStep::SendTiming) {
    char *body = config_format_timing_json(false);
    if (body == nullptr) {
      begin_error(500, "記憶體不足", g_cors);
      return;
    }
    begin_send(200, kJsonType, body, strlen(body), body, g_cors);
    return;
  }
  if (decision.step == ConfigRouteStep::QueueGet) {
    queue_or_error(ConfigJobKind::Get, nullptr, 0);
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
  g_route = config_route_id(req.path);
  apply_decision(req, end, already, decide_config_route(req, config_token_ok(req)));
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

void config_conn_listen() { g_server.begin(); }

void config_conn_halt() {
  const ConfigJobPhase phase = load_job().phase;
  g_drop_result = g_conn == Conn::Wait || phase != ConfigJobPhase::Idle;
  if (g_conn != Conn::Wait) drop_conn();
  else g_client.stop();
  g_server.stop();
}

void config_conn_abandon_wait() {
  if (g_conn != Conn::Wait) return;
  g_client.stop();
  g_conn = Conn::Idle;
}

void config_conn_service() {
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
