#pragma once

#include <Arduino.h>

// USB CDC (when enabled) and UART0, so a board that is only on the UART
// header still prints the hotspot address.
//
// CDC TX timeout is 0 so a full TinyUSB FIFO cannot busy-wait the scan loop
// (default is 250 ms). A line may be truncated or dropped on CDC; UART0 still
// prints the whole line.

inline void console_ensure() {
  static bool ready = false;
  if (ready) return;
  Serial0.begin(115200);
#if ARDUINO_USB_CDC_ON_BOOT
  Serial.begin(115200);
  Serial.setTxTimeoutMs(0);
#endif
  ready = true;
}

inline void console_println(const char *msg) {
  console_ensure();
  Serial0.println(msg);
#if ARDUINO_USB_CDC_ON_BOOT
  Serial.println(msg);
#endif
}
