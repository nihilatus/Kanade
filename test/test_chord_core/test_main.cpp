#include "chord_core.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <unity.h>

static ChordRule g_rules[256];
static char g_pool[64 * 1024];
static size_t g_pool_used = 0;
static size_t g_count = 0;
static char g_error[240];

static void set_error(const char *msg) { snprintf(g_error, sizeof(g_error), "%s", msg); }

static bool pool_add(const char *src, const char **out) {
  const size_t n = strlen(src);
  if (g_pool_used + n + 1 > sizeof(g_pool)) return false;
  char *dest = g_pool + g_pool_used;
  memcpy(dest, src, n + 1);
  g_pool_used += n + 1;
  *out = dest;
  return true;
}

static bool mask_of(const char *field, uint16_t *mask) {
  *mask = 0;
  if (field[0] == '\0') return true;
  const char *p = field;
  while (*p != '\0') {
    char name[8];
    size_t n = 0;
    while (*p != '\0' && *p != '+') {
      if (n + 1 >= sizeof(name)) return false;
      name[n++] = *p++;
    }
    name[n] = '\0';
    if (*p == '+') ++p;
    if (name[0] == '\0') return false;
    const int index = chord_key_index(name);
    if (index < 0) return false;
    const uint16_t bit = static_cast<uint16_t>(1u << index);
    if ((*mask & bit) != 0) return false;
    *mask = static_cast<uint16_t>(*mask | bit);
  }
  return true;
}

static bool read_field(const char **pp, char *dest, size_t cap) {
  const char *p = *pp;
  size_t n = 0;
  if (*p == '"') {
    ++p;
    while (*p != '\0') {
      if (*p == '"') {
        if (p[1] == '"') {
          if (n + 1 >= cap) return false;
          dest[n++] = '"';
          p += 2;
          continue;
        }
        ++p;
        break;
      }
      if (n + 1 >= cap) return false;
      dest[n++] = *p++;
    }
  } else {
    while (*p != '\0' && *p != ',' && *p != '\n' && *p != '\r') {
      if (n + 1 >= cap) return false;
      dest[n++] = *p++;
    }
  }
  dest[n] = '\0';
  *pp = p;
  return true;
}

static const ChordRule *rule_by_id(uint16_t id) {
  for (size_t i = 0; i < g_count; ++i) {
    if (g_rules[i].id == id) return &g_rules[i];
  }
  return nullptr;
}

static bool load_csv() {
  g_count = 0;
  g_pool_used = 0;
  g_error[0] = '\0';

  static const char *kPaths[] = {
      "data/chord_map.machine.csv",
      "../data/chord_map.machine.csv",
      "../../data/chord_map.machine.csv",
      "../../../data/chord_map.machine.csv",
  };
  FILE *file = nullptr;
  for (const char *path : kPaths) {
    file = fopen(path, "rb");
    if (file != nullptr) break;
  }
  if (file == nullptr) {
    set_error("could not open data/chord_map.machine.csv");
    return false;
  }
  if (fseek(file, 0, SEEK_END) != 0) {
    fclose(file);
    set_error("could not read chord map");
    return false;
  }
  const long bytes = ftell(file);
  if (bytes < 0 || bytes > 512 * 1024) {
    fclose(file);
    set_error("chord map has an unexpected size");
    return false;
  }
  if (fseek(file, 0, SEEK_SET) != 0) {
    fclose(file);
    set_error("could not read chord map");
    return false;
  }
  char *text = static_cast<char *>(malloc(static_cast<size_t>(bytes) + 1));
  if (text == nullptr) {
    fclose(file);
    set_error("out of memory");
    return false;
  }
  const size_t got = fread(text, 1, static_cast<size_t>(bytes), file);
  fclose(file);
  text[got] = '\0';

  const char *p = text;
  if (got >= 3 && static_cast<unsigned char>(text[0]) == 0xEF &&
      static_cast<unsigned char>(text[1]) == 0xBB &&
      static_cast<unsigned char>(text[2]) == 0xBF) {
    p += 3;
  }

  bool header_seen = false;
  while (*p != '\0') {
    char fields[6][160];
    int nfields = 0;
    while (nfields < 6 && *p != '\0' && *p != '\n' && *p != '\r') {
      if (!read_field(&p, fields[nfields], sizeof(fields[nfields]))) {
        free(text);
        set_error("a csv field is too long");
        return false;
      }
      ++nfields;
      if (*p == ',') ++p;
    }
    if (*p == '\r') ++p;
    if (*p == '\n') ++p;
    if (nfields == 0) continue;
    if (!header_seen && strcmp(fields[0], "id") == 0) {
      header_seen = true;
      continue;
    }
    if (fields[0][0] == '\0') continue;
    if (nfields < 4) {
      free(text);
      set_error("a csv row is missing columns");
      return false;
    }
    if (g_count >= sizeof(g_rules) / sizeof(g_rules[0])) {
      free(text);
      set_error("too many csv rows");
      return false;
    }
    char *end = nullptr;
    const long id = strtol(fields[0], &end, 10);
    if (end == fields[0] || *end != '\0' || id < 1 || id > 65535) {
      free(text);
      snprintf(g_error, sizeof(g_error), "bad id [%s]", fields[0]);
      return false;
    }
    uint16_t required = 0;
    uint16_t optional = 0;
    const char *optional_field = nfields >= 5 ? fields[4] : "";
    if (!mask_of(fields[3], &required) || !mask_of(optional_field, &optional)) {
      free(text);
      snprintf(g_error, sizeof(g_error), "bad keys on id %ld", id);
      return false;
    }
    const char *kana = "";
    const char *output = "";
    if (!pool_add(fields[1], &kana) || !pool_add(fields[2], &output)) {
      free(text);
      set_error("string pool full");
      return false;
    }
    ChordRule &row = g_rules[g_count++];
    row.id = static_cast<uint16_t>(id);
    row.required = required;
    row.optional = optional;
    row.kana = kana;
    row.output = output;
  }
  free(text);
  if (!header_seen || g_count == 0) {
    set_error("chord map has no rows");
    return false;
  }
  return true;
}

