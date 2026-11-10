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
  TEST_ASSERT_FALSE_MESSAGE(got.overflow_skips, label);
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

extern "C" void test_both_modes_send_a_chord_once(void) {
  const uint16_t chord = 0x0003;

  ChordEngine release;
  chord_engine_reset(&release);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(ChordFireMode::ReleaseAll), static_cast<int>(release.mode));

  ChordStep step = chord_engine_step(&release, chord, 0);
  TEST_ASSERT_FALSE(step.fire);
  step = chord_engine_step(&release, chord, 8);
  TEST_ASSERT_FALSE(step.fire);
  TEST_ASSERT_EQUAL_UINT16(chord, release.peak.mask);

  step = chord_engine_step(&release, 0, 20);
  TEST_ASSERT_FALSE(step.fire);
  step = chord_engine_step(&release, 0, 20 + kChordReleaseGuardMs - 1);
  TEST_ASSERT_FALSE(step.fire);
  step = chord_engine_step(&release, 0, 20 + kChordReleaseGuardMs);
  TEST_ASSERT_TRUE(step.fire);
  TEST_ASSERT_EQUAL_UINT16(chord, step.fire_mask);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(ChordPhase::Idle), static_cast<int>(release.phase));
  step = chord_engine_step(&release, 0, 40);
  TEST_ASSERT_FALSE(step.fire);

  step = chord_engine_step(&release, chord, 50);
  TEST_ASSERT_FALSE(step.fire);
  step = chord_engine_step(&release, 0, 60);
  TEST_ASSERT_FALSE(step.fire);
  step = chord_engine_step(&release, 0, 60 + kChordReleaseGuardMs);
  TEST_ASSERT_TRUE(step.fire);
  TEST_ASSERT_EQUAL_UINT16(chord, step.fire_mask);
  step = chord_engine_step(&release, 0, 80);
  TEST_ASSERT_FALSE(step.fire);

  ChordEngine first;
  chord_engine_reset(&first);
  first.mode = ChordFireMode::FirstUp;

  step = chord_engine_step(&first, chord, 0);
  TEST_ASSERT_FALSE(step.fire);
  step = chord_engine_step(&first, chord, 8);
  TEST_ASSERT_FALSE(step.fire);
  step = chord_engine_step(&first, 0x0001, 9);
  TEST_ASSERT_TRUE(step.fire);
  TEST_ASSERT_EQUAL_UINT16(chord, step.fire_mask);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(ChordPhase::Draining), static_cast<int>(first.phase));
  step = chord_engine_step(&first, 0x0001, 10);
  TEST_ASSERT_FALSE(step.fire);
  step = chord_engine_step(&first, 0, 11);
  TEST_ASSERT_FALSE(step.fire);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(ChordPhase::Idle), static_cast<int>(first.phase));

  step = chord_engine_step(&first, 0x0004, 20);
  TEST_ASSERT_FALSE(step.fire);
  step = chord_engine_step(&first, 0, 21);
  TEST_ASSERT_TRUE(step.fire);
  TEST_ASSERT_EQUAL_UINT16(0x0004, step.fire_mask);
  step = chord_engine_step(&first, 0, 22);
  TEST_ASSERT_FALSE(step.fire);
}

extern "C" void test_bounce_within_debounce_does_not_split(void) {
  TEST_ASSERT_EQUAL_INT(8, static_cast<int>(kChordDebounceMs));
  TEST_ASSERT_EQUAL_INT(10, static_cast<int>(kChordBounceMs));
  TEST_ASSERT_TRUE(kChordBounceMs > kChordDebounceMs);
  TEST_ASSERT_TRUE(kChordReleaseGuardMs >= kChordBounceMs);
  TEST_ASSERT_TRUE(kChordReleaseGuardMs > kChordDebounceMs);

  ChordEngine engine;
  chord_engine_reset(&engine);
  const uint16_t chord = 0x0003;

  ChordStep step = chord_engine_step(&engine, chord, 0);
  TEST_ASSERT_FALSE(step.fire);

  const uint32_t up = 1000;
  step = chord_engine_step(&engine, 0, up);
  TEST_ASSERT_FALSE(step.fire);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(ChordPhase::ReleaseGuard), static_cast<int>(engine.phase));

  step = chord_engine_step(&engine, chord, up + kChordDebounceMs);
  TEST_ASSERT_FALSE(step.fire);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(ChordPhase::Holding), static_cast<int>(engine.phase));
  TEST_ASSERT_EQUAL_UINT16(chord, engine.peak.mask);

  const uint32_t up2 = up + kChordDebounceMs + 100;
  step = chord_engine_step(&engine, 0, up2);
  TEST_ASSERT_FALSE(step.fire);
  step = chord_engine_step(&engine, 0, up2 + kChordReleaseGuardMs - 1);
  TEST_ASSERT_FALSE(step.fire);
  step = chord_engine_step(&engine, 0, up2 + kChordReleaseGuardMs);
  TEST_ASSERT_TRUE(step.fire);
  TEST_ASSERT_EQUAL_UINT16(chord, step.fire_mask);
  step = chord_engine_step(&engine, 0, up2 + kChordReleaseGuardMs + 1);
  TEST_ASSERT_FALSE(step.fire);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(ChordPhase::Idle), static_cast<int>(engine.phase));
}

