#include <Arduino.h>

#include <stdio.h>
#include <string.h>

#include "buttons.h"
#include "chord_core.h"
#include "chords.h"
#include "config_server.h"
#include "console_log.h"
#include "hid_keyboard.h"

// ESP32-S3-DevKitC-1 N16R8 — USB HID chord keyboard.
// Plug the PC into the board port labeled USB (OTG), not UART.
// Each switch: GPIO to one side, GND to the other. Pressed = LOW.

void setup() {
  console_ensure();
#if ARDUINO_USB_CDC_ON_BOOT
  delay(200);
  Serial.println();
  Serial.println(F("esp32-jis-keyboard"));
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
  hid_keyboard_begin();
  chords_begin();
  config_server_begin();
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
    if (!chords_set_fire_mode_name(arg)) console_println("fire: release 或 first");
  }
}

static CmdPort g_uart{&Serial0, {}, 0, false};
#if ARDUINO_USB_CDC_ON_BOOT
static CmdPort g_usb{&Serial, {}, 0, false};
#endif

void loop() {
  buttons_poll();
  trace_keys_if_changed();
  chords_poll();
  hid_keyboard_poll();
  config_server_poll();
  poll_fire_cmd(g_uart);
#if ARDUINO_USB_CDC_ON_BOOT
  poll_fire_cmd(g_usb);
#endif
  delay(1);
}
