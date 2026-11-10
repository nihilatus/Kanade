#include "hid_keyboard.h"

#include <string.h>

#include "USB.h"
#include "USBHIDKeyboard.h"

#include "chord_core.h"

static USBHIDKeyboard Keyboard;
static bool g_begun = false;

static constexpr uint16_t kTapGapMs = 12;

enum class HidOp : uint8_t { ReleaseAll, Press };

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

static void log_long_token(size_t len) {
#if ARDUINO_USB_CDC_ON_BOOT
  Serial.print(F("hid skip long token, len="));
  Serial.println(len);
#else
  (void)len;
#endif
}

static uint8_t press_code(const ChordEvent &event) {
  switch (event.key) {
    case ChordKeyKind::Char:
      return event.code;
    case ChordKeyKind::Shift:
      return KEY_LEFT_SHIFT;
    case ChordKeyKind::Ctrl:
      return KEY_LEFT_CTRL;
    case ChordKeyKind::Alt:
      return KEY_LEFT_ALT;
    case ChordKeyKind::Gui:
      return KEY_LEFT_GUI;
    case ChordKeyKind::Backspace:
      return KEY_BACKSPACE;
    case ChordKeyKind::Space:
      return ' ';
    case ChordKeyKind::Enter:
      return KEY_RETURN;
    case ChordKeyKind::Esc:
      return KEY_ESC;
    case ChordKeyKind::Up:
      return KEY_UP_ARROW;
    case ChordKeyKind::Down:
      return KEY_DOWN_ARROW;
    case ChordKeyKind::Left:
      return KEY_LEFT_ARROW;
    case ChordKeyKind::Right:
      return KEY_RIGHT_ARROW;
    case ChordKeyKind::Tab:
      return KEY_TAB;
  }
  return 0;
}

void hid_type_recipe(const char *recipe) {
  if (!recipe || !hid_keyboard_ready()) return;
  const bool release_first = queue_busy();

  static ChordRecipe parsed;
  if (!chord_parse_recipe(recipe, &parsed)) return;
  for (size_t i = 0; i < parsed.skip_count; ++i) {
    const ChordSkip &skip = parsed.skips[i];
    if (skip.too_long) log_long_token(skip.length);
    else log_skip(skip.text);
  }
  if (parsed.failed) {
#if ARDUINO_USB_CDC_ON_BOOT
    Serial.println(F("hid recipe too long, skipped"));
#endif
    return;
  }

  if (parsed.count == 0 && !release_first) return;
  // A plain letter is press then release, each held across a host poll.
  // One Keyboard.write() would queue both reports before the ~10 ms poll.
  size_t slots = release_first ? 1 : 0;
  for (size_t i = 0; i < parsed.count; ++i) {
    slots += parsed.events[i].op == ChordEvOp::Write ? 2 : 1;
  }
  if (g_count + slots > kQueueCap) {
#if ARDUINO_USB_CDC_ON_BOOT
    Serial.println(F("hid queue full, recipe skipped"));
#endif
    return;
  }
  if (release_first && !enqueue_release()) return;
  for (size_t i = 0; i < parsed.count; ++i) {
    const ChordEvent &event = parsed.events[i];
    if (event.op == ChordEvOp::Write) {
      const uint8_t gap = event.gap_after_ms == 0 ? kTapGapMs : event.gap_after_ms;
      enqueue(HidOp::Press, event.code, gap);
      enqueue(HidOp::ReleaseAll, 0, gap);
      continue;
    }
    HidOp op = HidOp::ReleaseAll;
    uint8_t code = 0;
    if (event.op == ChordEvOp::Press) {
      op = HidOp::Press;
      code = press_code(event);
    }
    enqueue(op, code, event.gap_after_ms);
  }
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
  if (len == 0 && !release_first) return;
  const size_t slots = len * 2 + (release_first ? 1 : 0);
  if (g_count + slots > kQueueCap) {
#if ARDUINO_USB_CDC_ON_BOOT
    Serial.println(F("hid queue full, romaji skipped"));
#endif
    return;
  }
  if (release_first && !enqueue_release()) return;
  for (size_t i = 0; i < len; ++i) {
    const uint8_t code = static_cast<uint8_t>(text[i]);
    enqueue(HidOp::Press, code, kTapGapMs);
    enqueue(HidOp::ReleaseAll, 0, kTapGapMs);
  }
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