extern "C" void test_repress_inside_guard_merges(void) {
  ChordEngine engine;
  chord_engine_reset(&engine);

  ChordStep step = chord_engine_step(&engine, 0x0003, 0);
  TEST_ASSERT_FALSE(step.fire);
  const uint32_t up = 100;
  step = chord_engine_step(&engine, 0, up);
  TEST_ASSERT_FALSE(step.fire);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(ChordPhase::ReleaseGuard), static_cast<int>(engine.phase));
  TEST_ASSERT_EQUAL_UINT16(0x0003, engine.peak.mask);

  step = chord_engine_step(&engine, 0x0007, up + kChordReleaseGuardMs - 1);
  TEST_ASSERT_FALSE(step.fire);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(ChordPhase::Holding), static_cast<int>(engine.phase));
  TEST_ASSERT_EQUAL_UINT16(0x0007, engine.peak.mask);

  const uint32_t up2 = up + 50;
  step = chord_engine_step(&engine, 0, up2);
  TEST_ASSERT_FALSE(step.fire);
  step = chord_engine_step(&engine, 0, up2 + kChordReleaseGuardMs);
  TEST_ASSERT_TRUE(step.fire);
  TEST_ASSERT_EQUAL_UINT16(0x0007, step.fire_mask);
  step = chord_engine_step(&engine, 0, up2 + kChordReleaseGuardMs + 5);
  TEST_ASSERT_FALSE(step.fire);
}

// あ (RT0) is a subset of か (LU + optional RT0). After the bounce window that
// nesting is the next chord, in either order. One millisecond earlier stays
// one peak, which is the contact-return case.
extern "C" void test_nested_chord_after_bounce_window_sends_both(void) {
  const uint16_t a = 0x0800;
  const uint16_t ka = 0x0802;
  const uint32_t up = 100;

  ChordEngine engine;
  chord_engine_reset(&engine);
  chord_engine_step(&engine, ka, 0);
  chord_engine_step(&engine, 0, up);
  ChordStep step = chord_engine_step(&engine, a, up + kChordBounceMs - 1);
  TEST_ASSERT_FALSE(step.fire);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(ChordPhase::Holding), static_cast<int>(engine.phase));
  TEST_ASSERT_EQUAL_UINT16(ka, engine.peak.mask);

  chord_engine_reset(&engine);
  chord_engine_step(&engine, ka, 0);
  chord_engine_step(&engine, 0, up);
  step = chord_engine_step(&engine, a, up + kChordBounceMs);
  TEST_ASSERT_TRUE(step.fire);
  TEST_ASSERT_EQUAL_UINT16(ka, step.fire_mask);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(ChordPhase::Holding), static_cast<int>(engine.phase));
  TEST_ASSERT_EQUAL_UINT16(a, engine.peak.mask);

  const uint32_t up_a = up + kChordBounceMs + 40;
  step = chord_engine_step(&engine, 0, up_a);
  TEST_ASSERT_FALSE(step.fire);
  step = chord_engine_step(&engine, 0, up_a + kChordReleaseGuardMs);
  TEST_ASSERT_TRUE(step.fire);
  TEST_ASSERT_EQUAL_UINT16(a, step.fire_mask);

  chord_engine_reset(&engine);
  chord_engine_step(&engine, a, 0);
  chord_engine_step(&engine, 0, up);
  step = chord_engine_step(&engine, ka, up + kChordBounceMs);
  TEST_ASSERT_TRUE(step.fire);
  TEST_ASSERT_EQUAL_UINT16(a, step.fire_mask);
  TEST_ASSERT_EQUAL_UINT16(ka, engine.peak.mask);

  chord_engine_reset(&engine);
  chord_engine_step(&engine, ka, 0);
  chord_engine_step(&engine, 0, up);
  step = chord_engine_step(&engine, a, up + 40);
  TEST_ASSERT_TRUE(step.fire);
  TEST_ASSERT_EQUAL_UINT16(ka, step.fire_mask);
  TEST_ASSERT_EQUAL_UINT16(a, engine.peak.mask);
}

extern "C" void test_different_chord_inside_guard_sends_both(void) {
  ChordEngine engine;
  chord_engine_reset(&engine);

  const uint16_t ab = 0x0003;
  const uint16_t c = 0x0004;

  ChordStep step = chord_engine_step(&engine, ab, 0);
  TEST_ASSERT_FALSE(step.fire);
  const uint32_t up = 100;
  step = chord_engine_step(&engine, 0, up);
  TEST_ASSERT_FALSE(step.fire);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(ChordPhase::ReleaseGuard), static_cast<int>(engine.phase));
  TEST_ASSERT_EQUAL_UINT16(ab, engine.peak.mask);
  TEST_ASSERT_EQUAL_UINT8(2, engine.peak.count);

  const uint32_t press_c = up + kChordReleaseGuardMs - 1;
  step = chord_engine_step(&engine, c, press_c);
  TEST_ASSERT_TRUE(step.fire);
  TEST_ASSERT_EQUAL_UINT16(ab, step.fire_mask);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(ChordPhase::Holding), static_cast<int>(engine.phase));
  TEST_ASSERT_EQUAL_UINT16(c, engine.peak.mask);
  TEST_ASSERT_EQUAL_UINT8(1, engine.peak.count);

  const uint32_t up_c = press_c + 40;
  step = chord_engine_step(&engine, 0, up_c);
  TEST_ASSERT_FALSE(step.fire);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(ChordPhase::ReleaseGuard), static_cast<int>(engine.phase));
  TEST_ASSERT_EQUAL_UINT16(c, engine.peak.mask);
  step = chord_engine_step(&engine, 0, up_c + kChordReleaseGuardMs - 1);
  TEST_ASSERT_FALSE(step.fire);
  step = chord_engine_step(&engine, 0, up_c + kChordReleaseGuardMs);
  TEST_ASSERT_TRUE(step.fire);
  TEST_ASSERT_EQUAL_UINT16(c, step.fire_mask);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(ChordPhase::Idle), static_cast<int>(engine.phase));
  step = chord_engine_step(&engine, 0, up_c + kChordReleaseGuardMs + 1);
  TEST_ASSERT_FALSE(step.fire);
}