// Spec oracle. Deliberately not chord_find_rule: larger required set, then
// tighter optional set, then smaller id.
static int spec_popcount(uint16_t value) {
  int n = 0;
  for (int bit = 0; bit < 16; ++bit) {
    if ((value & static_cast<uint16_t>(1u << bit)) != 0) ++n;
  }
  return n;
}

static int spec_find(uint16_t mask) {
  int best = -1;
  int best_req = -1;
  int best_opt = 1000;
  unsigned best_id = 100000;
  for (size_t i = 0; i < g_count; ++i) {
    const ChordRule &row = g_rules[i];
    const bool covers = (row.required & mask) == row.required;
    const uint16_t allowed = static_cast<uint16_t>(row.required | row.optional);
    const bool inside = (mask & static_cast<uint16_t>(~allowed)) == 0;
    if (!covers || !inside) continue;
    const int req_n = spec_popcount(row.required);
    const int opt_n = spec_popcount(row.optional);
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

static void check_mask(uint16_t src_id, uint16_t mask) {
  const int expected = spec_find(mask);
  const int got = chord_find_rule(g_rules, g_count, mask);
  char msg[180];
  snprintf(msg, sizeof(msg), "csv id %u mask 0x%04X core %d spec %d",
           static_cast<unsigned>(src_id), mask, got, expected);
  TEST_ASSERT_EQUAL_INT_MESSAGE(expected, got, msg);
  if (expected < 0) return;
  snprintf(msg, sizeof(msg), "csv id %u mask 0x%04X winner output",
           static_cast<unsigned>(src_id), mask);
  TEST_ASSERT_EQUAL_UINT16_MESSAGE(g_rules[expected].id, g_rules[got].id, msg);
  TEST_ASSERT_EQUAL_STRING_MESSAGE(g_rules[expected].output, g_rules[got].output, msg);
}

static ChordEvent ev_write(char c) {
  ChordEvent event;
  event.op = ChordEvOp::Write;
  event.key = ChordKeyKind::Char;
  event.code = static_cast<uint8_t>(c);
  event.gap_after_ms = 12;
  return event;
}

static ChordEvent ev_press(ChordKeyKind key, uint8_t code, uint8_t gap) {
  ChordEvent event;
  event.op = ChordEvOp::Press;
  event.key = key;
  event.code = code;
  event.gap_after_ms = gap;
  return event;
}

static ChordEvent ev_release(uint8_t gap) {
  ChordEvent event;
  event.op = ChordEvOp::ReleaseAll;
  event.key = ChordKeyKind::Char;
  event.code = 0;
  event.gap_after_ms = gap;
  return event;
}

static void expect_events(const char *label, const ChordEvent *want, size_t count) {
  ChordRecipe got;
  TEST_ASSERT_TRUE_MESSAGE(chord_parse_recipe(label, &got), label);
  TEST_ASSERT_FALSE_MESSAGE(got.failed, label);
  TEST_ASSERT_EQUAL_INT_MESSAGE(0, static_cast<int>(got.skip_count), label);
  TEST_ASSERT_EQUAL_INT_MESSAGE(static_cast<int>(count), static_cast<int>(got.count), label);
  for (size_t i = 0; i < count; ++i) {
    char msg[80];
    snprintf(msg, sizeof(msg), "%s [%u]", label, static_cast<unsigned>(i));
    TEST_ASSERT_EQUAL_INT_MESSAGE(static_cast<int>(want[i].op), static_cast<int>(got.events[i].op), msg);
    TEST_ASSERT_EQUAL_INT_MESSAGE(static_cast<int>(want[i].key), static_cast<int>(got.events[i].key),
                                  msg);
    TEST_ASSERT_EQUAL_INT_MESSAGE(want[i].code, got.events[i].code, msg);
    TEST_ASSERT_EQUAL_INT_MESSAGE(want[i].gap_after_ms, got.events[i].gap_after_ms, msg);
  }
}

extern "C" void setUp(void) {}
extern "C" void tearDown(void) {}

extern "C" void test_key_order(void) {
  static const char *kOrder[] = {"LL", "LU", "LR", "LD", "LT0", "LT1", "LT2",
                                 "RL", "RU", "RR", "RD", "RT0", "RT1", "RT2"};
  TEST_ASSERT_EQUAL_INT(14, kChordKeyCount);
  for (int i = 0; i < kChordKeyCount; ++i) {
    TEST_ASSERT_EQUAL_STRING(kOrder[i], chord_key_name(i));
    TEST_ASSERT_EQUAL_INT(i, chord_key_index(kOrder[i]));
  }
  TEST_ASSERT_EQUAL_INT(-1, chord_key_index("nope"));
  TEST_ASSERT_NULL(chord_key_name(-1));
  TEST_ASSERT_NULL(chord_key_name(kChordKeyCount));
}

extern "C" void test_csv_bits_match_firmware_table(void) {
  TEST_ASSERT_TRUE_MESSAGE(load_csv(), g_error);
  const ChordRule *n = rule_by_id(1);
  const ChordRule *tsu = rule_by_id(2);
  const ChordRule *backspace = rule_by_id(26);
  const ChordRule *ta = rule_by_id(50);
  const ChordRule *comma = rule_by_id(24);
  TEST_ASSERT_NOT_NULL(n);
  TEST_ASSERT_NOT_NULL(tsu);
  TEST_ASSERT_NOT_NULL(backspace);
  TEST_ASSERT_NOT_NULL(ta);
  TEST_ASSERT_NOT_NULL(comma);
  TEST_ASSERT_EQUAL_UINT16(0x0020, n->required);
  TEST_ASSERT_EQUAL_UINT16(0x0000, n->optional);
  TEST_ASSERT_EQUAL_STRING("nn", n->output);
  TEST_ASSERT_EQUAL_UINT16(0x1000, tsu->required);
  TEST_ASSERT_EQUAL_STRING("ltu", tsu->output);
  TEST_ASSERT_EQUAL_UINT16(0x0040, backspace->required);
  TEST_ASSERT_EQUAL_STRING("Backspace", backspace->output);
  TEST_ASSERT_EQUAL_UINT16(0x0008, ta->required);
  TEST_ASSERT_EQUAL_UINT16(0x0800, ta->optional);
  TEST_ASSERT_EQUAL_STRING("ta", ta->output);
  TEST_ASSERT_EQUAL_STRING(",", comma->output);
}

extern "C" void test_each_csv_row_matches_spec(void) {
  TEST_ASSERT_TRUE_MESSAGE(g_count > 0, g_error);
  TEST_ASSERT_EQUAL_INT(-1, chord_find_rule(nullptr, 1, 0x1));
  for (size_t i = 0; i < g_count; ++i) {
    const ChordRule &row = g_rules[i];
    check_mask(row.id, row.required);
    if (row.optional != 0) check_mask(row.id, static_cast<uint16_t>(row.required | row.optional));
  }
  const int empty = chord_find_rule(g_rules, g_count, 0);
  TEST_ASSERT_EQUAL_INT(-1, spec_find(0));
  TEST_ASSERT_EQUAL_INT(-1, empty);
  const uint16_t all = static_cast<uint16_t>((1u << kChordKeyCount) - 1);
  TEST_ASSERT_EQUAL_INT(spec_find(all), chord_find_rule(g_rules, g_count, all));
}

extern "C" void test_snapshot_keeps_largest_then_later(void) {
  ChordPeak peak;
  chord_peak_clear(&peak);
  chord_note_snapshot(&peak, 0x0001);
  TEST_ASSERT_EQUAL_UINT16(0x0001, peak.mask);
  TEST_ASSERT_EQUAL_UINT8(1, peak.count);
  chord_note_snapshot(&peak, 0x0001);
  TEST_ASSERT_EQUAL_UINT16(0x0001, peak.mask);
  chord_note_snapshot(&peak, 0x0002);
  TEST_ASSERT_EQUAL_UINT16(0x0002, peak.mask);
  TEST_ASSERT_EQUAL_UINT8(1, peak.count);
  chord_note_snapshot(&peak, 0x0003);
  TEST_ASSERT_EQUAL_UINT16(0x0003, peak.mask);
  TEST_ASSERT_EQUAL_UINT8(2, peak.count);
  chord_note_snapshot(&peak, 0x0001);
  TEST_ASSERT_EQUAL_UINT16(0x0003, peak.mask);
}

extern "C" void test_release_mode_waits_ten_ms(void) {
  TEST_ASSERT_EQUAL_INT(10, static_cast<int>(kChordReleaseGuardMs));
  ChordEngine engine;
  chord_engine_reset(&engine);

  ChordStep step = chord_engine_step(&engine, 0, 0);
  TEST_ASSERT_FALSE(step.fire);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(ChordPhase::Idle), static_cast<int>(engine.phase));

  step = chord_engine_step(&engine, 0x0001, 1000);
  TEST_ASSERT_FALSE(step.fire);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(ChordPhase::Holding), static_cast<int>(engine.phase));
  TEST_ASSERT_EQUAL_UINT16(0x0001, engine.peak.mask);

  step = chord_engine_step(&engine, 0, 1500);
  TEST_ASSERT_FALSE(step.fire);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(ChordPhase::ReleaseGuard), static_cast<int>(engine.phase));
  TEST_ASSERT_EQUAL_UINT16(0x0001, engine.peak.mask);

  step = chord_engine_step(&engine, 0, 1509);
  TEST_ASSERT_FALSE(step.fire);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(ChordPhase::ReleaseGuard), static_cast<int>(engine.phase));

  step = chord_engine_step(&engine, 0, 1510);
  TEST_ASSERT_TRUE(step.fire);
  TEST_ASSERT_EQUAL_UINT16(0x0001, step.fire_mask);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(ChordPhase::Idle), static_cast<int>(engine.phase));
  TEST_ASSERT_EQUAL_UINT8(0, engine.peak.count);

  chord_engine_reset(&engine);
  chord_engine_step(&engine, 0x0001, 0);
  chord_engine_step(&engine, 0, 100);
  step = chord_engine_step(&engine, 0x0005, 105);
  TEST_ASSERT_FALSE(step.fire);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(ChordPhase::Holding), static_cast<int>(engine.phase));
  TEST_ASSERT_EQUAL_UINT16(0x0005, engine.peak.mask);

  engine.phase = ChordPhase::ReleaseGuard;
  engine.guard_since = 0;
  chord_peak_clear(&engine.peak);
  step = chord_engine_step(&engine, 0, 10);
  TEST_ASSERT_FALSE(step.fire);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(ChordPhase::Idle), static_cast<int>(engine.phase));
}

