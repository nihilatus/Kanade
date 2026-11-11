#include "hid_keyboard.h"

#include <stdio.h>
#include <string.h>

#include "USB.h"
#include "USBHIDKeyboard.h"
#include "class/hid/hid_device.h"

#include "chord_core.h"
#include "console_log.h"

static USBHIDKeyboard Keyboard;
static bool g_begun = false;
// Whole outputs not queued, tokens skipped while the rest was kept, and
// outputs refused because the host was not enumerated or was asleep.
static uint32_t g_drop_recipes = 0;
static uint32_t g_skip_tokens = 0;
static uint32_t g_not_ready = 0;

static void note_dropped() { g_drop_recipes++; }

enum class HidOp : uint8_t { ReleaseAll, Press, PressRaw };

struct HidEvent {
  HidOp op;
  uint8_t code;
  uint8_t gap_after_ms;
};

// expand: Press(code) then ReleaseAll, both held for gap_after_ms.
// Otherwise one report, op/code/gap as given.
struct Stroke {
  bool expand;
  HidOp op;
  uint8_t code;
  uint8_t gap_after_ms;
};

// Last slot is only for hid_release_all. Recipes, romaji, and taps stop at
// kQueueCap - 1, so a full keystroke queue can still be closed.
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
  USB.productName("Kanade");
  USB.manufacturerName("Kanade");
  USB.serialNumber("kanade-1");

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