extern "C" void test_first_up_draining_ignores_presses(void) {
  ChordEngine engine;
  chord_engine_reset(&engine);
  engine.mode = ChordFireMode::FirstUp;

  ChordStep step = chord_engine_step(&engine, 0x0003, 0);
  TEST_ASSERT_FALSE(step.fire);
  step = chord_engine_step(&engine, 0x0002, 1);
  TEST_ASSERT_TRUE(step.fire);
  TEST_ASSERT_EQUAL_UINT16(0x0003, step.fire_mask);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(ChordPhase::Draining), static_cast<int>(engine.phase));
  TEST_ASSERT_EQUAL_UINT8(0, engine.peak.count);

  step = chord_engine_step(&engine, 0x0005, 2);
  TEST_ASSERT_FALSE(step.fire);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(ChordPhase::Draining), static_cast<int>(engine.phase));
  TEST_ASSERT_EQUAL_UINT16(0, engine.peak.mask);

  step = chord_engine_step(&engine, 0x0004, 3);
  TEST_ASSERT_FALSE(step.fire);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(ChordPhase::Draining), static_cast<int>(engine.phase));

  step = chord_engine_step(&engine, 0, 4);
  TEST_ASSERT_FALSE(step.fire);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(ChordPhase::Idle), static_cast<int>(engine.phase));

  step = chord_engine_step(&engine, 0x0004, 5);
  TEST_ASSERT_FALSE(step.fire);
  TEST_ASSERT_EQUAL_UINT16(0x0004, engine.peak.mask);
  step = chord_engine_step(&engine, 0, 6);
  TEST_ASSERT_TRUE(step.fire);
  TEST_ASSERT_EQUAL_UINT16(0x0004, step.fire_mask);
  step = chord_engine_step(&engine, 0, 7);
  TEST_ASSERT_FALSE(step.fire);
}

extern "C" void test_recipe_shapes(void) {
  TEST_ASSERT_EQUAL_INT(12, static_cast<int>(kChordTapGapMs));
  TEST_ASSERT_FALSE(chord_parse_recipe(nullptr, nullptr));
  ChordRecipe empty;
  TEST_ASSERT_FALSE(chord_parse_recipe(nullptr, &empty));
  TEST_ASSERT_FALSE(empty.failed);
  TEST_ASSERT_EQUAL_INT(0, static_cast<int>(empty.count));

  const ChordEvent a[] = {ev_write('a')};
  expect_events("a", a, 1);
  const ChordEvent nn[] = {ev_write('n'), ev_write('n')};
  expect_events("nn", nn, 2);
  const ChordEvent kome[] = {ev_write('k'), ev_write('o'), ev_write('m'), ev_write('e')};
  expect_events("kome", kome, 4);

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

  const ChordEvent spaced[] = {ev_write('z'), ev_write('.'), ev_write('z'), ev_write('.')};
  expect_events("z . z .", spaced, 4);
  const ChordEvent zbracket[] = {ev_write('z'), ev_write('[')};
  expect_events("z [", zbracket, 2);

  const ChordEvent plus[] = {ev_write('+')};
  expect_events("+", plus, 1);
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

extern "C" void test_skip_overflow_is_flagged(void) {
  char recipe[80];
  size_t n = 0;
  for (int i = 0; i < 33; ++i) {
    if (i > 0) recipe[n++] = ' ';
    recipe[n++] = '\x7f';
  }
  recipe[n] = '\0';

  ChordRecipe got;
  TEST_ASSERT_TRUE(chord_parse_recipe(recipe, &got));
  TEST_ASSERT_FALSE(got.failed);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(kChordSkipCap), static_cast<int>(got.skip_count));
  TEST_ASSERT_TRUE(got.overflow_skips);

  n = 0;
  for (int i = 0; i < static_cast<int>(kChordSkipCap); ++i) {
    if (i > 0) recipe[n++] = ' ';
    recipe[n++] = '\x7f';
  }
  recipe[n] = '\0';
  TEST_ASSERT_TRUE(chord_parse_recipe(recipe, &got));
  TEST_ASSERT_EQUAL_INT(static_cast<int>(kChordSkipCap), static_cast<int>(got.skip_count));
  TEST_ASSERT_FALSE(got.overflow_skips);
}

extern "C" void test_recipe_that_does_not_fit_fails(void) {
  char recipe[160];
  size_t n = 0;
  // 49 "aa" tokens are 98 Writes, one past the 96-event cap.
  for (int i = 0; i < 49; ++i) {
    if (i > 0) recipe[n++] = ' ';
    recipe[n++] = 'a';
    recipe[n++] = 'a';
  }
  recipe[n] = '\0';

  ChordRecipe got;
  TEST_ASSERT_FALSE(chord_parse_recipe(recipe, &got));
  TEST_ASSERT_TRUE(got.failed);
  TEST_ASSERT_EQUAL_INT(0, static_cast<int>(got.count));
  TEST_ASSERT_EQUAL_INT(0, static_cast<int>(got.skip_count));

  n = 0;
  // 47 "aa" tokens fill 94 events. Shift+a needs 3 more and does not fit.
  for (int i = 0; i < 47; ++i) {
    if (i > 0) recipe[n++] = ' ';
    recipe[n++] = 'a';
    recipe[n++] = 'a';
  }
  recipe[n++] = ' ';
  const char *combo = "Shift+a";
  memcpy(recipe + n, combo, 7);
  n += 7;
  recipe[n] = '\0';
  TEST_ASSERT_FALSE(chord_parse_recipe(recipe, &got));
  TEST_ASSERT_TRUE(got.failed);
  TEST_ASSERT_EQUAL_INT(0, static_cast<int>(got.count));
}

