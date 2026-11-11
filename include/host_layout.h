#pragma once

#include <stddef.h>
#include <stdint.h>

#include "chord_core.h"

// Host keyboard layout, applied to a parsed recipe before HID reports.
// The chord table stays as written. A second layout fills these tables;
// only JIS exists, and its rewrite lists are empty.
//
// ascii is 128 bytes in Arduino's layout encoding: bit 7 means Shift,
// the low 7 bits are a HID Keyboard usage. 0 means the character has no key.
// USBHIDKeyboard::press() still folds an ASCII byte so Shift and the key
// share one report. begin() receives this table.
//
// usages replace one raw HID usage with another (Henkan and the other
// JIS-only keys). combos replace one modifier chord plus one plain
// character, which is how Shift+8 stays a key combo rather than the
// character "（". Output modifiers are emitted Shift, Ctrl, Alt, Gui.
// Kana conversion stays on the host IME.

static constexpr uint8_t kLayoutShiftBit = 0x80;

static constexpr uint8_t kLayoutModShift = 0x01;
static constexpr uint8_t kLayoutModCtrl = 0x02;
static constexpr uint8_t kLayoutModAlt = 0x04;
static constexpr uint8_t kLayoutModGui = 0x08;

struct LayoutUsageRewrite {
  uint8_t from;
  uint8_t to;
};

struct LayoutComboRewrite {
  uint8_t mods;
  uint8_t key;
  uint8_t mods_out;
  uint8_t key_out;
};

struct HostLayout {
  const char *name;
  const uint8_t *ascii;
  const LayoutUsageRewrite *usages;
  size_t usage_count;
  const LayoutComboRewrite *combos;
  size_t combo_count;
};

extern const uint8_t kLayoutJisAscii[128];
extern const HostLayout kLayoutJis;

struct LayoutFold {
  uint8_t usage;
  bool shift;
  bool present;
};

// False present when layout is null, ascii is out of range, or the table entry is 0.
LayoutFold layout_fold_ascii(const HostLayout *layout, unsigned char ascii);

// Copy events, applying usage and combo rewrites. False when layout or
// out_count is null, or when the rewritten events do not fit in out_cap.
// An empty rewrite list leaves the events unchanged.
bool layout_apply(const HostLayout *layout, const ChordEvent *in, size_t count, ChordEvent *out,
                  size_t out_cap, size_t *out_count);
