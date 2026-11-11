#pragma once

#include <stdint.h>

#include "settings.h"

// Function → GPIO. The only place input GPIO numbers are written.
// Chord functions are 0 .. kChordKeyCount-1, the same index as ButtonId
// and chord_key_name. Later rows are not chord bits: do not fold them into
// buttons_mask(). A new input (for example a movement pin) is one enumerator
// before Count and one row here; callers ask pin_gpio and do not write the
// GPIO number.

enum class PinFn : uint8_t {
  LL = 0,
  LU,
  LR,
  LD,
  LT0,
  LT1,
  LT2,
  RL,
  RU,
  RR,
  RD,
  RT0,
  RT1,
  RT2,
  WifiEnable,
  OutputMode,
  FireTiming,
  Count
};

struct PinRow {
  PinFn fn;
  int gpio;
};

// Active-low to GND. Chord order matches docs/pinout.md. GPIO3 is skipped.
static constexpr PinRow kPinMap[] = {
    {PinFn::LL, 1},          {PinFn::LU, 2},
    {PinFn::LR, 4},          {PinFn::LD, 5},
    {PinFn::LT0, 6},         {PinFn::LT1, 7},
    {PinFn::LT2, 8},         {PinFn::RL, 9},
    {PinFn::RU, 10},         {PinFn::RR, 11},
    {PinFn::RD, 12},         {PinFn::RT0, 13},
    {PinFn::RT1, 14},        {PinFn::RT2, 21},
    {PinFn::WifiEnable, 15}, {PinFn::OutputMode, 16},
    {PinFn::FireTiming, 17},
};

static constexpr int kPinRowCount = static_cast<int>(sizeof(kPinMap) / sizeof(kPinMap[0]));

constexpr bool pin_fn_is_chord(PinFn fn) {
  return static_cast<uint8_t>(fn) < static_cast<uint8_t>(kChordKeyCount);
}

constexpr int pin_gpio(PinFn fn) {
  return static_cast<uint8_t>(fn) < static_cast<uint8_t>(kPinRowCount)
             ? kPinMap[static_cast<uint8_t>(fn)].gpio
             : -1;
}

constexpr bool pin_rows_follow_enum(int index) {
  return index >= kPinRowCount
             ? true
             : (static_cast<int>(kPinMap[index].fn) == index && pin_rows_follow_enum(index + 1));
}

constexpr bool pin_gpios_differ(int index, int later) {
  return later >= kPinRowCount
             ? true
             : (kPinMap[index].gpio == kPinMap[later].gpio ? false
                                                           : pin_gpios_differ(index, later + 1));
}

constexpr bool pin_gpios_unique(int index) {
  return index >= kPinRowCount ? true
                               : (pin_gpios_differ(index, index + 1) && pin_gpios_unique(index + 1));
}

static_assert(kPinRowCount == static_cast<int>(PinFn::Count), "one row per pin function");
static_assert(static_cast<int>(PinFn::RT2) + 1 == kChordKeyCount,
              "chord functions occupy 0 .. kChordKeyCount-1");
static_assert(pin_rows_follow_enum(0), "pin map rows follow PinFn order");
static_assert(pin_gpios_unique(0), "gpio numbers are unique");
static_assert(!pin_fn_is_chord(PinFn::WifiEnable), "wifi switch is not a chord key");
static_assert(!pin_fn_is_chord(PinFn::OutputMode), "output switch is not a chord key");
static_assert(!pin_fn_is_chord(PinFn::FireTiming), "fire switch is not a chord key");
static_assert(pin_gpio(PinFn::WifiEnable) != pin_gpio(PinFn::OutputMode),
              "output switch is not the wifi switch");
static_assert(pin_gpio(PinFn::FireTiming) != pin_gpio(PinFn::WifiEnable),
              "fire switch is not the wifi switch");
static_assert(pin_gpio(PinFn::FireTiming) != pin_gpio(PinFn::OutputMode),
              "fire switch is not the output switch");
