#include "hid_keyboard.h"

#include <string.h>

#include "USB.h"
#include "USBHIDKeyboard.h"

static USBHIDKeyboard Keyboard;
static bool g_begun = false;

static constexpr uint16_t kTapGapMs = 12;
static constexpr uint8_t kGapNone = 0;

enum class HidOp : uint8_t { ReleaseAll, Press, Write };

struct HidEvent {
  HidOp op;
  uint8_t code;
  uint8_t gap_after_ms;
};

// One slot stays free so a full queue can still be closed with release-all.
static constexpr size_t kQueueCap = 192;
static HidEvent g_queue[kQueueCap];
static size_t g_head = 0;
static size_t g_count = 0;
static bool g_hold = false;
static uint32_t g_next_ms = 0;

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

static bool enqueue(HidOp op, uint8_t code, uint8_t gap_after_ms) {
  if (g_count >= kQueueCap) return false;
  const size_t tail = (g_head + g_count) % kQueueCap;
  g_queue[tail] = HidEvent{op, code, gap_after_ms};
  g_count++;
  return true;
}

static bool enqueue_release() {
  return enqueue(HidOp::ReleaseAll, 0, kTapGapMs);
}

static void send_event(const HidEvent &event) {
  if (!hid_keyboard_ready()) return;
  switch (event.op) {
    case HidOp::ReleaseAll:
      Keyboard.releaseAll();
      break;
    case HidOp::Press:
      Keyboard.press(event.code);
      break;
    case HidOp::Write:
      Keyboard.write(event.code);
      break;
  }
}

void hid_keyboard_poll() {
  if (g_count == 0) return;
  const uint32_t now = millis();
  if (g_hold && static_cast<int32_t>(now - g_next_ms) < 0) return;

  const HidEvent event = g_queue[g_head];
  g_head = (g_head + 1) % kQueueCap;
  g_count--;
  send_event(event);
  g_next_ms = now + event.gap_after_ms;
  g_hold = true;
}

static bool push_events(const HidEvent *events, size_t count, bool release_first) {
  const size_t need = count + (release_first ? 1 : 0);
  if (g_count + need > kQueueCap) {
#if ARDUINO_USB_CDC_ON_BOOT
    Serial.println(F("hid queue full, recipe skipped"));
#endif
    return false;
  }
  if (release_first && !enqueue_release()) return false;
  for (size_t i = 0; i < count; ++i) {
    if (!enqueue(events[i].op, events[i].code, events[i].gap_after_ms)) return false;
  }
  return true;
}

static bool queue_busy() {
  if (g_count > 0) return true;
  if (!g_hold) return false;
  // Previous recipe already finished its gap. The next one can start clean.
  if (static_cast<int32_t>(millis() - g_next_ms) >= 0) {
    g_hold = false;
    return false;
  }
  return true;
}

// Build one token into events. Returns false only when the token is not a combo
// and the caller should try another interpretation. A handled-but-invalid token
// returns true and logs a skip.
static bool queue_modifier_combo(const char *token, HidEvent *out, size_t cap, size_t *count) {
  if (strchr(token, '+') == nullptr) return false;
  // A lone "+" is the plus character, not a modifier chord.
  if (token[0] == '+' && token[1] == '\0') return false;

  char buf[24];
  const size_t len = strlen(token);
  if (len == 0 || len >= sizeof(buf)) {
    log_skip(token);
    *count = 0;
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
    // The last component may itself be "+": "Shift++" is Shift and plus.
    if (plus[1] == '\0') break;
    *plus = '\0';
    cursor = plus + 1;
  }
  const bool last_is_plus = cursor[0] == '+' && cursor[1] == '\0';
  if (n < 2 || (strchr(cursor, '+') != nullptr && !last_is_plus) || n - 1 > 4) {
    log_skip(token);
    *count = 0;
    return true;
  }

  uint8_t mods[4];
  for (int i = 0; i < n - 1; ++i) {
    if (!modifier_code(parts[i], &mods[i])) {
      log_skip(token);
      *count = 0;
      return true;
    }
  }

  uint8_t key = 0;
  const char *last = parts[n - 1];
  if (last[0] != '\0' && last[1] == '\0' && is_plain_char(static_cast<unsigned char>(last[0]))) {
    key = static_cast<uint8_t>(last[0]);
  } else if (!named_key(last, &key)) {
    log_skip(token);
    *count = 0;
    return true;
  }

  const size_t presses = static_cast<size_t>(n - 1) + 1;
  if (presses + 1 > cap) {
    log_skip(token);
    *count = 0;
    return true;
  }
  for (int i = 0; i < n - 1; ++i) {
    out[i] = HidEvent{HidOp::Press, mods[i], kGapNone};
  }
  out[n - 1] = HidEvent{HidOp::Press, key, kTapGapMs};
  out[n] = HidEvent{HidOp::ReleaseAll, 0, kTapGapMs};
  *count = presses + 1;
  return true;
}

