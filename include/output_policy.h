#pragma once

#include <stdint.h>

// Which keyboard transport this boot is. The GPIO is the record.
// Open (not closed to GND) is USB. Closed to GND is BLE.
// No Arduino types: the native test includes this header.

enum class OutputMode : uint8_t { Usb, Ble };

constexpr OutputMode output_mode_from_closed(bool closed) {
  return closed ? OutputMode::Ble : OutputMode::Usb;
}

// USB HID always starts in USB mode, including when the wifi switch is closed.
// The AP silences sends until it stops. BLE starts only when this boot is BLE
// and the wifi switch is open. A closed switch holds the radio for the AP,
// even when the AP itself fails to start.
constexpr bool output_starts_usb(OutputMode mode) { return mode == OutputMode::Usb; }

constexpr bool output_starts_ble(OutputMode mode, bool wifi_closed) {
  return mode == OutputMode::Ble && !wifi_closed;
}

// The output switch is latched at boot. A later debounced level restarts
// instead of hot-swapping USB and BLE in one process.
constexpr bool output_switch_requests_restart(bool boot_closed, bool stable_closed) {
  return boot_closed != stable_closed;
}

// USB mode keeps the live wifi switch: open and close the AP without restarting.
// BLE mode does not share the radio. A debounced wifi edge restarts, and the
// caller must not start or stop Wi-Fi in this process. The boot level itself
// is not an edge: wifi already closed at boot only serves the AP.
constexpr bool wifi_edge_requests_restart(OutputMode mode, bool boot_wifi_closed,
                                          bool stable_wifi_closed) {
  return mode == OutputMode::Ble && boot_wifi_closed != stable_wifi_closed;
}
