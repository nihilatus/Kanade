#pragma once

#include <stdint.h>

#include "output_policy.h"
#include "pin_map.h"

// Behavior-mode switches. Chord keys and other inputs stay in pin_map.h
// and are not rows here. A new mode switch is one enumerator before Count
// and one row in kModeSwitches. GPIO numbers stay in pin_map.h; this table
// names the pin function. Count is how many behavior switches exist.
// No Arduino types: the native test includes this header.

enum class ModeSwitch : uint8_t {
  Wifi = 0,
  Output,
  Fire,
  Count
};

// What a debounced edge does on this boot.
// Live: the new level applies in this process.
// Restart: the caller restarts; the boot level stays until then.
// RestartWhenBle: live while output is USB; restart while output is BLE.
enum class SwitchChange : uint8_t {
  Live = 0,
  Restart,
  RestartWhenBle
};

struct ModeSwitchDesc {
  ModeSwitch id;
  PinFn pin;
  SwitchChange change;
  const char *key;
  const char *open_label;
  const char *closed_label;
};

static constexpr ModeSwitchDesc kModeSwitches[] = {
    {ModeSwitch::Wifi, PinFn::WifiEnable, SwitchChange::RestartWhenBle, "wifi", "off", "on"},
    {ModeSwitch::Output, PinFn::OutputMode, SwitchChange::Restart, "output", "USB", "BLE"},
    {ModeSwitch::Fire, PinFn::FireTiming, SwitchChange::Live, "fire", "release", "first"},
};

static constexpr int kModeSwitchCount =
    static_cast<int>(sizeof(kModeSwitches) / sizeof(kModeSwitches[0]));

constexpr bool mode_switch_rows_follow_enum(int index) {
  return index >= kModeSwitchCount
             ? true
             : (static_cast<int>(kModeSwitches[index].id) == index &&
                mode_switch_rows_follow_enum(index + 1));
}

constexpr bool mode_switch_pins_are_not_chords(int index) {
  return index >= kModeSwitchCount
             ? true
             : (!pin_fn_is_chord(kModeSwitches[index].pin) &&
                mode_switch_pins_are_not_chords(index + 1));
}

constexpr bool mode_switch_pins_differ(int index, int later) {
  return later >= kModeSwitchCount
             ? true
             : (kModeSwitches[index].pin == kModeSwitches[later].pin
                    ? false
                    : mode_switch_pins_differ(index, later + 1));
}

constexpr bool mode_switch_pins_unique(int index) {
  return index >= kModeSwitchCount ? true
                                   : (mode_switch_pins_differ(index, index + 1) &&
                                      mode_switch_pins_unique(index + 1));
}

static_assert(kModeSwitchCount == static_cast<int>(ModeSwitch::Count),
              "one row per mode switch");
static_assert(mode_switch_rows_follow_enum(0), "mode switch rows follow ModeSwitch order");
static_assert(mode_switch_pins_are_not_chords(0), "mode switches are not chord keys");
static_assert(mode_switch_pins_unique(0), "mode switch pins are unique");

// Display only: whether an edge restarts on this boot. The restart itself
// still uses output_switch_requests_restart and wifi_edge_requests_restart.
constexpr bool mode_switch_restarts(SwitchChange change, OutputMode mode) {
  return change == SwitchChange::Restart ||
         (change == SwitchChange::RestartWhenBle && mode == OutputMode::Ble);
}
