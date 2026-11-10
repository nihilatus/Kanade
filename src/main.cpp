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
};

static void poll_fire_cmd(CmdPort &port) {
  if (port.in == nullptr) return;
  while (port.in->available() > 0) {
    const int raw = port.in->read();
    if (raw < 0) break;
    const char c = static_cast<char>(raw);
    if (c == '\r') continue;
    if (c != '\n') {
      if (port.len + 1 < sizeof(port.buf)) port.buf[port.len++] = c;
      continue;
    }
    port.buf[port.len] = '\0';
    port.len = 0;

    char *arg = port.buf;
    while (*arg == ' ' || *arg == '\t') ++arg;
    if (strncmp(arg, "fire", 4) != 0) continue;
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

static CmdPort g_uart{&Serial0, {}, 0};
#if ARDUINO_USB_CDC_ON_BOOT
static CmdPort g_usb{&Serial, {}, 0};
#endif

void loop() {
  buttons_poll();
  chords_poll();
  hid_keyboard_poll();
  config_server_poll();
  poll_fire_cmd(g_uart);
#if ARDUINO_USB_CDC_ON_BOOT
  poll_fire_cmd(g_usb);
#endif
  delay(1);
}
