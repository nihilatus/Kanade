#include "hid_keyboard.h"

#include <string.h>

#include "USB.h"
#include "USBHIDKeyboard.h"

static USBHIDKeyboard Keyboard;
static bool g_begun = false;

static constexpr uint16_t kTapGapMs = 12;

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
  delay(kTapGapMs);
  Keyboard.release(keycode);
  delay(kTapGapMs);
}

static void log_skip(const char *token) {
#if ARDUINO_USB_CDC_ON_BOOT
  Serial.print(F("hid skip token: "));
  Serial.println(token);
#else
  (void)token;
#endif
}

static bool is_plain_char(unsigned char c) {
  return c >= 0x21 && c <= 0x7E;
}

static bool named_key(const char *token, uint8_t *code) {
  struct Item {
    const char *name;
    uint8_t code;
  };
  static const Item kNamed[] = {
      {"Backspace", KEY_BACKSPACE},
      {"Space", ' '},
      {"Enter", KEY_RETURN},
      {"Esc", KEY_ESC},
      {"ESC", KEY_ESC},
      {"[ESC]", KEY_ESC},
      {"Up", KEY_UP_ARROW},
      {"Down", KEY_DOWN_ARROW},
      {"Left", KEY_LEFT_ARROW},
      {"Right", KEY_RIGHT_ARROW},
      {"Tab", KEY_TAB},
  };
  for (const Item &item : kNamed) {
    if (strcmp(token, item.name) == 0) {
      *code = item.code;
      return true;
    }
  }
  return false;
}

static bool modifier_code(const char *word, uint8_t *code) {
  if (strcmp(word, "Shift") == 0) {
    *code = KEY_LEFT_SHIFT;
    return true;
  }
  if (strcmp(word, "Ctrl") == 0) {
    *code = KEY_LEFT_CTRL;
    return true;
  }
  if (strcmp(word, "Alt") == 0) {
    *code = KEY_LEFT_ALT;
    return true;
  }
  if (strcmp(word, "Gui") == 0) {
    *code = KEY_LEFT_GUI;
    return true;
  }
  return false;
}

static void type_plain_chars(const char *token) {
  for (const char *p = token; *p; ++p) {
    Keyboard.write(static_cast<uint8_t>(*p));
    delay(kTapGapMs);
  }
  Keyboard.releaseAll();
}

// "Shift+1", "Ctrl+i", "Shift+Ctrl+a". Returns false if the token is not a combo.
static bool try_modifier_combo(const char *token) {
  if (strchr(token, '+') == nullptr) return false;

  char buf[24];
  const size_t len = strlen(token);
  if (len == 0 || len >= sizeof(buf)) {
    log_skip(token);
    return true;
  }
  memcpy(buf, token, len + 1);

  char *parts[6];
  int n = 0;
  char *cursor = buf;
  while (n < 6) {
    parts[n++] = cursor;
    char *plus = strchr(cursor, '+');
    if (plus == nullptr) break;
    *plus = '\0';
    cursor = plus + 1;
  }
  if (n < 2 || strchr(cursor, '+') != nullptr) {
    log_skip(token);
    return true;
  }

  uint8_t mods[4];
  if (n - 1 > 4) {
    log_skip(token);
    return true;
  }
  for (int i = 0; i < n - 1; ++i) {
    if (!modifier_code(parts[i], &mods[i])) {
      log_skip(token);
      return true;
    }
  }

  uint8_t key = 0;
  const char *last = parts[n - 1];
  if (last[0] != '\0' && last[1] == '\0' && is_plain_char(static_cast<unsigned char>(last[0]))) {
    key = static_cast<uint8_t>(last[0]);
  } else if (!named_key(last, &key)) {
    log_skip(token);
    return true;
  }

  for (int i = 0; i < n - 1; ++i) {
    Keyboard.press(mods[i]);
  }
  Keyboard.press(key);
  delay(kTapGapMs);
  Keyboard.releaseAll();
  delay(kTapGapMs);
  return true;
}

static void emit_token(const char *token) {
  uint8_t named = 0;
  if (named_key(token, &named)) {
    hid_tap_key(named);
    return;
  }
  if (try_modifier_combo(token)) return;

  for (const char *p = token; *p; ++p) {
    if (!is_plain_char(static_cast<unsigned char>(*p))) {
      log_skip(token);
      return;
    }
  }
  type_plain_chars(token);
}

void hid_type_recipe(const char *recipe) {
  if (!recipe || !hid_keyboard_ready()) return;

  const char *p = recipe;
  while (*p) {
    while (*p == ' ') ++p;
    if (*p == '\0') break;
    const char *start = p;
    while (*p && *p != ' ') ++p;
    const size_t len = static_cast<size_t>(p - start);
    char token[24];
    if (len == 0 || len >= sizeof(token)) {
#if ARDUINO_USB_CDC_ON_BOOT
      Serial.print(F("hid skip long token, len="));
      Serial.println(len);
#endif
      continue;
    }
    memcpy(token, start, len);
    token[len] = '\0';
    emit_token(token);
  }
  Keyboard.releaseAll();
}

void hid_type_romaji(const char *text) {
  if (!text || !hid_keyboard_ready()) return;
  for (const char *p = text; *p; ++p) {
    Keyboard.write(static_cast<uint8_t>(*p));
    delay(kTapGapMs);
  }
  Keyboard.releaseAll();
}
