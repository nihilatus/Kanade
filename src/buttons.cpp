#include "buttons.h"

// Pin map adapted from Project store docs/esp32-hid-pinout.md (§2.1).
// Leave GPIO19/20 alone (USB D−/D+). GPIO35 used for R3 with N16R8 caveat.
//
// Stub bring-up: only a few GPIOs are enabled (gpio >= 0). Set gpio = -1 to
// skip a key until wired. Enable more rows as you solder.

static constexpr int8_t kStubOff = -1;

const ButtonPin kButtonPins[BTN_COUNT] = {
  {BTN_DPAD_UP,       1,  "DpadUp",      false},
  {BTN_DPAD_DOWN,     2,  "DpadDown",    false},
  {BTN_DPAD_LEFT,     42, "DpadLeft",    false},
  {BTN_DPAD_RIGHT,    41, "DpadRight",   false},
  {BTN_A,             40, "A",           false},
  {BTN_B,             39, "B",           false},
  {BTN_X,             47, "X",           false},
  {BTN_Y,             21, "Y",           false},
  {BTN_L1,            4,  "L1",          false},
  {BTN_R1,            5,  "R1",          false},
  {BTN_L2,            6,  "L2",          false},
  {BTN_R2,            7,  "R2",          false},
  {BTN_START,         15, "Start",       false},
  {BTN_SELECT,        16, "Select",      false},
  {BTN_LEFT_N,        17, "LeftN",       false},
  {BTN_LEFT_NE,       18, "LeftNE",      false},
  {BTN_LEFT_E,        8,  "LeftE",       false},
  {BTN_LEFT_SE,       3,  "LeftSE",      false},
  {BTN_LEFT_S,        9,  "LeftS",       false},
  {BTN_LEFT_SW,       10, "LeftSW",      false},
  {BTN_LEFT_W,        11, "LeftW",       false},
  {BTN_LEFT_NW,       12, "LeftNW",      false},
  {BTN_LEFT_CENTER,   13, "LeftCenter",  false},
  {BTN_RIGHT_N,       14, "RightN",      false},
  {BTN_RIGHT_NE,      48, "RightNE",     false},
  {BTN_RIGHT_E,       38, "RightE",      false},  // RGB LED on DevKitC-1 v1.1
  {BTN_RIGHT_SE,      45, "RightSE",     true},   // strapping
  {BTN_RIGHT_S,       0,  "RightS",      false},  // BOOT — don't hold at reset
  {BTN_RIGHT_SW,      46, "RightSW",     true},   // strapping
  {BTN_RIGHT_W,       43, "RightW",      false},  // UART0 TX
  {BTN_RIGHT_NW,      44, "RightNW",     false},  // UART0 RX
  {BTN_RIGHT_CENTER,  35, "RightCenter", false},  // N16R8 PSRAM line — bench-test
};

// --- Stub mask: enable only a few keys for first flash / HID proof ---
// Wire GND + these GPIOs to prove chord → romaji. Expand later.
static bool stub_enabled(ButtonId id) {
  switch (id) {
    case BTN_LEFT_CENTER:   // GPIO13 → "a"
    case BTN_LEFT_N:        // GPIO17 → with Right E → "ka"
    case BTN_RIGHT_E:       // GPIO38
    case BTN_A:             // GPIO40 → "shi" demo / fallback
    case BTN_START:         // GPIO15 → space
      return true;
    default:
      return false;
  }
}

static constexpr uint8_t kDebounceMs = 8;

static bool raw_down[BTN_COUNT];
static bool stable_down[BTN_COUNT];
static bool prev_down[BTN_COUNT];
static bool edge_press[BTN_COUNT];
static bool edge_release[BTN_COUNT];
static uint32_t last_change_ms[BTN_COUNT];

bool button_gpio_enabled(ButtonId id) {
  if (id >= BTN_COUNT) return false;
  return stub_enabled(id) && kButtonPins[id].gpio >= 0;
}

void buttons_begin() {
  const uint32_t now = millis();
  for (uint8_t i = 0; i < BTN_COUNT; ++i) {
    raw_down[i] = false;
    stable_down[i] = false;
    prev_down[i] = false;
    edge_press[i] = false;
    edge_release[i] = false;
    last_change_ms[i] = now;

    if (!button_gpio_enabled(static_cast<ButtonId>(i))) {
      continue;
    }

    const ButtonPin &p = kButtonPins[i];
    pinMode(p.gpio, INPUT);  // configure first
    if (!p.pullup_after_boot) {
      pinMode(p.gpio, INPUT_PULLUP);
    }
  }

  // Strapping pins: enable internal pull-up only after boot (GPIO45/46).
  delay(2);
  for (uint8_t i = 0; i < BTN_COUNT; ++i) {
    if (!button_gpio_enabled(static_cast<ButtonId>(i))) continue;
    const ButtonPin &p = kButtonPins[i];
    if (p.pullup_after_boot) {
      pinMode(p.gpio, INPUT_PULLUP);
    }
  }
}

void buttons_poll() {
  const uint32_t now = millis();
  for (uint8_t i = 0; i < BTN_COUNT; ++i) {
    edge_press[i] = false;
    edge_release[i] = false;

    if (!button_gpio_enabled(static_cast<ButtonId>(i))) {
      continue;
    }

    const bool down = digitalRead(kButtonPins[i].gpio) == LOW;
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
