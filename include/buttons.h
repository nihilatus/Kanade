#pragma once

#include <Arduino.h>
#include <stdint.h>

// Logical buttons — same product roles as joystick-writer / store pinout,
// but consumed here as a keyboard chord surface (not a gamepad).

enum ButtonId : uint8_t {
  BTN_DPAD_UP = 0,
  BTN_DPAD_DOWN,
  BTN_DPAD_LEFT,
  BTN_DPAD_RIGHT,
  BTN_A,
  BTN_B,
  BTN_X,
  BTN_Y,
  BTN_L1,
  BTN_R1,
  BTN_L2,
  BTN_R2,
  BTN_START,
  BTN_SELECT,
  BTN_LEFT_N,
  BTN_LEFT_NE,
  BTN_LEFT_E,
  BTN_LEFT_SE,
  BTN_LEFT_S,
  BTN_LEFT_SW,
  BTN_LEFT_W,
  BTN_LEFT_NW,
  BTN_LEFT_CENTER,   // L3-style physical center
  BTN_RIGHT_N,
  BTN_RIGHT_NE,
  BTN_RIGHT_E,
  BTN_RIGHT_SE,
  BTN_RIGHT_S,
  BTN_RIGHT_SW,
  BTN_RIGHT_W,
  BTN_RIGHT_NW,
  BTN_RIGHT_CENTER,  // R3-style physical center
  BTN_COUNT
};

struct ButtonPin {
  ButtonId id;
  int8_t gpio;           // -1 = not wired / stub-disabled
  const char *name;
  bool pullup_after_boot; // GPIO45/46: enable pull-up only after boot
};

extern const ButtonPin kButtonPins[BTN_COUNT];

void buttons_begin();
void buttons_poll();  // debounce + sample

bool button_pressed(ButtonId id);   // current debounced level (active-low → true)
bool button_just_pressed(ButtonId id);
bool button_just_released(ButtonId id);

// Stub helpers: which GPIOs are actually sampled this build
bool button_gpio_enabled(ButtonId id);
