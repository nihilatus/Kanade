#include "chords.h"

#include "buttons.h"
#include "hid_keyboard.h"

// --- Stub chord table (proof only) ---
//
// | Physical combo (matrix cell)           | Romaji typed |
// |----------------------------------------|--------------|
// | Left Center R1C2 (release)             | a            |
// | Left N R0C0 held + Right E R1C5 release| ka           |
// | A R3C4 (release)                       | shi          |
// | Start R5C0 (release)                   | (space)      |
//
// Later: port mapping_table.csv + engine timing from joystick-writer.

static bool left_n_held_at_right_e = false;

void chords_begin() {
  left_n_held_at_right_e = false;
}

void chords_poll() {
  if (!hid_keyboard_ready()) return;

  // Track Left N while waiting for Right E chord.
  if (button_just_pressed(BTN_LEFT_N)) {
    left_n_held_at_right_e = false;
  }

  // Chord: Left N + Right E → "ka"
  if (button_just_released(BTN_RIGHT_E)) {
    if (button_pressed(BTN_LEFT_N)) {
      hid_type_romaji("ka");
      left_n_held_at_right_e = true;
      return;
    }
  }

  // Single: Left Center → "a" (ignore if we just fired a chord involving centers later)
  if (button_just_released(BTN_LEFT_CENTER)) {
    hid_type_romaji("a");
    return;
  }

  // Single: A → "shi" (demo multi-letter token)
  if (button_just_released(BTN_A)) {
    hid_type_romaji("shi");
    return;
  }

  // Start → space (useful when testing in Notepad)
  if (button_just_released(BTN_START)) {
    hid_type_romaji(" ");
    return;
  }

  (void)left_n_held_at_right_e;
}
