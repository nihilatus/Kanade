#pragma once

#include <Arduino.h>
#include <stdint.h>

#include "pin_map.h"

// 14 discrete chord keys. Each switch sits between its GPIO and GND.
// Firmware uses INPUT_PULLUP (released = HIGH, pressed = LOW).
// Bit 0..13 of buttons_mask() follow this enum order, which is also the
// canonical order in data/chord_map.machine.csv.

// Same values as PinFn for the chord keys. Not a second pin list.
enum ButtonId : uint8_t {
  BTN_LL = static_cast<uint8_t>(PinFn::LL),
  BTN_LU = static_cast<uint8_t>(PinFn::LU),
  BTN_LR = static_cast<uint8_t>(PinFn::LR),
  BTN_LD = static_cast<uint8_t>(PinFn::LD),
  BTN_LT0 = static_cast<uint8_t>(PinFn::LT0),
  BTN_LT1 = static_cast<uint8_t>(PinFn::LT1),
  BTN_LT2 = static_cast<uint8_t>(PinFn::LT2),
  BTN_RL = static_cast<uint8_t>(PinFn::RL),
  BTN_RU = static_cast<uint8_t>(PinFn::RU),
  BTN_RR = static_cast<uint8_t>(PinFn::RR),
  BTN_RD = static_cast<uint8_t>(PinFn::RD),
  BTN_RT0 = static_cast<uint8_t>(PinFn::RT0),
  BTN_RT1 = static_cast<uint8_t>(PinFn::RT1),
  BTN_RT2 = static_cast<uint8_t>(PinFn::RT2),
  BTN_COUNT = static_cast<uint8_t>(kChordKeyCount)
};

static constexpr uint8_t kButtonCount = BTN_COUNT;

struct ButtonDef {
  int pin;
  const char *name;
};

// Filled from pin_map and chord_key_name before main. Callers only read it.
extern const ButtonDef *const kButtons;

void buttons_begin();
void buttons_poll();  // sample GPIOs + per-key debounce

// Sample each GPIO and treat that level as already stable. Clears edges.
// Does not write a trace event. buttons_begin samples after pull-ups settle.
// Also call when the AP goes off, before the next buttons_poll, so presses
// and releases during the AP are not a chord; and after boot/AP wait-for-up
// before arming chord input.
void buttons_resync();

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
