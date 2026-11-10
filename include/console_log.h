#pragma once

#include <Arduino.h>

// USB CDC (when enabled) and UART0, so a board that is only on the UART
// header still prints the hotspot address.

inline void console_ensure() {
  static bool ready = false;
  if (ready) return;
  Serial0.begin(115200);
#if ARDUINO_USB_CDC_ON_BOOT
  Serial.begin(115200);
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
