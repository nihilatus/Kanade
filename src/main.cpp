#include <Arduino.h>

#include "buttons.h"
#include "chords.h"
#include "hid_keyboard.h"

// ESP32-S3-DevKitC-1 N16R8 — native USB HID keyboard scaffold.
// Plug the PC into the board port labeled USB (OTG), not UART.

void setup() {
#if ARDUINO_USB_CDC_ON_BOOT
  Serial.begin(115200);
  delay(200);
  Serial.println();
  Serial.println(F("esp32-jis-keyboard scaffold"));
  Serial.println(F("HID on USB OTG port; chord→romaji stub active"));
#endif

  buttons_begin();
  hid_keyboard_begin();
  chords_begin();

#if ARDUINO_USB_CDC_ON_BOOT
  Serial.println(F("Stub keys: LeftCenter=a, LeftN+RightE=ka, A=shi, Start=space"));
#endif
}

void loop() {
  buttons_poll();
  chords_poll();
  delay(1);
}
