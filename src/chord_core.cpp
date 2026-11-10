#include "chord_core.h"

#include <string.h>

static const char *const kKeyNames[kChordKeyCount] = {
    "LL",  "LU",  "LR",  "LD",  "LT0", "LT1", "LT2",
    "RL",  "RU",  "RR",  "RD",  "RT0", "RT1", "RT2",
};

const char *chord_key_name(int index) {
  if (index < 0 || index >= kChordKeyCount) return nullptr;
  return kKeyNames[index];
}

int chord_key_index(const char *name) {
  if (name == nullptr) return -1;
  for (int i = 0; i < kChordKeyCount; ++i) {
    if (strcmp(name, kKeyNames[i]) == 0) return i;
  }
  return -1;
}

static uint8_t popcount16(uint16_t value) {
  uint8_t n = 0;
  while (value != 0) {
    n = static_cast<uint8_t>(n + (value & 1u));
    value = static_cast<uint16_t>(value >> 1);
  }
  return n;
}

int chord_find_rule(const ChordRule *rules, size_t count, uint16_t mask) {
  if (rules == nullptr) return -1;
  int best = -1;
  int best_req = -1;
  int best_opt = 255;
  uint16_t best_id = 0xFFFF;

  for (size_t i = 0; i < count; ++i) {
    const ChordRule &row = rules[i];
    if ((row.required & mask) != row.required) continue;
    const uint16_t allowed = static_cast<uint16_t>(row.required | row.optional);
    if ((mask & static_cast<uint16_t>(~allowed)) != 0) continue;

    const int req_n = popcount16(row.required);
    const int opt_n = popcount16(row.optional);
    const bool better = best < 0 || req_n > best_req ||
                        (req_n == best_req && opt_n < best_opt) ||
                        (req_n == best_req && opt_n == best_opt && row.id < best_id);
    if (!better) continue;
    best = static_cast<int>(i);
    best_req = req_n;
    best_opt = opt_n;
    best_id = row.id;
  }
  return best;
}

void chord_peak_clear(ChordPeak *peak) {
  if (peak == nullptr) return;
  peak->mask = 0;
  peak->count = 0;
}

void chord_note_snapshot(ChordPeak *peak, uint16_t mask) {
  if (peak == nullptr) return;
  const uint8_t count = popcount16(mask);
  if (count > peak->count || (count == peak->count && mask != peak->mask)) {
    peak->mask = mask;
    peak->count = count;
  }
}

bool chord_peak_broken(const ChordPeak &peak, uint16_t mask) {
  return peak.count > 0 && (mask & peak.mask) != peak.mask;
}

void chord_engine_reset(ChordEngine *engine) {
  if (engine == nullptr) return;
  engine->phase = ChordPhase::Idle;
  engine->mode = ChordFireMode::ReleaseAll;
  chord_peak_clear(&engine->peak);
  engine->guard_since = 0;
}

static ChordStep take_peak(ChordEngine *engine) {
  const uint16_t mask = engine->peak.mask;
  chord_peak_clear(&engine->peak);
  ChordStep step;
  step.fire = mask != 0;
  step.fire_mask = mask;
  return step;
}

static void begin_hold(ChordEngine *engine, uint16_t mask) {
  engine->phase = ChordPhase::Holding;
  chord_peak_clear(&engine->peak);
  chord_note_snapshot(&engine->peak, mask);
}

ChordStep chord_engine_step(ChordEngine *engine, uint16_t mask, uint32_t now_ms) {
  ChordStep step;
  step.fire = false;
  step.fire_mask = 0;
  if (engine == nullptr) return step;

  switch (engine->phase) {
    case ChordPhase::Idle:
      if (mask != 0) begin_hold(engine, mask);
      break;

    case ChordPhase::Holding:
      if (engine->mode == ChordFireMode::FirstUp) {
        if (chord_peak_broken(engine->peak, mask)) {
          step = take_peak(engine);
          engine->phase = mask == 0 ? ChordPhase::Idle : ChordPhase::Draining;
        } else {
          chord_note_snapshot(&engine->peak, mask);
        }
      } else if (mask == 0) {
        engine->phase = ChordPhase::ReleaseGuard;
        engine->guard_since = now_ms;
      } else {
        chord_note_snapshot(&engine->peak, mask);
      }
      break;

    case ChordPhase::ReleaseGuard:
      if (mask != 0) {
        engine->phase = ChordPhase::Holding;
        chord_note_snapshot(&engine->peak, mask);
      } else if ((now_ms - engine->guard_since) >= kChordReleaseGuardMs) {
        engine->phase = ChordPhase::Idle;
        step = take_peak(engine);
      }
      break;

    case ChordPhase::Draining:
      if (mask == 0) engine->phase = ChordPhase::Idle;
      break;
  }
  return step;
}

