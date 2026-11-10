#include "hid_keyboard.h"

#include "USB.h"
#include "USBHIDKeyboard.h"

static USBHIDKeyboard Keyboard;
static bool g_begun = false;

void hid_keyboard_begin() {
  // Identify as a keyboard. Japanese IME on the PC still expects romaji
  // Latin letters; a full JIS HID country code / layout can come later.
  USB.VID(0x303A);  // Espressif
  USB.PID(0x4004);  // local prototype — change before shipping
  USB.productName("ESP32 JIS Chord KB");
  USB.manufacturerName("JoystickWriter Lab");
  USB.serialNumber("jis-kb-proto-1");

  Keyboard.begin();
  USB.begin();
  g_begun = true;
}

bool hid_keyboard_ready() {
  return g_begun && USB;
}

void hid_release_all() {
  if (!g_begun) return;
  Keyboard.releaseAll();
}

void hid_tap_key(uint8_t keycode) {
  if (!hid_keyboard_ready()) return;
  Keyboard.press(keycode);
  delay(8);
  Keyboard.release(keycode);
  delay(8);
}

void hid_type_romaji(const char *text) {
  if (!text || !hid_keyboard_ready()) return;

  // Modest inter-key delay so host IME / apps keep up on first bring-up.
  for (const char *p = text; *p; ++p) {
    Keyboard.write(static_cast<uint8_t>(*p));
    delay(12);
  }
  Keyboard.releaseAll();
}
