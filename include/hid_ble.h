#pragma once

#include <stdint.h>

// BLE HID keyboard. Call only for a BLE boot whose config AP is down.
// Just Works bonding (no display). Keys are stored by NimBLE, not by this file.
// Ready means a host is connected and has subscribed to the input report.
// A disconnect or an unsubscription asks hid_keyboard to drop the queue and
// release modifiers. Reports typed while not ready are not replayed.
// On NimBLE, can_send is false while a notification is still in flight.
// Press and release return false only when the stack did not take the report.
// hid_keyboard leaves that report at the head of the queue and retries.
// True means the report was accepted, or this event has nothing to send.

bool hid_ble_begin();
bool hid_ble_ready();
bool hid_ble_can_send();
bool hid_ble_take_drop();
bool hid_ble_press(uint8_t code);
bool hid_ble_press_raw(uint8_t code);
bool hid_ble_release_all();