ChordStep chord_engine_set_mode(ChordEngine *engine, ChordFireMode mode) {
  ChordStep step;
  step.fire = false;
  step.fire_mask = 0;
  if (engine == nullptr) return step;
  if (mode != engine->mode && mode == ChordFireMode::FirstUp &&
      engine->phase == ChordPhase::ReleaseGuard) {
    step = take_peak(engine);
    engine->phase = ChordPhase::Idle;
  }
  engine->mode = mode;
  return step;
}

static bool is_plain_char(unsigned char c) { return c >= 0x21 && c <= 0x7E; }

static bool named_key(const char *token, ChordKeyKind *kind, uint8_t *code) {
  struct Item {
    const char *name;
    ChordKeyKind kind;
    uint8_t code;
  };
  // Raw codes are USB HID Keyboard/Keypad usages (pressRaw). The others keep
  // the existing kind and a zero code so the firmware maps them itself.
  static const Item kNamed[] = {
      {"Backspace", ChordKeyKind::Backspace, 0},
      {"Space", ChordKeyKind::Space, 0},
      {"Enter", ChordKeyKind::Enter, 0},
      {"Esc", ChordKeyKind::Esc, 0},
      {"ESC", ChordKeyKind::Esc, 0},
      {"[ESC]", ChordKeyKind::Esc, 0},
      {"Up", ChordKeyKind::Up, 0},
      {"Down", ChordKeyKind::Down, 0},
      {"Left", ChordKeyKind::Left, 0},
      {"Right", ChordKeyKind::Right, 0},
      {"Tab", ChordKeyKind::Tab, 0},
      {"Hankaku", ChordKeyKind::Raw, 0x35},
      {"Henkan", ChordKeyKind::Raw, 0x8A},
      {"Muhenkan", ChordKeyKind::Raw, 0x8B},
      {"Kana", ChordKeyKind::Raw, 0x88},
      {"Eisuu", ChordKeyKind::Raw, 0x91},
      {"Yen", ChordKeyKind::Raw, 0x89},
      {"Ro", ChordKeyKind::Raw, 0x87},
      {"Delete", ChordKeyKind::Raw, 0x4C},
      {"Insert", ChordKeyKind::Raw, 0x49},
      {"Home", ChordKeyKind::Raw, 0x4A},
      {"End", ChordKeyKind::Raw, 0x4D},
      {"PageUp", ChordKeyKind::Raw, 0x4B},
      {"PageDown", ChordKeyKind::Raw, 0x4E},
      {"CapsLock", ChordKeyKind::Raw, 0x39},
      {"F1", ChordKeyKind::Raw, 0x3A},
      {"F2", ChordKeyKind::Raw, 0x3B},
      {"F3", ChordKeyKind::Raw, 0x3C},
      {"F4", ChordKeyKind::Raw, 0x3D},
      {"F5", ChordKeyKind::Raw, 0x3E},
      {"F6", ChordKeyKind::Raw, 0x3F},
      {"F7", ChordKeyKind::Raw, 0x40},
      {"F8", ChordKeyKind::Raw, 0x41},
      {"F9", ChordKeyKind::Raw, 0x42},
      {"F10", ChordKeyKind::Raw, 0x43},
      {"F11", ChordKeyKind::Raw, 0x44},
      {"F12", ChordKeyKind::Raw, 0x45},
  };
  for (const Item &item : kNamed) {
    if (strcmp(token, item.name) == 0) {
      *kind = item.kind;
      *code = item.code;
      return true;
    }
  }
  return false;
}

static bool modifier_kind(const char *word, ChordKeyKind *kind) {
  if (strcmp(word, "Shift") == 0) {
    *kind = ChordKeyKind::Shift;
    return true;
  }
  if (strcmp(word, "Ctrl") == 0) {
    *kind = ChordKeyKind::Ctrl;
    return true;
  }
  if (strcmp(word, "Alt") == 0) {
    *kind = ChordKeyKind::Alt;
    return true;
  }
  if (strcmp(word, "Gui") == 0) {
    *kind = ChordKeyKind::Gui;
    return true;
  }
  return false;
}

static void add_skip(ChordRecipe *out, const char *token, size_t length, bool too_long) {
  if (out->skip_count >= kChordSkipCap) {
    out->overflow_skips = true;
    return;
  }
  ChordSkip &skip = out->skips[out->skip_count++];
  skip.length = length;
  skip.too_long = too_long;
  skip.text[0] = '\0';
  if (!too_long && token != nullptr) {
    size_t n = length;
    if (n >= sizeof(skip.text)) n = sizeof(skip.text) - 1;
    memcpy(skip.text, token, n);
    skip.text[n] = '\0';
  }
}

