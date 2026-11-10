#include <Arduino.h>

#include "buttons.h"
#include "chords.h"
#include "config_server.h"
#include "hid_keyboard.h"

// ESP32-S3-DevKitC-1 N16R8 — USB HID chord keyboard.
// Plug the PC into the board port labeled USB (OTG), not UART.
// Each switch: GPIO to one side, GND to the other. Pressed = LOW.

void setup() {
#if ARDUINO_USB_CDC_ON_BOOT
  Serial.begin(115200);
  delay(200);
  Serial.println();
  Serial.println(F("esp32-jis-keyboard"));
  Serial.println(F("14 direct keys, active-low, 15 ms debounce"));
  Serial.print(F("pins"));
  for (uint8_t i = 0; i < BTN_COUNT; ++i) {
    Serial.print(' ');
    Serial.print(kButtons[i].name);
    Serial.print(F("=GPIO"));
    Serial.print(kButtons[i].pin);
  }
  Serial.println();
  Serial.println(F("chord fires after all keys stay up for 25 ms"));
#endif

  buttons_begin();
  hid_keyboard_begin();
  chords_begin();
  config_server_begin();
}

void loop() {
  buttons_poll();
  chords_poll();
  config_server_poll();
  delay(1);
}
