#pragma once

#include <Arduino.h>
#include <stdint.h>

// 14 discrete chord keys. Each switch sits between its GPIO and GND.
// Firmware uses INPUT_PULLUP (released = HIGH, pressed = LOW).
// Bit 0..13 of buttons_mask() follow this enum order, which is also the
// canonical order in data/chord_map.machine.csv.

enum ButtonId : uint8_t {
  BTN_LL = 0,
  BTN_LU,
  BTN_LR,
  BTN_LD,
  BTN_LT0,
  BTN_LT1,
  BTN_LT2,
  BTN_RL,
  BTN_RU,
  BTN_RR,
  BTN_RD,
  BTN_RT0,
  BTN_RT1,
  BTN_RT2,
  BTN_COUNT
};

static constexpr uint8_t kButtonCount = BTN_COUNT;

struct ButtonDef {
  int pin;
  const char *name;
};

extern const ButtonDef kButtons[BTN_COUNT];

void buttons_begin();
void buttons_poll();  // sample GPIOs + per-key debounce

bool button_pressed(ButtonId id);  // debounced level (pressed → true)
bool button_just_pressed(ButtonId id);
bool button_just_released(ButtonId id);

// Bit i set when ButtonId i is debounced down. Chord engine reads this only.
uint16_t buttons_mask();
