#include "config_server.h"

#include <stdio.h>
#include <string.h>

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

static const char kApSsid[] = WIFI_AP_SSID;
static const char kApPassword[] = WIFI_AP_PASSWORD;
static constexpr char kApPlaceholder[] = "replace-with-8-or-more-chars";
static bool g_http = false;

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
  server.send(code, "application/json; charset=utf-8", body);
}

static void send_error(int code, const char *msg) {
  char buf[256];
  snprintf(buf, sizeof(buf), "{\"ok\":false,\"error\":\"%s\"}", msg != nullptr ? msg : "失敗");
  send_json(code, buf);
}

static void handle_root() {
  server.sendHeader("Cache-Control", "no-store");
  server.send_P(200, "text/html; charset=utf-8", kConfigPage);
}

static void handle_get() {
  char *json = chords_format_json();
  if (json == nullptr) {
    send_error(500, "記憶體不足");
    return;
  }
  send_json(200, json);
  free(json);
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
  if (!chords_stage_json(body.c_str(), body.length(), err, sizeof(err))) {
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
  free(json);
}

static void handle_404() { server.send(404, "text/plain; charset=utf-8", "not found"); }

void config_server_begin() {
  console_ensure();
  if (!ap_password_ok()) {
    console_println("AP stayed off: set WIFI_AP_PASSWORD in include/secrets.h");
    return;
  }

  WiFi.persistent(false);
  WiFi.mode(WIFI_AP);
  WiFi.setSleep(false);

  const IPAddress ip(192, 168, 4, 1);
  WiFi.softAPConfig(ip, ip, IPAddress(255, 255, 255, 0));
  const bool up = WiFi.softAP(kApSsid, kApPassword, 1, 0, 4);

  char line[96];
  if (up) {
    snprintf(line, sizeof(line), "AP %s", kApSsid);
    console_println(line);
    console_println("http://192.168.4.1/");
  } else {
    snprintf(line, sizeof(line), "AP %s failed to start", kApSsid);
    console_println(line);
    return;
  }

  server.on("/", HTTP_GET, handle_root);
  server.on("/api/chords", HTTP_GET, handle_get);
  server.on("/api/chords", HTTP_POST, handle_post);
  server.on("/api/chords/reset", HTTP_POST, handle_reset);
  server.onNotFound(handle_404);
  server.begin();
  g_http = true;
}

void config_server_poll() {
  if (g_http) server.handleClient();
}
