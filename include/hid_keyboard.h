#pragma once

#include <Arduino.h>

// Native USB HID keyboard (6KRO boot-protocol style via Arduino USBHIDKeyboard).
// Sends Latin/romaji keystrokes; PC Japanese IME converts to kana/kanji.
// Product presents as a keyboard — not a gamepad.

void hid_keyboard_begin();
bool hid_keyboard_ready();

// Type an ASCII/romaji string (a–z, space, punctuation the stock layout maps).
void hid_type_romaji(const char *text);

// Single HID usage helpers
void hid_tap_key(uint8_t keycode);
void hid_release_all();