extern "C" void test_first_up_fires_then_drains(void) {
  ChordEngine engine;
  chord_engine_reset(&engine);
  engine.mode = ChordFireMode::FirstUp;

  ChordStep step = chord_engine_step(&engine, 0x0001, 0);
  TEST_ASSERT_FALSE(step.fire);
  step = chord_engine_step(&engine, 0x0003, 1);
  TEST_ASSERT_FALSE(step.fire);
  TEST_ASSERT_EQUAL_UINT16(0x0003, engine.peak.mask);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(ChordPhase::Holding), static_cast<int>(engine.phase));

  step = chord_engine_step(&engine, 0x0002, 2);
  TEST_ASSERT_TRUE(step.fire);
  TEST_ASSERT_EQUAL_UINT16(0x0003, step.fire_mask);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(ChordPhase::Draining), static_cast<int>(engine.phase));

  step = chord_engine_step(&engine, 0x0007, 3);
  TEST_ASSERT_FALSE(step.fire);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(ChordPhase::Draining), static_cast<int>(engine.phase));
  TEST_ASSERT_EQUAL_UINT8(0, engine.peak.count);

  step = chord_engine_step(&engine, 0, 4);
  TEST_ASSERT_FALSE(step.fire);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(ChordPhase::Idle), static_cast<int>(engine.phase));

  step = chord_engine_step(&engine, 0x0001, 5);
  TEST_ASSERT_FALSE(step.fire);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(ChordPhase::Holding), static_cast<int>(engine.phase));
  step = chord_engine_step(&engine, 0, 6);
  TEST_ASSERT_TRUE(step.fire);
  TEST_ASSERT_EQUAL_UINT16(0x0001, step.fire_mask);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(ChordPhase::Idle), static_cast<int>(engine.phase));
}

