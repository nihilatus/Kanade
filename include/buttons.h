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

// Boot records every raw GPIO edge and every debounced mask change.
// The ring holds this many events (8 bytes each). A full ring drops the oldest.
static constexpr size_t kButtonTraceCap = 512;

enum class ButtonTraceKind : uint8_t { Raw = 0, Stable = 1 };

struct ButtonTraceEvent {
  uint32_t ms;
  uint16_t value;  // Raw: 1 down, 0 up. Stable: buttons_mask().
  ButtonTraceKind kind;
  uint8_t index;  // Raw: ButtonId. Stable: unused.
};

// oldest_index 0 is the oldest event still kept.
size_t button_trace_count();
bool button_trace_at(size_t oldest_index, ButtonTraceEvent *out);
