#include "output_switch.h"

#include <stdio.h>

#include <driver/gpio.h>
#include <esp_rom_sys.h>

#include <Arduino.h>

#include "console_log.h"
#include "pin_map.h"
#include "settings.h"

// The pull-up has just been enabled. Wait before the level is trusted.
static constexpr uint32_t kOutputSwitchSettleUs = 100;

static bool g_boot_closed = false;
static bool g_raw = false;
static bool g_stable = false;
static uint32_t g_change_ms = 0;

static bool closed_raw() { return digitalRead(pin_gpio(PinFn::OutputMode)) == LOW; }

bool output_switch_boot_closed() {
  static bool sampled = false;
  static bool closed = false;
  if (sampled) return closed;
  const int pin = pin_gpio(PinFn::OutputMode);
  gpio_config_t cfg = {};
  cfg.pin_bit_mask = 1ULL << pin;
  cfg.mode = GPIO_MODE_INPUT;
  cfg.pull_up_en = GPIO_PULLUP_ENABLE;
  cfg.pull_down_en = GPIO_PULLDOWN_DISABLE;
  cfg.intr_type = GPIO_INTR_DISABLE;
  if (gpio_config(&cfg) == ESP_OK) {
    esp_rom_delay_us(kOutputSwitchSettleUs);
    closed = gpio_get_level(static_cast<gpio_num_t>(pin)) == 0;
  }
  sampled = true;
  return closed;
}

void output_switch_begin() {
  pinMode(pin_gpio(PinFn::OutputMode), INPUT_PULLUP);
  g_boot_closed = output_switch_boot_closed();
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
  if ((now_ms - g_change_ms) >= kSwitchDebounceMs && g_stable != g_raw) {
    g_stable = g_raw;
  }
}

bool output_switch_restart_requested() {
  return output_switch_requests_restart(g_boot_closed, g_stable);
}