extern "C" void test_switch_to_first_during_guard_sends(void) {
  ChordEngine engine;
  chord_engine_reset(&engine);
  chord_engine_step(&engine, 0x0005, 0);
  chord_engine_step(&engine, 0, 20);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(ChordPhase::ReleaseGuard), static_cast<int>(engine.phase));

  ChordStep step = chord_engine_set_mode(&engine, ChordFireMode::ReleaseAll);
  TEST_ASSERT_FALSE(step.fire);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(ChordPhase::ReleaseGuard), static_cast<int>(engine.phase));

  step = chord_engine_set_mode(&engine, ChordFireMode::FirstUp);
  TEST_ASSERT_TRUE(step.fire);
  TEST_ASSERT_EQUAL_UINT16(0x0005, step.fire_mask);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(ChordPhase::Idle), static_cast<int>(engine.phase));
  TEST_ASSERT_EQUAL_INT(static_cast<int>(ChordFireMode::FirstUp), static_cast<int>(engine.mode));

  step = chord_engine_step(&engine, 0, 30);
  TEST_ASSERT_FALSE(step.fire);

  chord_engine_reset(&engine);
  chord_engine_step(&engine, 0x0001, 0);
  step = chord_engine_set_mode(&engine, ChordFireMode::FirstUp);
  TEST_ASSERT_FALSE(step.fire);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(ChordPhase::Holding), static_cast<int>(engine.phase));
}