extern "C" void test_recipe_token_and_cap_bounds(void) {
  const ChordEvent shift_plus[] = {ev_press(ChordKeyKind::Shift, 0, 0),
                                   ev_press(ChordKeyKind::Char, '+', 12), ev_release(12)};
  expect_events("Shift++", shift_plus, 3);

  char plain23[24];
  memset(plain23, 'a', 23);
  plain23[23] = '\0';
  ChordEvent want23[23];
  for (int i = 0; i < 23; ++i) want23[i] = ev_write('a');
  expect_events(plain23, want23, 23);

  char long24[32];
  memset(long24, 'b', 24);
  long24[24] = ' ';
  long24[25] = 'a';
  long24[26] = '\0';
  ChordRecipe got;
  TEST_ASSERT_TRUE(chord_parse_recipe(long24, &got));
  TEST_ASSERT_FALSE(got.failed);
  TEST_ASSERT_FALSE(got.overflow_skips);
  TEST_ASSERT_EQUAL_INT(1, static_cast<int>(got.skip_count));
  TEST_ASSERT_TRUE(got.skips[0].too_long);
  TEST_ASSERT_EQUAL_INT(24, static_cast<int>(got.skips[0].length));
  TEST_ASSERT_EQUAL_STRING("", got.skips[0].text);
  TEST_ASSERT_EQUAL_INT(1, static_cast<int>(got.count));
  TEST_ASSERT_EQUAL_INT(static_cast<int>(ChordEvOp::Write), static_cast<int>(got.events[0].op));
  TEST_ASSERT_EQUAL_INT('a', got.events[0].code);

  char fit[200];
  size_t n = 0;
  for (int i = 0; i < static_cast<int>(kChordRecipeCap); ++i) {
    if (i > 0) fit[n++] = ' ';
    fit[n++] = 'a';
  }
  fit[n] = '\0';
  TEST_ASSERT_TRUE(chord_parse_recipe(fit, &got));
  TEST_ASSERT_FALSE(got.failed);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(kChordRecipeCap), static_cast<int>(got.count));
  TEST_ASSERT_EQUAL_INT(0, static_cast<int>(got.skip_count));
  TEST_ASSERT_EQUAL_INT('a', got.events[0].code);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(ChordEvOp::Write),
                        static_cast<int>(got.events[kChordRecipeCap - 1].op));
  TEST_ASSERT_EQUAL_INT('a', got.events[kChordRecipeCap - 1].code);

  fit[n++] = ' ';
  fit[n++] = 'a';
  fit[n] = '\0';
  TEST_ASSERT_FALSE(chord_parse_recipe(fit, &got));
  TEST_ASSERT_TRUE(got.failed);
  TEST_ASSERT_FALSE(got.overflow_skips);
  TEST_ASSERT_EQUAL_INT(0, static_cast<int>(got.skip_count));
  TEST_ASSERT_EQUAL_INT(0, static_cast<int>(got.count));

  char skips[80];
  n = 0;
  for (int i = 0; i < static_cast<int>(kChordSkipCap) + 1; ++i) {
    if (i > 0) skips[n++] = ' ';
    skips[n++] = '\x7f';
  }
  skips[n++] = ' ';
  skips[n++] = 'z';
  skips[n] = '\0';
  TEST_ASSERT_TRUE(chord_parse_recipe(skips, &got));
  TEST_ASSERT_FALSE(got.failed);
  TEST_ASSERT_TRUE(got.overflow_skips);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(kChordSkipCap), static_cast<int>(got.skip_count));
  TEST_ASSERT_FALSE(got.skips[0].too_long);
  TEST_ASSERT_EQUAL_INT(1, static_cast<int>(got.skips[0].length));
  TEST_ASSERT_EQUAL_INT(1, static_cast<int>(got.count));
  TEST_ASSERT_EQUAL_INT('z', got.events[0].code);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(ChordEvOp::Write), static_cast<int>(got.events[0].op));
}

static int hex_nibble(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  return -1;
}

static bool unescape_field(const char *src, char *dest, size_t cap, size_t *out_len) {
  size_t w = 0;
  for (size_t i = 0; src[i] != '\0'; ++i) {
    unsigned char c = static_cast<unsigned char>(src[i]);
    if (c == '\\') {
      const unsigned char mark = static_cast<unsigned char>(src[++i]);
      if (mark == '\0') return false;
      if (mark == '\\') {
        c = '\\';
      } else if (mark == 't') {
        c = '\t';
      } else if (mark == 'n') {
        c = '\n';
      } else if (mark == 'r') {
        c = '\r';
      } else if (mark == 'x') {
        const int hi = hex_nibble(src[i + 1]);
        const int lo = hex_nibble(src[i + 2]);
        if (hi < 0 || lo < 0) return false;
        c = static_cast<unsigned char>((hi << 4) | lo);
        i += 2;
      } else {
        return false;
      }
    }
    if (w + 1 >= cap) return false;
    dest[w++] = static_cast<char>(c);
  }
  dest[w] = '\0';
  *out_len = w;
  return true;
}

static bool split_tabs(char *line, char **fields) {
  fields[0] = line;
  int n = 1;
  for (char *p = line; *p != '\0'; ++p) {
    if (*p != '\t') continue;
    if (n >= 6) return false;
    *p = '\0';
    fields[n++] = p + 1;
  }
  return n == 6;
}

static bool split_limit(char *text, char delim, char **parts, int limit) {
  parts[0] = text;
  int got = 1;
  for (char *p = text; *p != '\0'; ++p) {
    if (*p != delim) continue;
    *p = '\0';
    parts[got++] = p + 1;
    if (got == limit) return true;
  }
  return got == limit;
}

static bool parse_flag(const char *text, bool *out) {
  if (strcmp(text, "0") == 0) {
    *out = false;
    return true;
  }
  if (strcmp(text, "1") == 0) {
    *out = true;
    return true;
  }
  return false;
}

static bool parse_int(const char *text, int *out, int min_value, int max_value) {
  if (text[0] == '\0') return false;
  char *end = nullptr;
  const long value = strtol(text, &end, 10);
  if (end == text || *end != '\0' || value < min_value || value > max_value) return false;
  *out = static_cast<int>(value);
  return true;
}

