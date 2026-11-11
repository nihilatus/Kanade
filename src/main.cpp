#include <Arduino.h>

#include <stdio.h>
#include <string.h>

#include <esp_system.h>

#include "buttons.h"
#include "chord_core.h"
#include "chords.h"
#include "config_server.h"
#include "console_log.h"
#include "fire_switch.h"
#include "hid_keyboard.h"
#include "mode_switches.h"
#include "output_policy.h"
#include "output_switch.h"

// Set to 0 (or -DCHORD_MEASURE_LOOP=0) to leave loop timing out of the scan.
#ifndef CHORD_MEASURE_LOOP
#define CHORD_MEASURE_LOOP 1
#endif
#if CHORD_MEASURE_LOOP
#include <WiFi.h>
#endif

// Kanade on ESP32-S3-DevKitC-1 N16R8 — chord keyboard.
// GPIO16 open: USB HID on the port labeled USB (OTG). Closed to GND: BLE HID.
// Plug the UART header in for the log. Chord switches: GPIO to one side, GND to the other.

void setup() {
  console_ensure();
#if ARDUINO_USB_CDC_ON_BOOT
  delay(200);
  Serial.println();
  Serial.println(F("Kanade"));
  Serial.print(F("14 direct keys, active-low, "));
  Serial.print(kChordDebounceMs);
  Serial.println(F(" ms debounce"));
  Serial.print(F("pins"));
  for (uint8_t i = 0; i < BTN_COUNT; ++i) {
    Serial.print(' ');
    Serial.print(kButtons[i].name);
    Serial.print(F("=GPIO"));
    Serial.print(kButtons[i].pin);
  }
  Serial.println();
#endif

  buttons_begin();
  output_switch_begin();
  fire_switch_begin();
  chords_begin();
  config_server_set_output_mode(output_switch_mode());
  config_server_begin();
  const OutputMode mode = output_switch_mode();
  if (output_starts_usb(mode)) {
    if (!hid_keyboard_begin(OutputMode::Usb)) console_println("USB keyboard failed");
  } else if (output_starts_ble(mode, config_server_wifi_closed())) {
    if (!hid_keyboard_begin(OutputMode::Ble)) console_println("BLE keyboard failed");
  } else {
    console_println("BLE held, AP only");
  }
}

static bool mode_switch_level_closed(ModeSwitch id) {
  switch (id) {
    case ModeSwitch::Wifi:
      return config_server_wifi_closed();
    case ModeSwitch::Output:
      return output_switch_closed();
    case ModeSwitch::Fire:
      return fire_switch_mode() == ChordFireMode::FirstUp;
    case ModeSwitch::Count:
      break;
  }
  return false;
}

static void print_switches() {
  char line[72];
  snprintf(line, sizeof(line), "switches %d", kModeSwitchCount);
  console_println(line);
  const OutputMode boot = output_switch_mode();
  for (int i = 0; i < kModeSwitchCount; ++i) {
    const ModeSwitchDesc &row = kModeSwitches[i];
    const bool closed = mode_switch_level_closed(row.id);
    const char *label = closed ? row.closed_label : row.open_label;
    if (row.id == ModeSwitch::Output) {
      label = boot == OutputMode::Ble ? row.closed_label : row.open_label;
    }
    const char *edge = mode_switch_restarts(row.change, boot) ? "restart" : "live";
    if (row.id == ModeSwitch::Wifi) {
      snprintf(line, sizeof(line), "%s GPIO%d %s %s %s %s", row.key, pin_gpio(row.pin),
               closed ? "closed" : "open", label, config_ap_is_up() ? "AP" : "AP down", edge);
    } else {
      snprintf(line, sizeof(line), "%s GPIO%d %s %s %s", row.key, pin_gpio(row.pin),
               closed ? "closed" : "open", label, edge);
    }
    console_println(line);
  }
}

static void restart_for_switch() {
  const bool output = output_switch_restart_requested();
  const bool wifi = config_server_restart_requested();
  if (!output && !wifi) return;
  hid_keyboard_silence();
  console_println(output ? "output switch restart" : "wifi switch restart");
  delay(50);
  esp_restart();
}

struct CmdPort {
  Stream *in;
  char buf[24];
  uint8_t len;
  bool overflow;
};