extern "C" void test_recipe_shapes(void) {
  TEST_ASSERT_EQUAL_INT(12, static_cast<int>(kChordTapGapMs));
  TEST_ASSERT_FALSE(chord_parse_recipe(nullptr, nullptr));

  const ChordEvent a[] = {ev_write('a'), ev_release(0)};
  expect_events("a", a, 2);
  const ChordEvent nn[] = {ev_write('n'), ev_write('n'), ev_release(0)};
  expect_events("nn", nn, 3);
  const ChordEvent kome[] = {ev_write('k'), ev_write('o'), ev_write('m'), ev_write('e'), ev_release(0)};
  expect_events("kome", kome, 5);

  struct Named {
    const char *text;
    ChordKeyKind kind;
  };
  const Named named[] = {
      {"Backspace", ChordKeyKind::Backspace}, {"Space", ChordKeyKind::Space},
      {"Enter", ChordKeyKind::Enter},         {"[ESC]", ChordKeyKind::Esc},
      {"Up", ChordKeyKind::Up},               {"Down", ChordKeyKind::Down},
      {"Left", ChordKeyKind::Left},           {"Right", ChordKeyKind::Right},
  };
  for (const Named &item : named) {
    const ChordEvent want[] = {ev_press(item.kind, 0, 12), ev_release(12)};
    expect_events(item.text, want, 2);
  }

  const ChordEvent shift1[] = {ev_press(ChordKeyKind::Shift, 0, 0),
                               ev_press(ChordKeyKind::Char, '1', 12), ev_release(12)};
  expect_events("Shift+1", shift1, 3);
  const ChordEvent ctrli[] = {ev_press(ChordKeyKind::Ctrl, 0, 0), ev_press(ChordKeyKind::Char, 'i', 12),
                              ev_release(12)};
  expect_events("Ctrl+i", ctrli, 3);

  const ChordEvent spaced[] = {ev_write('z'), ev_release(0), ev_write('.'), ev_release(0),
                               ev_write('z'), ev_release(0), ev_write('.'), ev_release(0)};
  expect_events("z . z .", spaced, 8);
  const ChordEvent zbracket[] = {ev_write('z'), ev_release(0), ev_write('['), ev_release(0)};
  expect_events("z [", zbracket, 4);

  const ChordEvent plus[] = {ev_write('+'), ev_release(0)};
  expect_events("+", plus, 2);
  const ChordEvent shift_plus[] = {ev_press(ChordKeyKind::Shift, 0, 0),
                                   ev_press(ChordKeyKind::Char, '+', 12), ev_release(12)};
  expect_events("Shift++", shift_plus, 3);

  struct RawKey {
    const char *text;
    uint8_t usage;
  };
  const RawKey raw[] = {
      {"Hankaku", 0x35}, {"Henkan", 0x8A},  {"Muhenkan", 0x8B}, {"Kana", 0x88},
      {"Eisuu", 0x91},   {"Yen", 0x89},     {"Ro", 0x87},       {"Delete", 0x4C},
      {"Insert", 0x49},  {"Home", 0x4A},    {"End", 0x4D},      {"PageUp", 0x4B},
      {"PageDown", 0x4E}, {"CapsLock", 0x39}, {"F1", 0x3A},     {"F2", 0x3B},
      {"F3", 0x3C},      {"F4", 0x3D},      {"F5", 0x3E},       {"F6", 0x3F},
      {"F7", 0x40},      {"F8", 0x41},      {"F9", 0x42},       {"F10", 0x43},
      {"F11", 0x44},     {"F12", 0x45},
  };
  for (const RawKey &item : raw) {
    const ChordEvent want[] = {ev_press(ChordKeyKind::Raw, item.usage, 12), ev_release(12)};
    expect_events(item.text, want, 2);
  }
  const ChordEvent shift_henkan[] = {ev_press(ChordKeyKind::Shift, 0, 0),
                                     ev_press(ChordKeyKind::Raw, 0x8A, 12), ev_release(12)};
  expect_events("Shift+Henkan", shift_henkan, 3);
  const ChordEvent ctrl_f1[] = {ev_press(ChordKeyKind::Ctrl, 0, 0),
                                ev_press(ChordKeyKind::Raw, 0x3A, 12), ev_release(12)};
  expect_events("Ctrl+F1", ctrl_f1, 3);
}

