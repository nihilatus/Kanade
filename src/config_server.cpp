#include "config_server.h"

#include <stdio.h>
#include <string.h>

#include <Arduino.h>
#include <ArduinoJson.h>
#include <LittleFS.h>
#include <WebServer.h>
#include <WiFi.h>

#include "chords.h"
#include "console_log.h"

#if __has_include("secrets.h")
#include "secrets.h"
#endif

#ifndef WIFI_AP_SSID
#define WIFI_AP_SSID "JIS-Chord"
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
static constexpr char kApPlaceholder[] = "replace-with-8-or-more-chars";

// Not one of the 14 chord keys. Closed to GND enables the AP.
static constexpr int kWifiEnablePin = 15;
// Longer than the 8 ms key debounce so a bounce does not flap the radio.
static constexpr uint32_t kWifiSwitchDebounceMs = 50;

static bool g_http = false;
static bool g_routes = false;
static bool g_switch_raw = false;
static bool g_switch_stable = false;
static uint32_t g_switch_change_ms = 0;
static bool g_start_failed = false;

static bool ap_password_ok() {
  const size_t n = strlen(kApPassword);
  if (n < 8 || n > 63) return false;
  if (strcmp(kApPassword, kApPlaceholder) == 0) return false;
  if (kApSsid[0] == '\0' || strlen(kApSsid) > 32) return false;
  return true;
}

static WebServer server(80);

static const char kConfigPage[] PROGMEM =
#include "config_page.inc"
    ;

static void send_json(int code, const char *body) {
  server.sendHeader("Cache-Control", "no-store");
  server.sendHeader("Access-Control-Allow-Origin", "*");
  server.sendHeader("Access-Control-Allow-Methods", "GET, POST, OPTIONS");
  server.sendHeader("Access-Control-Allow-Headers", "Content-Type, X-Api-Token");
  server.send(code, "application/json; charset=utf-8", body);
}

static void handle_options() {
  server.sendHeader("Access-Control-Allow-Origin", "*");
  server.sendHeader("Access-Control-Allow-Methods", "GET, POST, OPTIONS");
  server.sendHeader("Access-Control-Allow-Headers", "Content-Type, X-Api-Token");
  server.send(204);
}

static void send_error(int code, const char *msg) {
  JsonDocument doc;
  doc["ok"] = false;
  doc["error"] = msg != nullptr ? msg : "失敗";
  char buf[512];
  const size_t n = serializeJson(doc, buf, sizeof(buf));
  if (n == 0 || n >= sizeof(buf)) {
    send_json(code, "{\"ok\":false,\"error\":\"失敗\"}");
    return;
  }
  send_json(code, buf);
}

static bool api_token_ok() {
  if (kApiToken[0] == '\0') return true;
  if (!server.hasHeader("X-Api-Token")) return false;
  const String &got = server.header("X-Api-Token");
  const size_t n = strlen(kApiToken);
  if (got.length() != n) return false;
  unsigned diff = 0;
  for (size_t i = 0; i < n; ++i) {
    diff |= static_cast<unsigned char>(got[i]) ^ static_cast<unsigned char>(kApiToken[i]);
  }
  return diff == 0;
}

static bool reject_unauthorized() {
  if (api_token_ok()) return false;
  send_error(401, "請輸入 API token");
  return true;
}

static void handle_root() {
  server.sendHeader("Cache-Control", "no-store");
  server.send_P(200, "text/html; charset=utf-8", kConfigPage);
}

static void handle_get() {
  if (reject_unauthorized()) return;
  char *json = chords_format_json();
  if (json == nullptr) {
    send_error(500, "記憶體不足");
    return;
  }
  send_json(200, json);
  chords_release(json);
}

static bool write_chords_file(const uint8_t *data, size_t len) {
  // .tmp is closed before chords.json moves aside. A power cut between
  // renaming chords.json to .bak and renaming .tmp to chords.json leaves
  // those two files only; try_load_saved() adopts .tmp, then .bak.
  LittleFS.remove(kChordsTmpPath);
  File file = LittleFS.open(kChordsTmpPath, "w");
  if (!file) return false;
  const size_t wrote = file.write(data, len);
  file.close();
  if (wrote != len) {
    LittleFS.remove(kChordsTmpPath);
    return false;
  }

  LittleFS.remove(kChordsBakPath);
  const bool had = LittleFS.exists(kChordsPath);
  if (had && !LittleFS.rename(kChordsPath, kChordsBakPath)) {
    LittleFS.remove(kChordsTmpPath);
    return false;
  }
  if (!LittleFS.rename(kChordsTmpPath, kChordsPath)) {
    if (had) LittleFS.rename(kChordsBakPath, kChordsPath);
    LittleFS.remove(kChordsTmpPath);
    return false;
  }
  LittleFS.remove(kChordsBakPath);
  return true;
}