static bool kind_from_name(const char *name, ChordKeyKind *kind) {
  static const struct {
    const char *name;
    ChordKeyKind kind;
  } kKinds[] = {
      {"Char", ChordKeyKind::Char},     {"Shift", ChordKeyKind::Shift},
      {"Ctrl", ChordKeyKind::Ctrl},     {"Alt", ChordKeyKind::Alt},
      {"Gui", ChordKeyKind::Gui},       {"Backspace", ChordKeyKind::Backspace},
      {"Space", ChordKeyKind::Space},   {"Enter", ChordKeyKind::Enter},
      {"Esc", ChordKeyKind::Esc},       {"Up", ChordKeyKind::Up},
      {"Down", ChordKeyKind::Down},     {"Left", ChordKeyKind::Left},
      {"Right", ChordKeyKind::Right},   {"Tab", ChordKeyKind::Tab},
      {"Raw", ChordKeyKind::Raw},
  };
  for (const auto &item : kKinds) {
    if (strcmp(name, item.name) == 0) {
      *kind = item.kind;
      return true;
    }
  }
  return false;
}

static bool parse_one_event(char *text, ChordEvent *event) {
  char *parts[4];
  if (!split_limit(text, ':', parts, 4)) return false;
  if (strcmp(parts[0], "W") == 0) event->op = ChordEvOp::Write;
  else if (strcmp(parts[0], "P") == 0) event->op = ChordEvOp::Press;
  else if (strcmp(parts[0], "R") == 0) event->op = ChordEvOp::ReleaseAll;
  else return false;
  if (!kind_from_name(parts[1], &event->key)) return false;
  int code = 0;
  int gap = 0;
  if (!parse_int(parts[2], &code, 0, 255) || !parse_int(parts[3], &gap, 0, 255)) return false;
  event->code = static_cast<uint8_t>(code);
  event->gap_after_ms = static_cast<uint8_t>(gap);
  return true;
}

static bool vector_line_ok(char *line, int line_no) {
  char *fields[6];
  if (!split_tabs(line, fields)) {
    snprintf(g_error, sizeof(g_error), "line %d: expected 6 fields", line_no);
    return false;
  }
  const char *name = fields[0];
  char recipe[512];
  size_t recipe_len = 0;
  if (!unescape_field(fields[1], recipe, sizeof(recipe), &recipe_len)) {
    snprintf(g_error, sizeof(g_error), "line %d %s: bad recipe escape", line_no, name);
    return false;
  }
  bool want_failed = false;
  bool want_overflow = false;
  if (!parse_flag(fields[2], &want_failed) || !parse_flag(fields[3], &want_overflow)) {
    snprintf(g_error, sizeof(g_error), "line %d %s: bad flag", line_no, name);
    return false;
  }

  ChordEvent want_events[kChordRecipeCap];
  size_t want_count = 0;
  if (fields[4][0] != '\0') {
    char *cursor = fields[4];
    while (cursor != nullptr) {
      char *semi = strchr(cursor, ';');
      if (semi != nullptr) *semi = '\0';
      if (cursor[0] == '\0' || want_count >= kChordRecipeCap ||
          !parse_one_event(cursor, &want_events[want_count])) {
        snprintf(g_error, sizeof(g_error), "line %d %s: bad event", line_no, name);
        return false;
      }
      ++want_count;
      cursor = semi == nullptr ? nullptr : semi + 1;
    }
  }

  struct WantSkip {
    size_t length;
    bool too_long;
    char text[24];
  };
  WantSkip want_skips[kChordSkipCap];
  size_t want_skip_count = 0;
  if (fields[5][0] != '\0') {
    char *cursor = fields[5];
    while (cursor != nullptr) {
      char *semi = strchr(cursor, ';');
      if (semi != nullptr) *semi = '\0';
      char *parts[3];
      int length = 0;
      bool too_long = false;
      size_t text_len = 0;
      if (cursor[0] == '\0' || want_skip_count >= kChordSkipCap ||
          !split_limit(cursor, ':', parts, 3) || !parse_int(parts[0], &length, 0, 1000) ||
          !parse_flag(parts[1], &too_long) ||
          !unescape_field(parts[2], want_skips[want_skip_count].text,
                          sizeof(want_skips[want_skip_count].text), &text_len)) {
        snprintf(g_error, sizeof(g_error), "line %d %s: bad skip", line_no, name);
        return false;
      }
      want_skips[want_skip_count].length = static_cast<size_t>(length);
      want_skips[want_skip_count].too_long = too_long;
      ++want_skip_count;
      cursor = semi == nullptr ? nullptr : semi + 1;
    }
  }

  ChordRecipe got;
  const bool parsed_ok = chord_parse_recipe(recipe, &got);
  if (parsed_ok == want_failed || got.failed != want_failed || got.overflow_skips != want_overflow) {
    snprintf(g_error, sizeof(g_error), "line %d %s: ok %d failed %d/%d overflow %d/%d", line_no,
             name, parsed_ok ? 1 : 0, got.failed ? 1 : 0, want_failed ? 1 : 0,
             got.overflow_skips ? 1 : 0, want_overflow ? 1 : 0);
    return false;
  }
  if (got.count != want_count) {
    snprintf(g_error, sizeof(g_error), "line %d %s: event count %u want %u", line_no, name,
             static_cast<unsigned>(got.count), static_cast<unsigned>(want_count));
    return false;
  }
  for (size_t i = 0; i < want_count; ++i) {
    const ChordEvent &have = got.events[i];
    const ChordEvent &want = want_events[i];
    if (have.op != want.op || have.key != want.key || have.code != want.code ||
        have.gap_after_ms != want.gap_after_ms) {
      snprintf(g_error, sizeof(g_error), "line %d %s: event %u differs", line_no, name,
               static_cast<unsigned>(i));
      return false;
    }
  }
  if (got.skip_count != want_skip_count) {
    snprintf(g_error, sizeof(g_error), "line %d %s: skip count %u want %u", line_no, name,
             static_cast<unsigned>(got.skip_count), static_cast<unsigned>(want_skip_count));
    return false;
  }
  for (size_t i = 0; i < want_skip_count; ++i) {
    const ChordSkip &have = got.skips[i];
    const WantSkip &want = want_skips[i];
    if (have.length != want.length || have.too_long != want.too_long ||
        strcmp(have.text, want.text) != 0) {
      snprintf(g_error, sizeof(g_error), "line %d %s: skip %u differs", line_no, name,
               static_cast<unsigned>(i));
      return false;
    }
  }
  (void)recipe_len;
  return true;
}

