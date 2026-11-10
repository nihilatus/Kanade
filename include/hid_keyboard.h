#pragma once

#include <Arduino.h>

// Native USB HID keyboard (6KRO boot-protocol style via Arduino USBHIDKeyboard).
// ASCII uses the JIS layout. Romaji letters match a US keyboard; '[' and ']'
// do not. The PC Japanese IME converts romaji. Henkan, Muhenkan, and Kana
// are raw usages inside the stock descriptor (usages 0–255). Windows maps
// those once its keyboard type is Japanese 106/109. Eisuu (LANG2, 0x91) is
// the macOS 英数 key; Windows often ignores it. The USB country code stays
// unset; it does not select that layout. Product
// presents as a keyboard — not a gamepad.
//
// Recipes are queued. hid_keyboard_poll() sends at most one report when it
// is due, so the key scan in loop() keeps running between keystrokes.
// A busy endpoint leaves that report queued. After the endpoint accepts a
// report, SendReport can still wait up to its 100 ms timeout.

void hid_keyboard_begin();
bool hid_keyboard_ready();
void hid_keyboard_poll();

// Type an ASCII/romaji string (a–z, space, punctuation the JIS layout maps).
// Each character is its own press report, a gap, then release. Keyboard.write()
// would send both reports before a ~10 ms host poll, so a repeat such as "nn"
// can collapse into one key. A byte at 0x80 or above is refused: Keyboard.press()
// treats that range as a modifier or a non-printing key. Queued; does not block.
void hid_type_romaji(const char *text);

// Space-separated recipe from the chord table:
//   "nn" "kome"     each character as a key
//   "Shift+1"       modifiers plus one key, then release
//   "+"             the plus character; "Shift++" is Shift and plus
//   "Backspace"     named HID key (also Space, Enter, Esc, [ESC], arrows)
//   "Henkan"        raw HID usage (Hankaku, Muhenkan, Kana, Delete, F1–F12, …)
// An unrecognized token is skipped and the rest is typed. If skip storage
// overflows, the whole recipe is dropped. hid_print_drops() counts recipes
// not queued, tokens skipped, and sends refused because the host was not ready.
// Not reentrant: the parser buffer is owned by this module. Call only from loop().
// If a previous recipe is still playing, this one is appended after a
// release-all so a modifier cannot leak into the next chord.
void hid_type_recipe(const char *recipe);

// Queued helpers. They join the same timeline as recipes.
void hid_tap_key(uint8_t keycode);
void hid_release_all();

// Totals since boot, on UART0 and USB CDC. `drops` on the serial console.
void hid_print_drops();