// `keys` toggles this. While on, a debounced buttons_mask() change prints
// bit-order names, for example LL+LU+RT0. Empty mask prints "-".
static bool g_key_trace = false;
static uint16_t g_key_trace_mask = 0;

static void format_key_trace(uint16_t mask, char *buf, size_t buflen) {
  if (buflen == 0) return;
  size_t used = 0;
  buf[0] = '\0';
  for (int i = 0; i < kChordKeyCount; ++i) {
    if ((mask & static_cast<uint16_t>(1u << i)) == 0) continue;
    const char *name = chord_key_name(i);
    if (name == nullptr) continue;
    const size_t len = strlen(name);
    const size_t extra = used == 0 ? 0 : 1;
    if (used + extra + len + 1 > buflen) break;
    if (extra != 0) buf[used++] = '+';
    memcpy(buf + used, name, len);
    used += len;
    buf[used] = '\0';
  }
  if (used == 0 && buflen > 1) {
    buf[0] = '-';
    buf[1] = '\0';
  }
}

static void dump_button_trace() {
  const size_t n = button_trace_count();
  if (n == 0) {
    console_println("trace empty");
    return;
  }
  char line[80];
  char keys[64];
  for (size_t i = 0; i < n; ++i) {
    ButtonTraceEvent ev;
    if (!button_trace_at(i, &ev)) break;
    if (ev.kind == ButtonTraceKind::Raw) {
      const char *name = ev.index < kChordKeyCount ? chord_key_name(ev.index) : nullptr;
      if (name == nullptr) name = "?";
      snprintf(line, sizeof(line), "%lu raw %s %s", static_cast<unsigned long>(ev.ms), name,
               ev.value != 0 ? "down" : "up");
    } else {
      format_key_trace(ev.value, keys, sizeof(keys));
      snprintf(line, sizeof(line), "%lu stable %s", static_cast<unsigned long>(ev.ms), keys);
    }
    console_println(line);
  }
  snprintf(line, sizeof(line), "trace %u", static_cast<unsigned>(n));
  console_println(line);
}

static void trace_keys_if_changed() {
  if (!g_key_trace) return;
  const uint16_t mask = buttons_mask();
  if (mask == g_key_trace_mask) return;
  g_key_trace_mask = mask;
  char line[64];
  format_key_trace(mask, line, sizeof(line));
  console_println(line);
}

static void poll_fire_cmd(CmdPort &port) {
  if (port.in == nullptr) return;
  while (port.in->available() > 0) {
    const int raw = port.in->read();
    if (raw < 0) break;
    const char c = static_cast<char>(raw);
    if (c == '\r') continue;
    if (c != '\n') {
      if (port.overflow) continue;
      if (port.len + 1 < sizeof(port.buf)) port.buf[port.len++] = c;
      else {
        port.overflow = true;
        port.len = 0;
      }
      continue;
    }
    if (port.overflow) {
      port.overflow = false;
      port.len = 0;
      console_println("指令過長");
      continue;
    }
    port.buf[port.len] = '\0';
    port.len = 0;

    char *arg = port.buf;
    while (*arg == ' ' || *arg == '\t') ++arg;
    if (strncmp(arg, "drops", 5) == 0 && (arg[5] == '\0' || arg[5] == ' ' || arg[5] == '\t')) {
      char *rest = arg + 5;
      while (*rest == ' ' || *rest == '\t') ++rest;
      size_t n = strlen(rest);
      while (n > 0 && (rest[n - 1] == ' ' || rest[n - 1] == '\t')) rest[--n] = '\0';
      if (rest[0] != '\0') continue;
      hid_print_drops();
      continue;
    }
    if (strncmp(arg, "keys", 4) == 0 && (arg[4] == '\0' || arg[4] == ' ' || arg[4] == '\t')) {
      char *rest = arg + 4;
      while (*rest == ' ' || *rest == '\t') ++rest;
      size_t n = strlen(rest);
      while (n > 0 && (rest[n - 1] == ' ' || rest[n - 1] == '\t')) rest[--n] = '\0';
      if (rest[0] != '\0') continue;
      g_key_trace = !g_key_trace;
      if (g_key_trace) g_key_trace_mask = buttons_mask();
      console_println(g_key_trace ? "keys on" : "keys off");
      continue;
    }
    if (strncmp(arg, "trace", 5) == 0 && (arg[5] == '\0' || arg[5] == ' ' || arg[5] == '\t')) {
      char *rest = arg + 5;
      while (*rest == ' ' || *rest == '\t') ++rest;
      size_t n = strlen(rest);
      while (n > 0 && (rest[n - 1] == ' ' || rest[n - 1] == '\t')) rest[--n] = '\0';
      if (rest[0] != '\0') continue;
      dump_button_trace();
      continue;
    }
    if (strncmp(arg, "switches", 8) == 0 && (arg[8] == '\0' || arg[8] == ' ' || arg[8] == '\t')) {
      char *rest = arg + 8;
      while (*rest == ' ' || *rest == '\t') ++rest;
      size_t n = strlen(rest);
      while (n > 0 && (rest[n - 1] == ' ' || rest[n - 1] == '\t')) rest[--n] = '\0';
      if (rest[0] != '\0') continue;
      print_switches();
      continue;
    }
    if (strncmp(arg, "fire", 4) != 0) continue;
    if (arg[4] != '\0' && arg[4] != ' ' && arg[4] != '\t') continue;
    arg += 4;
    while (*arg == ' ' || *arg == '\t') ++arg;
    size_t n = strlen(arg);
    while (n > 0 && (arg[n - 1] == ' ' || arg[n - 1] == '\t')) arg[--n] = '\0';
    if (arg[0] == '\0') {
      char line[40];
      snprintf(line, sizeof(line), "fire %s", chords_fire_mode_name());
      console_println(line);
      continue;
    }
    console_println("送出模式由開關決定");
  }
}