extern "C" void test_shared_recipe_vectors(void) {
  static const char *kPaths[] = {
      "test/recipe_vectors.tsv",
      "../test/recipe_vectors.tsv",
      "../../test/recipe_vectors.tsv",
      "../../../test/recipe_vectors.tsv",
  };
  FILE *file = nullptr;
  for (const char *path : kPaths) {
    file = fopen(path, "rb");
    if (file != nullptr) break;
  }
  if (file == nullptr) {
    TEST_FAIL_MESSAGE("could not open test/recipe_vectors.tsv");
    return;
  }

  int cases = 0;
  int line_no = 0;
  bool ok = true;
  char line[2048];
  while (fgets(line, sizeof(line), file) != nullptr) {
    ++line_no;
    if (strchr(line, '\n') == nullptr && strchr(line, '\r') == nullptr && feof(file) == 0) {
      snprintf(g_error, sizeof(g_error), "line %d is longer than the reader", line_no);
      ok = false;
      break;
    }
    size_t n = strlen(line);
    while (n > 0 && (line[n - 1] == '\n' || line[n - 1] == '\r')) line[--n] = '\0';
    if (line[0] == '\0' || line[0] == '#') continue;
    if (!vector_line_ok(line, line_no)) {
      ok = false;
      break;
    }
    ++cases;
  }
  fclose(file);
  if (!ok) TEST_FAIL_MESSAGE(g_error);
  TEST_ASSERT_TRUE_MESSAGE(cases > 0, "recipe vector file has no cases");
}

struct RepeatLog {
  int taps;
  int release_fires;
  uint16_t last_tap;
  uint16_t last_release;
};

static void repeat_poll(ChordEngine *engine, ChordRepeat *rep, RepeatLog *log, uint16_t keys,
                        uint32_t now, bool (*repeats_at)(uint16_t peak)) {
  const ChordStep step = chord_engine_step(engine, keys, now);
  const bool holding = engine->phase == ChordPhase::Holding;
  const uint16_t peak = holding ? engine->peak.mask : 0;
  const bool repeats = holding && peak != 0 && repeats_at != nullptr && repeats_at(peak);
  const ChordRepeatStep got =
      chord_repeat_step(rep, engine->phase, peak, repeats, step.fire, step.fire_mask, now);
  TEST_ASSERT_FALSE(got.send && step.fire);
  if (got.send) {
    log->taps++;
    log->last_tap = got.mask;
  }
  if (step.fire && !got.swallow_fire) {
    log->release_fires++;
    log->last_release = step.fire_mask;
  }
}

static bool repeats_bit0(uint16_t peak) { return peak == 0x0001; }

static bool repeats_none(uint16_t peak) {
  (void)peak;
  return false;
}

static bool repeats_bit0_or_bit01(uint16_t peak) { return peak == 0x0001 || peak == 0x0003; }

static void repeat_begin(ChordEngine *engine, ChordRepeat *rep, RepeatLog *log) {
  chord_engine_reset(engine);
  chord_repeat_reset(rep);
  log->taps = 0;
  log->release_fires = 0;
  log->last_tap = 0;
  log->last_release = 0;
}

extern "C" void test_output_auto_repeat_names(void) {
  TEST_ASSERT_TRUE(chord_output_auto_repeats("Up"));
  TEST_ASSERT_TRUE(chord_output_auto_repeats("Down"));
  TEST_ASSERT_TRUE(chord_output_auto_repeats("Left"));
  TEST_ASSERT_TRUE(chord_output_auto_repeats("Right"));
  TEST_ASSERT_TRUE(chord_output_auto_repeats("Backspace"));
  TEST_ASSERT_FALSE(chord_output_auto_repeats(nullptr));
  TEST_ASSERT_FALSE(chord_output_auto_repeats(""));
  TEST_ASSERT_FALSE(chord_output_auto_repeats("a"));
  TEST_ASSERT_FALSE(chord_output_auto_repeats("Backspace "));
  TEST_ASSERT_FALSE(chord_output_auto_repeats("Shift+Left"));
  TEST_ASSERT_FALSE(chord_output_auto_repeats("Delete"));
}

extern "C" void test_repeat_settle_then_interval(void) {
  ChordEngine engine;
  ChordRepeat rep;
  RepeatLog log;
  repeat_begin(&engine, &rep, &log);

  repeat_poll(&engine, &rep, &log, 0x0001, 0, repeats_bit0);
  repeat_poll(&engine, &rep, &log, 0x0001, 49, repeats_bit0);
  TEST_ASSERT_EQUAL_INT(0, log.taps);
  repeat_poll(&engine, &rep, &log, 0x0001, 50, repeats_bit0);
  TEST_ASSERT_EQUAL_INT(1, log.taps);
  TEST_ASSERT_EQUAL_UINT16(0x0001, log.last_tap);
  repeat_poll(&engine, &rep, &log, 0x0001, 51, repeats_bit0);
  repeat_poll(&engine, &rep, &log, 0x0001, 399, repeats_bit0);
  TEST_ASSERT_EQUAL_INT(1, log.taps);
  repeat_poll(&engine, &rep, &log, 0x0001, 400, repeats_bit0);
  TEST_ASSERT_EQUAL_INT(2, log.taps);
  repeat_poll(&engine, &rep, &log, 0x0001, 432, repeats_bit0);
  TEST_ASSERT_EQUAL_INT(2, log.taps);
  repeat_poll(&engine, &rep, &log, 0x0001, 433, repeats_bit0);
  TEST_ASSERT_EQUAL_INT(3, log.taps);
  TEST_ASSERT_EQUAL_INT(0, log.release_fires);
}