static void handle_post() {
  if (reject_unauthorized()) return;
  if (!chords_fs_ready()) {
    send_error(500, "檔案系統還沒備好");
    return;
  }
  if (!server.hasArg("plain")) {
    send_error(400, "沒有內容");
    return;
  }
  const String &body = server.arg("plain");
  if (body.length() == 0) {
    send_error(400, "沒有內容");
    return;
  }
  if (body.length() > kChordsJsonMax) {
    send_error(413, "內容太大");
    return;
  }

  char err[160];
  err[0] = '\0';
  if (chords_stage_json(body.c_str(), body.length(), err, sizeof(err)) != ChordStageStatus::Ok) {
    chords_drop_staged();
    send_error(400, err[0] != '\0' ? err : "JSON 格式不對");
    return;
  }
  if (!write_chords_file(reinterpret_cast<const uint8_t *>(body.c_str()), body.length())) {
    chords_drop_staged();
    send_error(500, "存檔失敗");
    return;
  }
  chords_commit_staged();

  char ok[80];
  snprintf(ok, sizeof(ok), "{\"ok\":true,\"source\":\"saved\",\"count\":%u}",
           static_cast<unsigned>(chords_count()));
  send_json(200, ok);
}

static void handle_reset() {
  if (reject_unauthorized()) return;
  if (chords_fs_ready() && LittleFS.exists(kChordsPath)) {
    if (!LittleFS.remove(kChordsPath)) {
      send_error(500, "存檔失敗");
      return;
    }
  }
  if (chords_fs_ready()) {
    LittleFS.remove(kChordsTmpPath);
    LittleFS.remove(kChordsBakPath);
  }
  chords_load_factory();

  char *json = chords_format_json();
  if (json == nullptr) {
    send_error(500, "記憶體不足");
    return;
  }
  send_json(200, json);
  chords_release(json);
}

static void handle_fire_get() {
  if (reject_unauthorized()) return;
  char body[32];
  snprintf(body, sizeof(body), "{\"mode\":\"%s\"}", chords_fire_mode_name());
  send_json(200, body);
}

static void handle_fire_post() {
  if (reject_unauthorized()) return;
  if (!server.hasArg("plain")) {
    send_error(400, "沒有內容");
    return;
  }
  JsonDocument doc;
  const DeserializationError error = deserializeJson(doc, server.arg("plain"));
  const char *mode = nullptr;
  if (!error && doc["mode"].is<const char *>()) mode = doc["mode"].as<const char *>();
  if (mode == nullptr || !chords_set_fire_mode_name(mode)) {
    send_error(400, "模式只能是 release 或 first");
    return;
  }
  char body[48];
  snprintf(body, sizeof(body), "{\"ok\":true,\"mode\":\"%s\"}", chords_fire_mode_name());
  send_json(200, body);
}

static void handle_404() { server.send(404, "text/plain; charset=utf-8", "not found"); }

static void register_routes() {
  if (g_routes) return;
  static const char *kHeaders[] = {"X-Api-Token"};
  server.collectHeaders(kHeaders, 1);
  server.on("/", HTTP_GET, handle_root);
  server.on("/", HTTP_OPTIONS, handle_options);
  server.on("/api/chords", HTTP_GET, handle_get);
  server.on("/api/chords", HTTP_POST, handle_post);
  server.on("/api/chords", HTTP_OPTIONS, handle_options);
  server.on("/api/chords/reset", HTTP_POST, handle_reset);
  server.on("/api/chords/reset", HTTP_OPTIONS, handle_options);
  server.on("/api/fire", HTTP_GET, handle_fire_get);
  server.on("/api/fire", HTTP_POST, handle_fire_post);
  server.on("/api/fire", HTTP_OPTIONS, handle_options);
  server.onNotFound(handle_404);
  g_routes = true;
}

static bool switch_closed_raw() { return digitalRead(kWifiEnablePin) == LOW; }

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
  if (!ap_password_ok()) {
    console_println("AP stayed off: set WIFI_AP_PASSWORD in include/secrets.h");
    g_start_failed = true;
    return;
  }

  WiFi.persistent(false);
  WiFi.mode(WIFI_AP);
  WiFi.setSleep(false);

  const IPAddress ip(192, 168, 4, 1);
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
  console_println("http://192.168.4.1/");
  server.begin();
  g_http = true;
}

static void stop_ap() {
  if (!g_http) return;
  g_http = false;
  server.stop();
  WiFi.softAPdisconnect(true);
  WiFi.mode(WIFI_OFF);
  console_println("AP off");
}

void config_server_begin() {
  console_ensure();
  pinMode(kWifiEnablePin, INPUT_PULLUP);
  g_switch_raw = switch_closed_raw();
  g_switch_stable = g_switch_raw;
  g_switch_change_ms = millis();
  register_routes();
  console_println("WiFi switch GPIO15, closed to GND");
  if (g_switch_stable) start_ap();
}

void config_server_poll() {
  poll_switch();
  if (g_switch_stable && !g_http) start_ap();
  else if (!g_switch_stable && g_http) stop_ap();
  if (g_http) server.handleClient();
}