static void put_event(ChordRecipe *out, ChordEvOp op, ChordKeyKind key, uint8_t code,
                      uint8_t gap_after_ms) {
  ChordEvent &event = out->events[out->count++];
  event.op = op;
  event.key = key;
  event.code = code;
  event.gap_after_ms = gap_after_ms;
}

// True when the token contains '+' and is not the lone plus character.
// A handled token is either appended or recorded as a skip.
static bool take_combo(ChordRecipe *out, const char *token) {
  if (strchr(token, '+') == nullptr) return false;
  if (token[0] == '+' && token[1] == '\0') return false;

  char buf[24];
  const size_t len = strlen(token);
  if (len == 0 || len >= sizeof(buf)) {
    add_skip(out, token, len, true);
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
    // "Shift++" keeps a final "+" as its own part.
    if (plus[1] == '\0') break;
    *plus = '\0';
    cursor = plus + 1;
  }
  const bool last_is_plus = cursor[0] == '+' && cursor[1] == '\0';
  if (n < 2 || (strchr(cursor, '+') != nullptr && !last_is_plus) || n - 1 > 4) {
    add_skip(out, token, len, false);
    return true;
  }

  ChordKeyKind mods[4];
  for (int i = 0; i < n - 1; ++i) {
    if (!modifier_kind(parts[i], &mods[i])) {
      add_skip(out, token, len, false);
      return true;
    }
  }

  ChordKeyKind key_kind = ChordKeyKind::Char;
  uint8_t key_code = 0;
  const char *last = parts[n - 1];
  if (last[0] != '\0' && last[1] == '\0' && is_plain_char(static_cast<unsigned char>(last[0]))) {
    key_kind = ChordKeyKind::Char;
    key_code = static_cast<uint8_t>(last[0]);
  } else if (!named_key(last, &key_kind, &key_code)) {
    add_skip(out, token, len, false);
    return true;
  }

  const size_t presses = static_cast<size_t>(n - 1) + 1;
  const size_t room = kChordRecipeCap - out->count;
  if (presses + 1 > room) {
    add_skip(out, token, len, false);
    return true;
  }
  for (int i = 0; i < n - 1; ++i) {
    put_event(out, ChordEvOp::Press, mods[i], 0, kChordGapNone);
  }
  put_event(out, ChordEvOp::Press, key_kind, key_code, kChordTapGapMs);
  put_event(out, ChordEvOp::ReleaseAll, ChordKeyKind::Char, 0, kChordTapGapMs);
  return true;
}

// Abort means the whole recipe does not fit. A skip still returns false here
// so the caller keeps going.
static bool take_token(ChordRecipe *out, const char *token, bool *abort_recipe) {
  *abort_recipe = false;
  const size_t room = kChordRecipeCap - out->count;

  ChordKeyKind named = ChordKeyKind::Char;
  uint8_t named_code = 0;
  if (named_key(token, &named, &named_code)) {
    if (room < 2) {
      *abort_recipe = true;
      return false;
    }
    put_event(out, ChordEvOp::Press, named, named_code, kChordTapGapMs);
    put_event(out, ChordEvOp::ReleaseAll, ChordKeyKind::Char, 0, kChordTapGapMs);
    return true;
  }

  if (take_combo(out, token)) return true;

  const size_t len = strlen(token);
  if (len + 1 > room) {
    add_skip(out, token, len, false);
    return true;
  }
  for (size_t i = 0; i < len; ++i) {
    if (!is_plain_char(static_cast<unsigned char>(token[i]))) {
      add_skip(out, token, len, false);
      return true;
    }
  }
  for (size_t i = 0; i < len; ++i) {
    put_event(out, ChordEvOp::Write, ChordKeyKind::Char, static_cast<uint8_t>(token[i]),
              kChordTapGapMs);
  }
  put_event(out, ChordEvOp::ReleaseAll, ChordKeyKind::Char, 0, kChordGapNone);
  return true;
}

bool chord_parse_recipe(const char *recipe, ChordRecipe *out) {
  if (out == nullptr) return false;
  out->count = 0;
  out->skip_count = 0;
  out->overflow_skips = false;
  out->failed = false;
  if (recipe == nullptr) return false;

  const char *p = recipe;
  while (*p != '\0') {
    while (*p == ' ') ++p;
    if (*p == '\0') break;
    const char *start = p;
    while (*p != '\0' && *p != ' ') ++p;
    const size_t len = static_cast<size_t>(p - start);
    if (len == 0 || len >= 24) {
      add_skip(out, nullptr, len, true);
      continue;
    }
    if (out->count >= kChordRecipeCap) {
      out->failed = true;
      return true;
    }
    char token[24];
    memcpy(token, start, len);
    token[len] = '\0';
    bool abort_recipe = false;
    take_token(out, token, &abort_recipe);
    if (abort_recipe) {
      out->failed = true;
      return true;
    }
  }
  return true;
}
