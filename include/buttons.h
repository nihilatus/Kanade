#pragma once

#include <Arduino.h>
#include <stdint.h>

// Logical buttons — same product roles as joystick-writer / store pinout,
// but consumed here as a keyboard chord surface (not a gamepad).
// Physical wiring is a diode key matrix (see docs/pinout.md), not 1 GPIO/key.

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

static constexpr uint8_t kMatrixRows = 6;
static constexpr uint8_t kMatrixCols = 6;

// Empty / unused matrix cell
static constexpr int8_t kMatrixEmpty = -1;

struct MatrixKey {
  int8_t button;  // ButtonId, or kMatrixEmpty if spare
  const char *name;
};

extern const int kMatrixRowPins[kMatrixRows];
extern const int kMatrixColPins[kMatrixCols];
extern const MatrixKey kMatrixMap[kMatrixRows][kMatrixCols];

void buttons_begin();
void buttons_poll();  // scan matrix + debounce

bool button_pressed(ButtonId id);   // current debounced level (active → true)
bool button_just_pressed(ButtonId id);
bool button_just_released(ButtonId id);

// Stub helpers: which logical keys are sampled this build
bool button_gpio_enabled(ButtonId id);