extern "C" void test_each_csv_output_parses(void) {
  TEST_ASSERT_TRUE_MESSAGE(g_count > 0, g_error);
  for (size_t i = 0; i < g_count; ++i) {
    const ChordRule &row = g_rules[i];
    ChordRecipe got;
    char msg[160];
    snprintf(msg, sizeof(msg), "id %u output [%s]", static_cast<unsigned>(row.id), row.output);
    TEST_ASSERT_TRUE_MESSAGE(chord_parse_recipe(row.output, &got), msg);
    TEST_ASSERT_FALSE_MESSAGE(got.failed, msg);
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, static_cast<int>(got.skip_count), msg);
    if (row.output[0] != '\0') TEST_ASSERT_TRUE_MESSAGE(got.count > 0, msg);
  }
}

int main(int argc, char **argv) {
  (void)argc;
  (void)argv;
  UNITY_BEGIN();
  RUN_TEST(test_key_order);
  RUN_TEST(test_snapshot_keeps_largest_then_later);
  RUN_TEST(test_release_mode_waits_ten_ms);
  RUN_TEST(test_first_up_fires_then_drains);
  RUN_TEST(test_switch_to_first_during_guard_sends);
  RUN_TEST(test_recipe_shapes);
  RUN_TEST(test_csv_bits_match_firmware_table);
  RUN_TEST(test_each_csv_row_matches_spec);
  RUN_TEST(test_each_csv_output_parses);
  return UNITY_END();
}
