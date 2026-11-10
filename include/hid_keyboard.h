#pragma once

#include <Arduino.h>

// Native USB HID keyboard (6KRO boot-protocol style via Arduino USBHIDKeyboard).
// Sends Latin/romaji keystrokes and a few named keys; PC Japanese IME
// converts romaji. Product presents as a keyboard — not a gamepad.

void hid_keyboard_begin();
bool hid_keyboard_ready();

// Type an ASCII/romaji string (a–z, space, punctuation the stock layout maps).
void hid_type_romaji(const char *text);

// Space-separated recipe from the chord table:
//   "nn" "kome"     each character as a key
//   "Shift+1"       modifiers plus one key, then release
//   "Backspace"     named HID key (also Space, Enter, Esc, [ESC], arrows)
// An unrecognized token is logged and skipped.
void hid_type_recipe(const char *recipe);

// Single HID usage helpers
void hid_tap_key(uint8_t keycode);
void hid_release_all();