static CmdPort g_uart{&Serial0, {}, 0, false};
#if ARDUINO_USB_CDC_ON_BOOT
static CmdPort g_usb{&Serial, {}, 0, false};
#endif

#if CHORD_MEASURE_LOOP
// First 2000 loop() bodies in each WiFi mode. delay(1) is included.
static constexpr uint32_t kLoopMeasureSamples = 2000;

static uint32_t g_loop_max_off_us = 0;
static uint32_t g_loop_max_on_us = 0;
static uint32_t g_loop_n_off = 0;
static uint32_t g_loop_n_on = 0;
static bool g_loop_done_off = false;
static bool g_loop_done_on = false;

static bool loop_measure_done() { return g_loop_done_off && g_loop_done_on; }

static void loop_measure_note(uint32_t elapsed_us, bool ap_on) {
  uint32_t *max_us = ap_on ? &g_loop_max_on_us : &g_loop_max_off_us;
  uint32_t *count = ap_on ? &g_loop_n_on : &g_loop_n_off;
  bool *done = ap_on ? &g_loop_done_on : &g_loop_done_off;
  if (*done) return;
  if (elapsed_us > *max_us) *max_us = elapsed_us;
  if (++(*count) < kLoopMeasureSamples) return;
  *done = true;
  char line[48];
  snprintf(line, sizeof(line), "loop max AP %s %u us", ap_on ? "on" : "off",
           static_cast<unsigned>(*max_us));
  console_println(line);
}
#endif

// False until loop() sees the AP. setup() may already have started it.
static bool g_ap_was_up = false;

void loop() {
#if CHORD_MEASURE_LOOP
  const bool timing = !loop_measure_done();
  const uint32_t loop_t0 = timing ? micros() : 0;
#endif
  output_switch_poll(millis());
  config_server_poll();
  restart_for_switch();
  const bool ap_up = config_ap_is_up();
  if (ap_up != g_ap_was_up) {
    if (ap_up) {
      chords_abandon();
      hid_keyboard_silence();
    } else {
      buttons_resync();
    }
    g_ap_was_up = ap_up;
  }
  if (fire_switch_poll(millis())) chords_apply_fire_switch();
  if (!ap_up) {
    buttons_poll();
    trace_keys_if_changed();
    chords_poll();
    hid_keyboard_poll();
  }
  poll_fire_cmd(g_uart);
#if ARDUINO_USB_CDC_ON_BOOT
  poll_fire_cmd(g_usb);
#endif
  delay(1);
#if CHORD_MEASURE_LOOP
  if (timing) loop_measure_note(micros() - loop_t0, WiFi.getMode() == WIFI_AP);
#endif
}
