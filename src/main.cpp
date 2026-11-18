#include <Arduino.h>

#include <stdio.h>

#include <esp_system.h>

#include "buttons.h"
#include "chord_timing.h"
#include "chord_timing_store.h"
#include "chords.h"
#include "config_server.h"
#include "console_commands.h"
#include "console_log.h"
#include "fire_switch.h"
#include "hid_keyboard.h"
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
  chord_timing_store_load();
#if ARDUINO_USB_CDC_ON_BOOT
  delay(200);
#endif
  console_println("");
  console_println("Kanade");
  char line[192];
  snprintf(line, sizeof(line), "14 direct keys, active-low, %u ms debounce",
           static_cast<unsigned>(chord_timing_current().debounce_ms));
  console_println(line);
  size_t used = 0;
  int wrote = snprintf(line, sizeof(line), "pins");
  if (wrote > 0) used = static_cast<size_t>(wrote);
  for (uint8_t i = 0; i < BTN_COUNT && used < sizeof(line); ++i) {
    wrote = snprintf(line + used, sizeof(line) - used, " %s=GPIO%d", kButtons[i].name,
                     kButtons[i].pin);
    if (wrote < 0 || static_cast<size_t>(wrote) >= sizeof(line) - used) break;
    used += static_cast<size_t>(wrote);
  }
  console_println(line);

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

static void restart_for_switch() {
  const bool output = output_switch_restart_requested();
  const bool wifi = config_server_restart_requested();
  if (!output && !wifi) return;
  hid_keyboard_silence();
  console_println(output ? "output switch restart" : "wifi switch restart");
  delay(50);
  esp_restart();
}

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

// False until every chord key has been debounced up after boot or AP-off.
// Held keys must not enter the chord engine as a fresh chord.
static bool g_input_armed = false;

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
      g_input_armed = false;
    } else {
      buttons_resync();
    }
    g_ap_was_up = ap_up;
  }
  if (fire_switch_poll(millis())) chords_apply_fire_switch();
  if (!ap_up) {
    buttons_poll();
    console_commands_trace_keys();
    if (!g_input_armed && buttons_mask() == 0) {
      buttons_resync();
      g_input_armed = true;
    }
    if (g_input_armed) {
      chords_poll();
      hid_keyboard_poll();
    }
  }
  console_commands_poll();
  delay(1);
#if CHORD_MEASURE_LOOP
  if (timing) loop_measure_note(micros() - loop_t0, WiFi.getMode() == WIFI_AP);
#endif
}
