#include "buttons.h"

// 6×6 diode key matrix on safe ESP32-S3 N16R8 GPIOs.
// Rows driven LOW one-at-a-time; columns INPUT_PULLUP.
// Diode: switch → anode, cathode toward the driven ROW (anti-ghosting).
// See docs/pinout.md for cell map and BOM.

const int kMatrixRowPins[kMatrixRows] = {1, 2, 3, 4, 5, 6};
const int kMatrixColPins[kMatrixCols] = {7, 8, 9, 10, 11, 12};

// Row × Col → logical button. Four corner spares left empty.
const MatrixKey kMatrixMap[kMatrixRows][kMatrixCols] = {
  // COL0              COL1              COL2                COL3               COL4               COL5
  {{BTN_LEFT_N, "LN"}, {BTN_LEFT_NE, "LNE"}, {BTN_LEFT_E, "LE"}, {BTN_LEFT_SE, "LSE"}, {BTN_LEFT_S, "LS"}, {BTN_LEFT_SW, "LSW"}},
  {{BTN_LEFT_W, "LW"}, {BTN_LEFT_NW, "LNW"}, {BTN_LEFT_CENTER, "LC"}, {BTN_RIGHT_N, "RN"}, {BTN_RIGHT_NE, "RNE"}, {BTN_RIGHT_E, "RE"}},
  {{BTN_RIGHT_SE, "RSE"}, {BTN_RIGHT_S, "RS"}, {BTN_RIGHT_SW, "RSW"}, {BTN_RIGHT_W, "RW"}, {BTN_RIGHT_NW, "RNW"}, {BTN_RIGHT_CENTER, "RC"}},
  {{BTN_DPAD_UP, "Up"}, {BTN_DPAD_DOWN, "Dn"}, {BTN_DPAD_LEFT, "Lf"}, {BTN_DPAD_RIGHT, "Rt"}, {BTN_A, "A"}, {BTN_B, "B"}},
  {{BTN_X, "X"}, {BTN_Y, "Y"}, {BTN_L1, "L1"}, {BTN_L2, "L2"}, {BTN_R1, "R1"}, {BTN_R2, "R2"}},
  {{BTN_START, "Start"}, {BTN_SELECT, "Select"}, {kMatrixEmpty, "-"}, {kMatrixEmpty, "-"}, {kMatrixEmpty, "-"}, {kMatrixEmpty, "-"}},
};

// --- Stub mask: enable only a few keys for first flash / HID proof ---
// Wire those matrix cells to prove chord → romaji. Expand later.
static bool stub_enabled(ButtonId id) {
  switch (id) {
    case BTN_LEFT_CENTER:   // R1C2 → "a"
    case BTN_LEFT_N:        // R0C0 → with Right E → "ka"
    case BTN_RIGHT_E:       // R1C5
    case BTN_A:             // R3C4 → "shi" demo / fallback
    case BTN_START:         // R5C0 → space
      return true;
    default:
      return false;
  }
}

static constexpr uint8_t kDebounceMs = 8;
static constexpr uint8_t kSettleUs = 5;  // row-drive settle before col read

static bool raw_down[BTN_COUNT];
static bool stable_down[BTN_COUNT];
static bool prev_down[BTN_COUNT];
static bool edge_press[BTN_COUNT];
static bool edge_release[BTN_COUNT];
static uint32_t last_change_ms[BTN_COUNT];

bool button_gpio_enabled(ButtonId id) {
  if (id >= BTN_COUNT) return false;
  return stub_enabled(id);
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
  }

  // Columns: idle HIGH via pull-up; go LOW when a switch closes onto a driven row.
  for (uint8_t c = 0; c < kMatrixCols; ++c) {
    pinMode(kMatrixColPins[c], INPUT_PULLUP);
  }

  // Rows: idle as inputs (Hi-Z) so no accidental drive contention; scan sets OUTPUT LOW.
  for (uint8_t r = 0; r < kMatrixRows; ++r) {
    pinMode(kMatrixRowPins[r], INPUT);
  }
}

void buttons_poll() {
  const uint32_t now = millis();

  for (uint8_t i = 0; i < BTN_COUNT; ++i) {
    edge_press[i] = false;
    edge_release[i] = false;
  }

  // Fresh raw sample for every mapped cell this frame.
  bool sample[BTN_COUNT];
  for (uint8_t i = 0; i < BTN_COUNT; ++i) {
    sample[i] = false;
  }

  for (uint8_t r = 0; r < kMatrixRows; ++r) {
    const int row_pin = kMatrixRowPins[r];
    pinMode(row_pin, OUTPUT);
    digitalWrite(row_pin, LOW);
    delayMicroseconds(kSettleUs);

    for (uint8_t c = 0; c < kMatrixCols; ++c) {
      const MatrixKey &cell = kMatrixMap[r][c];
      if (cell.button < 0) continue;

      const ButtonId id = static_cast<ButtonId>(cell.button);
      if (!button_gpio_enabled(id)) continue;

      // Active-low on column when switch + diode conduct to this row.
      if (digitalRead(kMatrixColPins[c]) == LOW) {
        sample[id] = true;
      }
    }

    // Release row before next drive (avoid two rows low at once).
    pinMode(row_pin, INPUT);
  }

  for (uint8_t i = 0; i < BTN_COUNT; ++i) {
    if (!button_gpio_enabled(static_cast<ButtonId>(i))) {
      continue;
    }

    const bool down = sample[i];
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
