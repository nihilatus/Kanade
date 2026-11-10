#pragma once

#include <Arduino.h>

// Native USB HID keyboard (6KRO boot-protocol style via Arduino USBHIDKeyboard).
// Sends Latin/romaji keystrokes and a few named keys; PC Japanese IME
// converts romaji. Product presents as a keyboard — not a gamepad.
//
// Recipes are queued. hid_keyboard_poll() sends at most one report when it
// is due, so the key scan in loop() keeps running between keystrokes.

void hid_keyboard_begin();
bool hid_keyboard_ready();
void hid_keyboard_poll();

// Type an ASCII/romaji string (a–z, space, punctuation the stock layout maps).
// Each character is its own press report, a gap, then release. Keyboard.write()
// would send both reports before a ~10 ms host poll, so a repeat such as "nn"
// can collapse into one key. Queued; does not block the caller.
void hid_type_romaji(const char *text);

// Space-separated recipe from the chord table:
//   "nn" "kome"     each character as a key
//   "Shift+1"       modifiers plus one key, then release
//   "+"             the plus character; "Shift++" is Shift and plus
//   "Backspace"     named HID key (also Space, Enter, Esc, [ESC], arrows)
//   "Henkan"        raw HID usage (Hankaku, Muhenkan, Kana, Delete, F1–F12, …)
// An unrecognized token is logged and skipped. Dropped skips abort the recipe.
// Not reentrant: the parser buffer is owned by this module. Call only from loop().
// If a previous recipe is still playing, this one is appended after a
// release-all so a modifier cannot leak into the next chord.
void hid_type_recipe(const char *recipe);

// Queued helpers. They join the same timeline as recipes.
void hid_tap_key(uint8_t keycode);
void hid_release_all();
