#include "config_server.h"
#include "config_runtime.h"

#include <stdio.h>
#include <string.h>

#include <atomic>

#include <Arduino.h>
#include <Network.h>
#include <Preferences.h>
#include <WiFi.h>
#include <esp_random.h>

#include "config_http.h"
#include "console_log.h"
#include "output_policy.h"
#include "pin_map.h"

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

static constexpr char kApiTokenNs[] = "jis";
static constexpr char kApiTokenKey[] = "apitoken";
static constexpr size_t kApiTokenRandomBytes = 16;

static char g_api_token[128];
static constexpr char kApPlaceholder[] = "replace-with-8-or-more-chars";

static bool g_http = false;
static bool g_switch_raw = false;
static bool g_switch_stable = false;
static uint32_t g_switch_change_ms = 0;
static bool g_start_failed = false;
static OutputMode g_output_mode = OutputMode::Usb;
static bool g_wifi_boot_closed = false;
static bool g_restart = false;

Conn g_conn = Conn::Idle;
uint32_t g_io_ms = 0;
char g_header[kHttpHeaderMax];
size_t g_header_len = 0;
char *g_body = nullptr;
size_t g_body_len = 0;
size_t g_body_got = 0;
bool g_cors = false;
ConfigRouteId g_route = ConfigRouteId::None;
After g_after = After::Error;
int g_after_code = 400;
const char *g_after_msg = "失敗";
size_t g_drain_left = 0;
Outgoing g_out = {};

std::atomic<uint8_t> g_job_phase{static_cast<uint8_t>(ConfigJobPhase::Idle)};
ConfigJobKind g_job_kind = ConfigJobKind::None;
char *g_job_body = nullptr;
size_t g_job_len = 0;
int g_job_code = 500;
char *g_job_resp = nullptr;
bool g_job_main_ok = false;
unsigned g_job_count = 0;
TaskHandle_t g_worker = nullptr;
bool g_drop_result = false;

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

bool config_token_ok(const HttpRequest &req) {
  if (req.token_overflow) return false;
  // token_matches treats an empty expected token as allow-all. An unresolved
  // token must deny /api/* instead.
  if (g_api_token[0] == '\0') return false;
  if (!req.has_token) return false;
  return token_matches(g_api_token, req.token);
}

static bool switch_closed_raw() { return digitalRead(pin_gpio(PinFn::WifiEnable)) == LOW; }

static void poll_switch() {
  const uint32_t now = millis();
  const bool closed = switch_closed_raw();
  if (closed != g_switch_raw) {
    g_switch_raw = closed;
    g_switch_change_ms = now;
  }
  if ((now - g_switch_change_ms) >= kSwitchDebounceMs && g_switch_stable != g_switch_raw) {
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
  config_conn_listen();
  g_http = true;
}

static void stop_ap() {
  if (!g_http) return;
  g_http = false;
  config_conn_halt();
  WiFi.softAPdisconnect(true);
  WiFi.mode(WIFI_OFF);
  console_println("AP off");
}

void config_server_set_output_mode(OutputMode mode) { g_output_mode = mode; }

bool config_server_restart_requested() { return g_restart; }

bool config_server_wifi_closed() { return g_switch_stable; }

void config_server_begin() {
  console_ensure();
  load_api_token();
  pinMode(pin_gpio(PinFn::WifiEnable), INPUT_PULLUP);
  g_switch_raw = switch_closed_raw();
  g_switch_stable = g_switch_raw;
  g_wifi_boot_closed = g_switch_stable;
  g_switch_change_ms = millis();
  config_worker_start();
  char line[48];
  snprintf(line, sizeof(line), "WiFi switch GPIO%d, closed to GND", pin_gpio(PinFn::WifiEnable));
  console_println(line);
  if (g_switch_stable) start_ap();
}

void config_server_poll() {
  poll_switch();
  config_worker_service();
  if (wifi_edge_requests_restart(g_output_mode, g_wifi_boot_closed, g_switch_stable)) {
    // Do not start or stop the radio here. The caller restarts, and the next
    // boot either serves the AP or starts BLE.
    g_restart = true;
  } else if (!g_restart) {
    if (g_switch_stable && !g_http) start_ap();
    else if (!g_switch_stable && g_http) stop_ap();
  }
  if (g_http) config_conn_service();
  config_worker_service();
}

bool config_ap_is_up() { return g_http; }
