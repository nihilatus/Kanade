#include "buttons.h"

#include "chord_core.h"

static_assert(BTN_COUNT == kChordKeyCount, "button bits match chord_core");

// One GPIO per key, active-low, internal pull-up.
// No matrix and no diodes: simultaneous chords cannot ghost.
// See docs/pinout.md. GPIO3 is skipped (strapping).

const ButtonDef kButtons[BTN_COUNT] = {
    {1, chord_key_name(0)},  {2, chord_key_name(1)},  {4, chord_key_name(2)},
    {5, chord_key_name(3)},  {6, chord_key_name(4)},  {7, chord_key_name(5)},
    {8, chord_key_name(6)},  {9, chord_key_name(7)},  {10, chord_key_name(8)},
    {11, chord_key_name(9)}, {12, chord_key_name(10)}, {13, chord_key_name(11)},
    {14, chord_key_name(12)}, {21, chord_key_name(13)},
};

// Both edges. Cheap tactiles usually finish bouncing inside 10 ms.
// kChordDebounceMs is a trial: the chord engine waits again after this mask
// is empty, so a longer debounce mostly adds release latency. Raise
// kChordDebounceMs in chord_core.h if a noisy edge splits a chord or repeats a key.

static bool raw_down[BTN_COUNT];
static bool stable_down[BTN_COUNT];
static bool prev_down[BTN_COUNT];
static bool edge_press[BTN_COUNT];
static bool edge_release[BTN_COUNT];
static uint32_t last_change_ms[BTN_COUNT];

void buttons_begin() {
  const uint32_t now = millis();
  for (uint8_t i = 0; i < BTN_COUNT; ++i) {
    raw_down[i] = false;
    stable_down[i] = false;
    prev_down[i] = false;
    edge_press[i] = false;
    edge_release[i] = false;
    last_change_ms[i] = now;
    pinMode(kButtons[i].pin, INPUT_PULLUP);
  }
}

void buttons_poll() {
  const uint32_t now = millis();

  for (uint8_t i = 0; i < BTN_COUNT; ++i) {
    edge_press[i] = false;
    edge_release[i] = false;

    const bool down = digitalRead(kButtons[i].pin) == LOW;
    if (down != raw_down[i]) {
      raw_down[i] = down;
      last_change_ms[i] = now;
    }

    if ((now - last_change_ms[i]) >= kChordDebounceMs && stable_down[i] != raw_down[i]) {
      stable_down[i] = raw_down[i];
    }

    if (stable_down[i] && !prev_down[i]) edge_press[i] = true;
    if (!stable_down[i] && prev_down[i]) edge_release[i] = true;
    prev_down[i] = stable_down[i];
  }
}

bool button_pressed(ButtonId id) {
  return id < BTN_COUNT && stable_down[id];
}

bool button_just_pressed(ButtonId id) {
  return id < BTN_COUNT && edge_press[id];
}

bool button_just_released(ButtonId id) {
  return id < BTN_COUNT && edge_release[id];
}

uint16_t buttons_mask() {
  uint16_t mask = 0;
  for (uint8_t i = 0; i < BTN_COUNT; ++i) {
    if (stable_down[i]) mask |= static_cast<uint16_t>(1u << i);
  }
  return mask;
}