extern "C" void test_repeat_short_tap_uses_release_fire(void) {
  ChordEngine engine;
  ChordRepeat rep;
  RepeatLog log;
  repeat_begin(&engine, &rep, &log);

  repeat_poll(&engine, &rep, &log, 0x0001, 0, repeats_bit0);
  repeat_poll(&engine, &rep, &log, 0, 20, repeats_bit0);
  TEST_ASSERT_EQUAL_INT(0, log.taps);
  TEST_ASSERT_EQUAL_INT(0, log.release_fires);
  repeat_poll(&engine, &rep, &log, 0, 30, repeats_bit0);
  TEST_ASSERT_EQUAL_INT(0, log.taps);
  TEST_ASSERT_EQUAL_INT(1, log.release_fires);
  TEST_ASSERT_EQUAL_UINT16(0x0001, log.last_release);
}

extern "C" void test_repeat_release_after_first_tap_is_swallowed(void) {
  ChordEngine engine;
  ChordRepeat rep;
  RepeatLog log;
  repeat_begin(&engine, &rep, &log);

  repeat_poll(&engine, &rep, &log, 0x0001, 0, repeats_bit0);
  repeat_poll(&engine, &rep, &log, 0x0001, 50, repeats_bit0);
  repeat_poll(&engine, &rep, &log, 0, 60, repeats_bit0);
  repeat_poll(&engine, &rep, &log, 0, 70, repeats_bit0);
  TEST_ASSERT_EQUAL_INT(1, log.taps);
  TEST_ASSERT_EQUAL_INT(0, log.release_fires);
}

extern "C" void test_repeat_guard_does_not_tick_while_up(void) {
  ChordEngine engine;
  ChordRepeat rep;
  RepeatLog log;
  repeat_begin(&engine, &rep, &log);

  repeat_poll(&engine, &rep, &log, 0x0001, 0, repeats_bit0);
  repeat_poll(&engine, &rep, &log, 0x0001, 50, repeats_bit0);
  repeat_poll(&engine, &rep, &log, 0x0001, 400, repeats_bit0);
  TEST_ASSERT_EQUAL_INT(2, log.taps);
  repeat_poll(&engine, &rep, &log, 0, 430, repeats_bit0);
  repeat_poll(&engine, &rep, &log, 0, 433, repeats_bit0);
  TEST_ASSERT_EQUAL_INT(2, log.taps);
  repeat_poll(&engine, &rep, &log, 0, 440, repeats_bit0);
  TEST_ASSERT_EQUAL_INT(2, log.taps);
  TEST_ASSERT_EQUAL_INT(0, log.release_fires);
}

extern "C" void test_repeat_bounce_in_guard_does_not_retap(void) {
  ChordEngine engine;
  ChordRepeat rep;
  RepeatLog log;
  repeat_begin(&engine, &rep, &log);

  repeat_poll(&engine, &rep, &log, 0x0001, 0, repeats_bit0);
  repeat_poll(&engine, &rep, &log, 0x0001, 50, repeats_bit0);
  repeat_poll(&engine, &rep, &log, 0, 55, repeats_bit0);
  repeat_poll(&engine, &rep, &log, 0x0001, 60, repeats_bit0);
  repeat_poll(&engine, &rep, &log, 0x0001, 100, repeats_bit0);
  TEST_ASSERT_EQUAL_INT(1, log.taps);
  repeat_poll(&engine, &rep, &log, 0x0001, 400, repeats_bit0);
  TEST_ASSERT_EQUAL_INT(2, log.taps);
  TEST_ASSERT_EQUAL_UINT16(0x0001, log.last_tap);
}

extern "C" void test_repeat_larger_peak_restarts_clock(void) {
  ChordEngine engine;
  ChordRepeat rep;
  RepeatLog log;
  repeat_begin(&engine, &rep, &log);

  repeat_poll(&engine, &rep, &log, 0x0001, 0, repeats_bit0);
  repeat_poll(&engine, &rep, &log, 0x0003, 40, repeats_bit0);
  repeat_poll(&engine, &rep, &log, 0x0003, 90, repeats_bit0);
  repeat_poll(&engine, &rep, &log, 0x0003, 440, repeats_bit0);
  TEST_ASSERT_EQUAL_INT(0, log.taps);
  repeat_poll(&engine, &rep, &log, 0, 500, repeats_bit0);
  repeat_poll(&engine, &rep, &log, 0, 510, repeats_bit0);
  TEST_ASSERT_EQUAL_INT(0, log.taps);
  TEST_ASSERT_EQUAL_INT(1, log.release_fires);
  TEST_ASSERT_EQUAL_UINT16(0x0003, log.last_release);
}

extern "C" void test_repeat_larger_repeatable_peak_waits_again(void) {
  ChordEngine engine;
  ChordRepeat rep;
  RepeatLog log;
  repeat_begin(&engine, &rep, &log);

  repeat_poll(&engine, &rep, &log, 0x0001, 0, repeats_bit0_or_bit01);
  repeat_poll(&engine, &rep, &log, 0x0001, 50, repeats_bit0_or_bit01);
  TEST_ASSERT_EQUAL_UINT16(0x0001, log.last_tap);
  repeat_poll(&engine, &rep, &log, 0x0003, 60, repeats_bit0_or_bit01);
  repeat_poll(&engine, &rep, &log, 0x0003, 100, repeats_bit0_or_bit01);
  TEST_ASSERT_EQUAL_INT(1, log.taps);
  repeat_poll(&engine, &rep, &log, 0x0003, 110, repeats_bit0_or_bit01);
  TEST_ASSERT_EQUAL_INT(2, log.taps);
  TEST_ASSERT_EQUAL_UINT16(0x0003, log.last_tap);
}

