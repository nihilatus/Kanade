#include "buttons.h"

// One GPIO per key, active-low, internal pull-up.
// No matrix and no diodes: simultaneous chords cannot ghost.
// See docs/pinout.md. GPIO3 is skipped (strapping).

const ButtonDef kButtons[BTN_COUNT] = {
    {1, "LL"},  {2, "LU"},  {4, "LR"},  {5, "LD"},  {6, "LT0"},
    {7, "LT1"}, {8, "LT2"}, {9, "RL"},  {10, "RU"}, {11, "RR"},
    {12, "RD"}, {13, "RT0"}, {14, "RT1"}, {21, "RT2"},
};

// Both edges. Cheap tactiles usually finish bouncing inside 10 ms;
// 15 ms keeps a short chord without treating a noisy edge as a new press.
static constexpr uint8_t kDebounceMs = 15;

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

    if ((now - last_change_ms[i]) >= kDebounceMs && stable_down[i] != raw_down[i]) {
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
