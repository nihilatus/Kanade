#include "host_layout.h"

// Copied from Arduino-ESP32 KeyboardLayout_ja_JP. Bit 7 is Shift (0x80).
// Owned here so a later layout is another table, not a hardcoded begin() argument.
const uint8_t kLayoutJisAscii[128] = {
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,  // 0x00 NUL-BEL
    0x2a, 0x2b, 0x28, 0x00, 0x00, 0x00, 0x00, 0x00,  // 0x08 BS TAB LF
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,  // 0x10
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,  // 0x18
    0x2c, 0x9e, 0x9f, 0xa0, 0xa1, 0xa2, 0xa3, 0xa4,  // 0x20  space ! " # $ % & '
    0xa5, 0xa6, 0xb4, 0xb3, 0x36, 0x2d, 0x37, 0x38,  // 0x28  ( ) * + , - . /
    0x27, 0x1e, 0x1f, 0x20, 0x21, 0x22, 0x23, 0x24,  // 0x30  0-7
    0x25, 0x26, 0x34, 0x33, 0xb6, 0xad, 0xb7, 0xb8,  // 0x38  8 9 : ; < = > ?
    0x2f, 0x84, 0x85, 0x86, 0x87, 0x88, 0x89, 0x8a,  // 0x40  @ A-G
    0x8b, 0x8c, 0x8d, 0x8e, 0x8f, 0x90, 0x91, 0x92,  // 0x48  H-O
    0x93, 0x94, 0x95, 0x96, 0x97, 0x98, 0x99, 0x9a,  // 0x50  P-W
    0x9b, 0x9c, 0x9d, 0x30, 0x00, 0x31, 0x2e, 0x00,  // 0x58  X-Z [ \ ] ^ _
    0xaf, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0a,  // 0x60  ` a-g
    0x0b, 0x0c, 0x0d, 0x0e, 0x0f, 0x10, 0x11, 0x12,  // 0x68  h-o
    0x13, 0x14, 0x15, 0x16, 0x17, 0x18, 0x19, 0x1a,  // 0x70  p-w
    0x1b, 0x1c, 0x1d, 0xb0, 0x00, 0xb1, 0xae, 0x00,  // 0x78  x-z { | } ~ DEL
};

const HostLayout kLayoutJis = {
    "jis", kLayoutJisAscii, nullptr, 0, nullptr, 0,
};

LayoutFold layout_fold_ascii(const HostLayout *layout, unsigned char ascii) {
  LayoutFold fold{};
  if (layout == nullptr || layout->ascii == nullptr || ascii >= 128) return fold;
  const uint8_t entry = layout->ascii[ascii];
  if (entry == 0) return fold;
  fold.present = true;
  fold.shift = (entry & kLayoutShiftBit) != 0;
  fold.usage = static_cast<uint8_t>(entry & ~kLayoutShiftBit);
  return fold;
}

static uint8_t modifier_bit(ChordKeyKind key) {
  switch (key) {
    case ChordKeyKind::Shift:
      return kLayoutModShift;
    case ChordKeyKind::Ctrl:
      return kLayoutModCtrl;
    case ChordKeyKind::Alt:
      return kLayoutModAlt;
    case ChordKeyKind::Gui:
      return kLayoutModGui;
    default:
      return 0;
  }
}

static uint8_t rewritten_usage(const HostLayout *layout, uint8_t usage) {
  for (size_t i = 0; i < layout->usage_count; ++i) {
    if (layout->usages[i].from == usage) return layout->usages[i].to;
  }
  return usage;
}

static const LayoutComboRewrite *find_combo(const HostLayout *layout, uint8_t mods, uint8_t key) {
  for (size_t i = 0; i < layout->combo_count; ++i) {
    if (layout->combos[i].mods == mods && layout->combos[i].key == key) return &layout->combos[i];
  }
  return nullptr;
}

// Modifier presses, one plain character, then release. 0 when this is not that shape.
static size_t read_combo(const ChordEvent *in, size_t count, size_t index, uint8_t *mods, uint8_t *key,
                         uint8_t *key_gap, uint8_t *release_gap) {
  if (index >= count || modifier_bit(in[index].key) == 0 || in[index].op != ChordEvOp::Press) return 0;
  size_t cursor = index;
  uint8_t bits = 0;
  while (cursor < count && in[cursor].op == ChordEvOp::Press && modifier_bit(in[cursor].key) != 0) {
    bits = static_cast<uint8_t>(bits | modifier_bit(in[cursor].key));
    ++cursor;
  }
  if (cursor >= count || in[cursor].op != ChordEvOp::Press || in[cursor].key != ChordKeyKind::Char) return 0;
  *key = in[cursor].code;
  *key_gap = in[cursor].gap_after_ms;
  ++cursor;
  if (cursor >= count || in[cursor].op != ChordEvOp::ReleaseAll) return 0;
  *release_gap = in[cursor].gap_after_ms;
  ++cursor;
  *mods = bits;
  return cursor - index;
}

static bool push_event(ChordEvent *out, size_t cap, size_t *n, ChordEvOp op, ChordKeyKind key, uint8_t code,
                       uint8_t gap) {
  if (*n >= cap) return false;
  out[*n] = ChordEvent{op, key, code, gap};
  *n += 1;
  return true;
}

static bool emit_combo(ChordEvent *out, size_t cap, size_t *n, uint8_t mods, uint8_t key, uint8_t key_gap,
                       uint8_t release_gap) {
  struct Mod {
    uint8_t bit;
    ChordKeyKind key;
  };
  static const Mod kMods[] = {
      {kLayoutModShift, ChordKeyKind::Shift},
      {kLayoutModCtrl, ChordKeyKind::Ctrl},
      {kLayoutModAlt, ChordKeyKind::Alt},
      {kLayoutModGui, ChordKeyKind::Gui},
  };
  for (const Mod &mod : kMods) {
    if ((mods & mod.bit) == 0) continue;
    if (!push_event(out, cap, n, ChordEvOp::Press, mod.key, 0, kChordGapNone)) return false;
  }
  if (!push_event(out, cap, n, ChordEvOp::Press, ChordKeyKind::Char, key, key_gap)) return false;
  return push_event(out, cap, n, ChordEvOp::ReleaseAll, ChordKeyKind::Char, 0, release_gap);
}

bool layout_apply(const HostLayout *layout, const ChordEvent *in, size_t count, ChordEvent *out, size_t out_cap,
                  size_t *out_count) {
  if (layout == nullptr || out_count == nullptr) return false;
  if (count > 0 && (in == nullptr || out == nullptr)) return false;
  size_t written = 0;
  size_t index = 0;
  while (index < count) {
    uint8_t mods = 0;
    uint8_t key = 0;
    uint8_t key_gap = 0;
    uint8_t release_gap = 0;
    const size_t consumed = read_combo(in, count, index, &mods, &key, &key_gap, &release_gap);
    if (consumed != 0) {
      const LayoutComboRewrite *rule = find_combo(layout, mods, key);
      if (rule != nullptr) {
        if (!emit_combo(out, out_cap, &written, rule->mods_out, rule->key_out, key_gap, release_gap)) return false;
        index += consumed;
        continue;
      }
    }
    if (written >= out_cap) return false;
    ChordEvent event = in[index];
    if (event.op == ChordEvOp::Press && event.key == ChordKeyKind::Raw) {
      event.code = rewritten_usage(layout, event.code);
    }
    out[written++] = event;
    ++index;
  }
  *out_count = written;
  return true;
}