extern "C" void test_repeat_letter_hold_does_not_repeat(void) {
  ChordEngine engine;
  ChordRepeat rep;
  RepeatLog log;
  repeat_begin(&engine, &rep, &log);

  repeat_poll(&engine, &rep, &log, 0x0001, 0, repeats_none);
  repeat_poll(&engine, &rep, &log, 0x0001, 1000, repeats_none);
  TEST_ASSERT_EQUAL_INT(0, log.taps);
  repeat_poll(&engine, &rep, &log, 0, 1010, repeats_none);
  repeat_poll(&engine, &rep, &log, 0, 1020, repeats_none);
  TEST_ASSERT_EQUAL_INT(0, log.taps);
  TEST_ASSERT_EQUAL_INT(1, log.release_fires);
  TEST_ASSERT_EQUAL_UINT16(0x0001, log.last_release);
}

extern "C" void test_repeat_late_poll_does_not_catch_up(void) {
  ChordEngine engine;
  ChordRepeat rep;
  RepeatLog log;
  repeat_begin(&engine, &rep, &log);

  repeat_poll(&engine, &rep, &log, 0x0001, 0, repeats_bit0);
  repeat_poll(&engine, &rep, &log, 0x0001, 500, repeats_bit0);
  TEST_ASSERT_EQUAL_INT(1, log.taps);
  repeat_poll(&engine, &rep, &log, 0x0001, 510, repeats_bit0);
  TEST_ASSERT_EQUAL_INT(1, log.taps);
  repeat_poll(&engine, &rep, &log, 0x0001, 533, repeats_bit0);
  TEST_ASSERT_EQUAL_INT(2, log.taps);
}

extern "C" void test_repeat_first_up_swallows_after_tap(void) {
  ChordEngine engine;
  ChordRepeat rep;
  RepeatLog log;
  repeat_begin(&engine, &rep, &log);
  engine.mode = ChordFireMode::FirstUp;

  repeat_poll(&engine, &rep, &log, 0x0001, 0, repeats_bit0);
  repeat_poll(&engine, &rep, &log, 0, 20, repeats_bit0);
  TEST_ASSERT_EQUAL_INT(0, log.taps);
  TEST_ASSERT_EQUAL_INT(1, log.release_fires);

  repeat_begin(&engine, &rep, &log);
  engine.mode = ChordFireMode::FirstUp;
  repeat_poll(&engine, &rep, &log, 0x0001, 0, repeats_bit0);
  repeat_poll(&engine, &rep, &log, 0x0001, 50, repeats_bit0);
  repeat_poll(&engine, &rep, &log, 0, 80, repeats_bit0);
  TEST_ASSERT_EQUAL_INT(1, log.taps);
  TEST_ASSERT_EQUAL_INT(0, log.release_fires);
}

extern "C" void test_repeat_switch_to_first_swallows_guard_fire(void) {
  ChordEngine engine;
  ChordRepeat rep;
  RepeatLog log;
  repeat_begin(&engine, &rep, &log);

  repeat_poll(&engine, &rep, &log, 0x0001, 0, repeats_bit0);
  repeat_poll(&engine, &rep, &log, 0x0001, 50, repeats_bit0);
  repeat_poll(&engine, &rep, &log, 0, 60, repeats_bit0);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(ChordPhase::ReleaseGuard), static_cast<int>(engine.phase));

  const ChordStep step = chord_engine_set_mode(&engine, ChordFireMode::FirstUp);
  TEST_ASSERT_TRUE(step.fire);
  const ChordRepeatStep got = chord_repeat_step(&rep, engine.phase, 0, false, step.fire,
                                                step.fire_mask, 65);
  TEST_ASSERT_FALSE(got.send);
  TEST_ASSERT_TRUE(got.swallow_fire);
  TEST_ASSERT_EQUAL_INT(1, log.taps);
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
  RUN_TEST(test_both_modes_send_a_chord_once);
  RUN_TEST(test_bounce_within_debounce_does_not_split);
  RUN_TEST(test_repress_inside_guard_merges);
  RUN_TEST(test_nested_chord_after_bounce_window_sends_both);
  RUN_TEST(test_different_chord_inside_guard_sends_both);
  RUN_TEST(test_first_up_draining_ignores_presses);
  RUN_TEST(test_recipe_shapes);
  RUN_TEST(test_skip_overflow_is_flagged);
  RUN_TEST(test_recipe_that_does_not_fit_fails);
  RUN_TEST(test_recipe_token_and_cap_bounds);
  RUN_TEST(test_shared_recipe_vectors);
  RUN_TEST(test_csv_bits_match_firmware_table);
  RUN_TEST(test_each_csv_row_matches_spec);
  RUN_TEST(test_each_csv_output_parses);
  RUN_TEST(test_output_auto_repeat_names);
  RUN_TEST(test_repeat_settle_then_interval);
  RUN_TEST(test_repeat_short_tap_uses_release_fire);
  RUN_TEST(test_repeat_release_after_first_tap_is_swallowed);
  RUN_TEST(test_repeat_guard_does_not_tick_while_up);
  RUN_TEST(test_repeat_bounce_in_guard_does_not_retap);
  RUN_TEST(test_repeat_larger_peak_restarts_clock);
  RUN_TEST(test_repeat_larger_repeatable_peak_waits_again);
  RUN_TEST(test_repeat_letter_hold_does_not_repeat);
  RUN_TEST(test_repeat_late_poll_does_not_catch_up);
  RUN_TEST(test_repeat_first_up_swallows_after_tap);
  RUN_TEST(test_repeat_switch_to_first_swallows_guard_fire);
  return UNITY_END();
}
