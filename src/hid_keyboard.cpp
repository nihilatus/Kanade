#include "hid_keyboard.h"

#include <string.h>

#include "USB.h"
#include "USBHIDKeyboard.h"

#include "chord_core.h"

static USBHIDKeyboard Keyboard;
static bool g_begun = false;

enum class HidOp : uint8_t { ReleaseAll, Press, PressRaw };

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
  // ASCII goes through the JIS map. a–z and the digits share usages with a
  // US keyboard; the characters this table actually sends that do not are
  // '[' (0x30) and ']' (0x31). Shift+8 / Shift+9 become （ ） only when the
  // host keyboard type is Japanese. The USB country code stays unset: it
  // does not switch Windows to the 106/109 layout.
  // Hankaku (0x35), Henkan (0x8A), Muhenkan (0x8B), and Kana (0x88) are
  // pressRaw usages Windows maps under that keyboard type. Eisuu is LANG2
  // (0x91), which macOS uses for 英数 and Windows often ignores.
  // The stock keyboard descriptor already declares usages 0–255.
  USB.VID(0x303A);  // Espressif
  USB.PID(0x4004);  // local prototype — change before shipping
  USB.productName("ESP32 JIS Chord KB");
  USB.manufacturerName("JoystickWriter Lab");
  USB.serialNumber("jis-kb-proto-1");

  Keyboard.begin(KeyboardLayout_ja_JP);
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

static void rollback(size_t added) {
  if (added > g_count) added = g_count;
  g_count -= added;
}

static void log_queue_full(const char *what) {
#if ARDUINO_USB_CDC_ON_BOOT
  Serial.print(F("hid queue full, "));
  Serial.println(what);
#else
  (void)what;
#endif
}

static bool take_slot(HidOp op, uint8_t code, uint8_t gap, size_t *added, const char *what) {
  if (!enqueue(op, code, gap)) {
    rollback(*added);
    log_queue_full(what);
    return false;
  }
  *added += 1;
  return true;
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
    case HidOp::PressRaw:
      Keyboard.pressRaw(event.code);
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
    log_queue_full("recipe skipped");
    return false;
  }
  size_t added = 0;
  if (release_first && !take_slot(HidOp::ReleaseAll, 0, kChordTapGapMs, &added, "recipe skipped")) {
    return false;
  }
  for (size_t i = 0; i < count; ++i) {
    if (!take_slot(events[i].op, events[i].code, events[i].gap_after_ms, &added, "recipe skipped")) {
      return false;
    }
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
    case ChordKeyKind::Raw:
      return event.code;
  }
  return 0;
}

// One buffer for loop(). hid_type_recipe must not be called reentrantly.
static ChordRecipe g_parsed_recipe;

void hid_type_recipe(const char *recipe) {
  if (!recipe || !hid_keyboard_ready()) return;
  const bool release_first = queue_busy();

  ChordRecipe &parsed = g_parsed_recipe;
  const bool parsed_ok = chord_parse_recipe(recipe, &parsed);
  for (size_t i = 0; i < parsed.skip_count; ++i) {
    const ChordSkip &skip = parsed.skips[i];
    if (skip.too_long) log_long_token(skip.length);
    else log_skip(skip.text);
  }
  if (!parsed_ok || parsed.overflow_skips) {
#if ARDUINO_USB_CDC_ON_BOOT
    if (parsed.failed) Serial.println(F("hid recipe too long, skipped"));
    else Serial.println(F("hid recipe dropped tokens, skipped"));
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
    log_queue_full("recipe skipped");
    return;
  }
  size_t added = 0;
  if (release_first && !take_slot(HidOp::ReleaseAll, 0, kChordTapGapMs, &added, "recipe skipped")) {
    return;
  }
  for (size_t i = 0; i < parsed.count; ++i) {
    const ChordEvent &event = parsed.events[i];
    if (event.op == ChordEvOp::Write) {
      const uint8_t gap = event.gap_after_ms == 0 ? kChordTapGapMs : event.gap_after_ms;
      if (!take_slot(HidOp::Press, event.code, gap, &added, "recipe skipped")) return;
      if (!take_slot(HidOp::ReleaseAll, 0, gap, &added, "recipe skipped")) return;
      continue;
    }
    HidOp op = HidOp::ReleaseAll;
    uint8_t code = 0;
    if (event.op == ChordEvOp::Press) {
      if (event.key == ChordKeyKind::Raw) {
        op = HidOp::PressRaw;
        code = event.code;
      } else {
        op = HidOp::Press;
        code = press_code(event);
      }
    }
    if (!take_slot(op, code, event.gap_after_ms, &added, "recipe skipped")) return;
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
    log_queue_full("romaji skipped");
    return;
  }
  size_t added = 0;
  if (release_first && !take_slot(HidOp::ReleaseAll, 0, kChordTapGapMs, &added, "romaji skipped")) {
    return;
  }
  for (size_t i = 0; i < len; ++i) {
    const uint8_t code = static_cast<uint8_t>(text[i]);
    if (!take_slot(HidOp::Press, code, kChordTapGapMs, &added, "romaji skipped")) return;
    if (!take_slot(HidOp::ReleaseAll, 0, kChordTapGapMs, &added, "romaji skipped")) return;
  }
}

void hid_tap_key(uint8_t keycode) {
  if (!hid_keyboard_ready()) return;
  HidEvent batch[2] = {
      HidEvent{HidOp::Press, keycode, kChordTapGapMs},
      HidEvent{HidOp::ReleaseAll, 0, kChordTapGapMs},
  };
  push_events(batch, 2, queue_busy());
}

void hid_release_all() {
  if (!g_begun) return;
  size_t added = 0;
  take_slot(HidOp::ReleaseAll, 0, kChordTapGapMs, &added, "release skipped");
}
