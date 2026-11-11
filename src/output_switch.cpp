#include "output_switch.h"

#include <stdio.h>

#include <Arduino.h>

#include "console_log.h"
#include "pin_map.h"
#include "settings.h"

static bool g_boot_closed = false;
static bool g_raw = false;
static bool g_stable = false;
static uint32_t g_change_ms = 0;

static bool closed_raw() { return digitalRead(pin_gpio(PinFn::OutputMode)) == LOW; }

void output_switch_begin() {
  pinMode(pin_gpio(PinFn::OutputMode), INPUT_PULLUP);
  g_boot_closed = closed_raw();
  g_raw = g_boot_closed;
  g_stable = g_boot_closed;
  g_change_ms = millis();
  char line[72];
  snprintf(line, sizeof(line), "Output switch GPIO%d, open is USB, closed to GND is BLE",
           pin_gpio(PinFn::OutputMode));
  console_println(line);
  console_println(g_boot_closed ? "output BLE" : "output USB");
}

OutputMode output_switch_mode() { return output_mode_from_closed(g_boot_closed); }

void output_switch_poll(uint32_t now_ms) {
  const bool closed = closed_raw();
  if (closed != g_raw) {
    g_raw = closed;
    g_change_ms = now_ms;
  }
  if ((now_ms - g_change_ms) >= kWifiSwitchDebounceMs && g_stable != g_raw) {
    g_stable = g_raw;
  }
}

bool output_switch_restart_requested() {
  return output_switch_requests_restart(g_boot_closed, g_stable);
}