static bool queue_token(const char *token, HidEvent *out, size_t cap, size_t *count) {
  *count = 0;
  uint8_t named = 0;
  if (named_key(token, &named)) {
    if (cap < 2) return false;
    out[0] = HidEvent{HidOp::Press, named, kTapGapMs};
    out[1] = HidEvent{HidOp::ReleaseAll, 0, kTapGapMs};
    *count = 2;
    return true;
  }

  size_t combo_count = 0;
  if (queue_modifier_combo(token, out, cap, &combo_count)) {
    *count = combo_count;
    return true;
  }

  const size_t len = strlen(token);
  if (len + 1 > cap) {
    log_skip(token);
    return true;
  }
  for (size_t i = 0; i < len; ++i) {
    if (!is_plain_char(static_cast<unsigned char>(token[i]))) {
      log_skip(token);
      *count = 0;
      return true;
    }
    out[i] = HidEvent{HidOp::Write, static_cast<uint8_t>(token[i]), kTapGapMs};
  }
  out[len] = HidEvent{HidOp::ReleaseAll, 0, kGapNone};
  *count = len + 1;
  return true;
}

void hid_type_recipe(const char *recipe) {
  if (!recipe || !hid_keyboard_ready()) return;

  HidEvent batch[96];
  size_t n = 0;
  const bool release_first = queue_busy();

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

    size_t added = 0;
    if (n >= sizeof(batch) / sizeof(batch[0]) ||
        !queue_token(token, batch + n, (sizeof(batch) / sizeof(batch[0])) - n, &added)) {
#if ARDUINO_USB_CDC_ON_BOOT
      Serial.println(F("hid recipe too long, skipped"));
#endif
      return;
    }
    n += added;
  }

  if (n == 0 && !release_first) return;
  push_events(batch, n, release_first);
}

void hid_type_romaji(const char *text) {
  if (!text || !hid_keyboard_ready()) return;
  const bool release_first = queue_busy();
  const size_t len = strlen(text);
  if (len > 80) {
#if ARDUINO_USB_CDC_ON_BOOT
    Serial.println(F("hid romaji too long, skipped"));
#endif
    return;
  }
  HidEvent batch[81];
  for (size_t i = 0; i < len; ++i) {
    batch[i] = HidEvent{HidOp::Write, static_cast<uint8_t>(text[i]), kTapGapMs};
  }
  push_events(batch, len, release_first);
}

void hid_tap_key(uint8_t keycode) {
  if (!hid_keyboard_ready()) return;
  HidEvent batch[2] = {
      HidEvent{HidOp::Press, keycode, kTapGapMs},
      HidEvent{HidOp::ReleaseAll, 0, kTapGapMs},
  };
  push_events(batch, 2, queue_busy());
}

void hid_release_all() {
  if (!g_begun) return;
  enqueue_release();
}