static bool enqueue(HidOp op, uint8_t code, uint8_t gap_after_ms, bool allow_last) {
  const size_t limit = allow_last ? kQueueCap : kQueueCap - 1;
  if (g_count >= limit) return false;
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

static bool take_slot(HidOp op, uint8_t code, uint8_t gap, size_t *added, const char *what,
                      bool allow_last = false) {
  if (!enqueue(op, code, gap, allow_last)) {
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

// Peak time spent inside one send, printed every 5 s while keys are going out.
// Idle polls are not counted. SendReport waits for the host after tud_hid_ready()
// is already true, so this is the number that shows a stall.
static void note_send_us(uint32_t elapsed_us) {
  static uint32_t max_us = 0;
  static uint32_t window_ms = 0;
  static bool window_open = false;
  if (elapsed_us > max_us) max_us = elapsed_us;
  const uint32_t now = millis();
  if (!window_open) {
    window_open = true;
    window_ms = now;
  }
  if (static_cast<uint32_t>(now - window_ms) < 5000) return;
  window_ms = now;
  char line[40];
  snprintf(line, sizeof(line), "hid poll max %u us", static_cast<unsigned>(max_us));
  console_println(line);
  max_us = 0;
}

void hid_keyboard_poll() {
  if (g_count == 0) return;
  const uint32_t now = millis();
  if (g_hold && static_cast<int32_t>(now - g_next_ms) < 0) return;
  // Endpoint still holds the previous report. Leave this event queued.
  if (!tud_hid_ready()) return;

  const uint32_t t0 = micros();
  const HidEvent event = g_queue[g_head];
  g_head = (g_head + 1) % kQueueCap;
  g_count--;
  send_event(event);
  const uint32_t elapsed = micros() - t0;
  g_next_ms = now + event.gap_after_ms;
  g_hold = true;
  note_send_us(elapsed);
}

// Count slots, refuse the batch if it would use the release-all reserve, then
// enqueue. A Write-style stroke is press plus release. Failure rolls the batch back.
static bool enqueue_strokes(const Stroke *strokes, size_t count, bool release_first,
                            const char *what) {
  size_t need = release_first ? 1 : 0;
  for (size_t i = 0; i < count; ++i) need += strokes[i].expand ? 2 : 1;
  if (need == 0) return true;
  if (g_count + need + 1 > kQueueCap) {
    note_dropped();
    log_queue_full(what);
    return false;
  }
  size_t added = 0;
  if (release_first && !take_slot(HidOp::ReleaseAll, 0, kChordTapGapMs, &added, what)) return false;
  for (size_t i = 0; i < count; ++i) {
    const Stroke &stroke = strokes[i];
    if (stroke.expand) {
      // A plain letter is press then release, each held across a host poll.
      // One Keyboard.write() would queue both reports before the ~10 ms poll.
      const uint8_t gap = stroke.gap_after_ms == 0 ? kChordTapGapMs : stroke.gap_after_ms;
      if (!take_slot(HidOp::Press, stroke.code, gap, &added, what)) return false;
      if (!take_slot(HidOp::ReleaseAll, 0, gap, &added, what)) return false;
      continue;
    }
    if (!take_slot(stroke.op, stroke.code, stroke.gap_after_ms, &added, what)) return false;
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

static void log_unrecorded_skips(size_t count) {
#if ARDUINO_USB_CDC_ON_BOOT
  Serial.print(F("hid skip tokens not logged: "));
  Serial.println(count);
#else
  (void)count;
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

// One buffer for loop(). hid_type_recipe and hid_type_romaji must not be called reentrantly.
static ChordRecipe g_parsed_recipe;
static Stroke g_strokes[kChordRecipeCap];

static Stroke stroke_from(const ChordEvent &event) {
  Stroke stroke{};
  stroke.gap_after_ms = event.gap_after_ms;
  if (event.op == ChordEvOp::Write) {
    stroke.expand = true;
    stroke.op = HidOp::Press;
    stroke.code = event.code;
    return stroke;
  }
  stroke.expand = false;
  if (event.op == ChordEvOp::Press && event.key == ChordKeyKind::Raw) {
    stroke.op = HidOp::PressRaw;
    stroke.code = event.code;
    return stroke;
  }
  if (event.op == ChordEvOp::Press) {
    stroke.op = HidOp::Press;
    stroke.code = press_code(event);
    return stroke;
  }
  stroke.op = HidOp::ReleaseAll;
  stroke.code = 0;
  return stroke;
}

void hid_type_recipe(const char *recipe) {
  if (!recipe) return;
  if (!hid_keyboard_ready()) {
    g_not_ready++;
    return;
  }
  const bool release_first = queue_busy();

  ChordRecipe &parsed = g_parsed_recipe;
  const bool parsed_ok = chord_parse_recipe(recipe, &parsed);
  for (size_t i = 0; i < parsed.skip_count; ++i) {
    const ChordSkip &skip = parsed.skips[i];
    if (skip.too_long) log_long_token(skip.length);
    else log_skip(skip.text);
  }
  if (parsed.overflow_skips) {
    const size_t stored = parsed.skip_count;
    const size_t total = parsed.skip_total;
    log_unrecorded_skips(total > stored ? total - stored : 0);
  }
  // parsed_ok is already !failed, with count cleared. Name failed here so a
  // partial recipe cannot be queued if that return is missed.
  // Unknown tokens stay skipped and the rest is typed. overflow_skips only
  // means later skip text was not stored; skip_total still counts those tokens.
  if (!parsed_ok || parsed.failed) {
    note_dropped();
#if ARDUINO_USB_CDC_ON_BOOT
    Serial.println(F("hid recipe too long, skipped"));
#endif
    return;
  }

  if (parsed.count == 0 && !release_first) {
    g_skip_tokens += static_cast<uint32_t>(parsed.skip_total);
    return;
  }
  for (size_t i = 0; i < parsed.count; ++i) g_strokes[i] = stroke_from(parsed.events[i]);
  if (!enqueue_strokes(g_strokes, parsed.count, release_first, "recipe skipped")) return;
  g_skip_tokens += static_cast<uint32_t>(parsed.skip_total);
}

void hid_type_romaji(const char *text) {
  if (!text) return;
  if (!hid_keyboard_ready()) {
    g_not_ready++;
    return;
  }
  const bool release_first = queue_busy();
  const size_t len = strlen(text);
  if (len > kRomajiMax) {
    note_dropped();
#if ARDUINO_USB_CDC_ON_BOOT
    Serial.println(F("hid romaji too long, skipped"));
#endif
    return;
  }
  if (len == 0 && !release_first) return;
  for (size_t i = 0; i < len; ++i) {
    // Keyboard.press() treats 0x80–0x87 as modifiers and 0x88+ as non-printing keys.
    if (static_cast<unsigned char>(text[i]) >= 0x80) {
      note_dropped();
#if ARDUINO_USB_CDC_ON_BOOT
      Serial.println(F("hid romaji non-ascii, skipped"));
#endif
      return;
    }
  }
  for (size_t i = 0; i < len; ++i) {
    g_strokes[i].expand = true;
    g_strokes[i].op = HidOp::Press;
    g_strokes[i].code = static_cast<uint8_t>(text[i]);
    g_strokes[i].gap_after_ms = kChordTapGapMs;
  }
  enqueue_strokes(g_strokes, len, release_first, "romaji skipped");
}

void hid_tap_key(uint8_t keycode) {
  if (!hid_keyboard_ready()) {
    g_not_ready++;
    return;
  }
  const Stroke tap{true, HidOp::Press, keycode, kChordTapGapMs};
  enqueue_strokes(&tap, 1, queue_busy(), "tap skipped");
}

void hid_release_all() {
  if (!g_begun) return;
  size_t added = 0;
  take_slot(HidOp::ReleaseAll, 0, kChordTapGapMs, &added, "release skipped", true);
}

void hid_print_drops() {
  char line[72];
  snprintf(line, sizeof(line), "drops recipes=%u tokens=%u not_ready=%u",
           static_cast<unsigned>(g_drop_recipes), static_cast<unsigned>(g_skip_tokens),
           static_cast<unsigned>(g_not_ready));
  console_println(line);
}
