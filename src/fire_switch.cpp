#include "fire_switch.h"

#include <stdio.h>

#include <driver/gpio.h>
#include <esp_rom_sys.h>

#include <Arduino.h>

#include "console_log.h"
#include "pin_map.h"
#include "settings.h"

// The pull-up has just been enabled. Wait before the level is trusted.
static constexpr uint32_t kFireSwitchSettleUs = 100;

static bool g_ready = false;
static bool g_raw = false;
static bool g_stable = false;
static uint32_t g_change_ms = 0;

static bool closed_raw() { return digitalRead(pin_gpio(PinFn::FireTiming)) == LOW; }

void fire_switch_begin() {
  const int pin = pin_gpio(PinFn::FireTiming);
  gpio_config_t cfg = {};
  cfg.pin_bit_mask = 1ULL << pin;
  cfg.mode = GPIO_MODE_INPUT;
  cfg.pull_up_en = GPIO_PULLUP_ENABLE;
  cfg.pull_down_en = GPIO_PULLDOWN_DISABLE;
  cfg.intr_type = GPIO_INTR_DISABLE;
  bool closed = false;
  if (gpio_config(&cfg) == ESP_OK) {
    esp_rom_delay_us(kFireSwitchSettleUs);
    closed = gpio_get_level(static_cast<gpio_num_t>(pin)) == 0;
  }
  pinMode(pin, INPUT_PULLUP);
  g_raw = closed;
  g_stable = closed;
  g_change_ms = millis();
  g_ready = true;
  char line[80];
  snprintf(line, sizeof(line),
           "Fire switch GPIO%d, open is release, closed to GND is first", pin);
  console_println(line);
  console_println(closed ? "fire first" : "fire release");
}

ChordFireMode fire_switch_mode() { return fire_mode_from_closed(g_stable); }

bool fire_switch_poll(uint32_t now_ms) {
  if (!g_ready) return false;
  const bool closed = closed_raw();
  if (closed != g_raw) {
    g_raw = closed;
    g_change_ms = now_ms;
  }
  if ((now_ms - g_change_ms) >= kSwitchDebounceMs && g_stable != g_raw) {
    g_stable = g_raw;
    return true;
  }
  return false;
}
