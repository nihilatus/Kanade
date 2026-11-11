#pragma once

#include <stdint.h>

#include "output_policy.h"

// USB or BLE keyboard reports. The recipe queue stays in hid_keyboard.cpp.
// USB uses Arduino USBHIDKeyboard. BLE uses the built-in NimBLE HID service
// and does not call Keyboard.begin(), so the OTG port is not a keyboard.
// Bonds stay in the BLE stack's own NVS. This module does not write the
// output mode and does not delete bonds.

// False when BLE init or advertising fails. USB begin returns true.
bool hid_link_begin(OutputMode mode);
bool hid_link_ready();
// USB: the HID endpoint can take another report. BLE: the host is subscribed.
bool hid_link_can_send();
// BLE disconnect or lost subscription. One-shot. USB always returns false.
bool hid_link_take_drop();
void hid_link_press(uint8_t code);
void hid_link_press_raw(uint8_t code);
void hid_link_release_all();
