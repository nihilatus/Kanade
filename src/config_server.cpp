#include "config_server.h"

#include <string.h>

#include <ArduinoJson.h>
#include <WebServer.h>
#include <WiFi.h>

#include "chords.h"
#include "config_page.h"

#if __has_include("secrets.h")
#include "secrets.h"
#else
#error "Copy include/secrets.h.example to include/secrets.h and set WIFI_AP_PASSWORD. That file is gitignored."
#endif

#ifndef WIFI_AP_SSID
#error "include/secrets.h must define WIFI_AP_SSID"
#endif
#ifndef WIFI_AP_PASSWORD
#error "include/secrets.h must define WIFI_AP_PASSWORD"
#endif

static const char *kPasswordPlaceholder = "replace-with-8-or-more-chars";

static WebServer g_server(80);
static bool g_started = false;

static void send_json(int status, const String &body) {
  g_server.send(status, "application/json; charset=utf-8", body);
}

static String error_json(const String &error) {
  String out;
  JsonDocument doc;
  doc["error"] = error;
  serializeJson(doc, out);
  return out;
}

static bool password_ok() {
  const size_t length = strlen(WIFI_AP_PASSWORD);
  if (length < 8 || length > 63) return false;
  if (strcmp(WIFI_AP_PASSWORD, kPasswordPlaceholder) == 0) return false;
  return true;
}

static void handle_get() {
  String body;
  if (!chords_export_json(body)) {
    send_json(500, error_json("無法讀取和弦表"));
    return;
  }
  send_json(200, body);
}

static void handle_post() {
  if (!g_server.hasArg("plain")) {
    send_json(400, error_json("沒有收到 JSON"));
    return;
  }
  String error;
  if (!chords_save_json(g_server.arg("plain"), error)) {
    send_json(400, error_json(error));
    return;
  }
  send_json(200, "{\"ok\":true}");
}

static void handle_reset() {
  String error;
  if (!chords_reset_saved(error)) {
    send_json(500, error_json(error));
    return;
  }
  send_json(200, "{\"ok\":true}");
}

void config_server_begin() {
  if (!password_ok()) {
#if ARDUINO_USB_CDC_ON_BOOT
    Serial.println(F("Wi-Fi AP off: set an 8+ character password in include/secrets.h"));
#endif
    return;
  }

  WiFi.mode(WIFI_AP);
  WiFi.softAPConfig(IPAddress(192, 168, 4, 1), IPAddress(192, 168, 4, 1), IPAddress(255, 255, 255, 0));
  const bool up = WiFi.softAP(WIFI_AP_SSID, WIFI_AP_PASSWORD);
#if ARDUINO_USB_CDC_ON_BOOT
  if (!up) {
    Serial.println(F("Wi-Fi AP failed to start"));
  } else {
    Serial.print(F("Wi-Fi AP SSID "));
    Serial.println(WIFI_AP_SSID);
    Serial.println(F("Open http://192.168.4.1/"));
    Serial.println(F("AP password stays in include/secrets.h and is not printed"));
  }
#else
  (void)up;
#endif
  if (!up) return;

  g_server.on("/", HTTP_GET, []() {
    g_server.send_P(200, "text/html; charset=utf-8", kConfigPage);
  });
  g_server.on("/api/chords", HTTP_GET, handle_get);
  g_server.on("/api/chords", HTTP_POST, handle_post);
  g_server.on("/api/chords/reset", HTTP_POST, handle_reset);
  g_server.begin();
  g_started = true;
}

void config_server_poll() {
  if (g_started) g_server.handleClient();
}
