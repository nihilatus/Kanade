#pragma once

#include <stddef.h>
#include <stdint.h>

// Chord engine. Tracks the largest simultaneous set while any key is down,
// confirms full release for 25 ms, then matches the live table once.
//
// Boot loads /chords.json from LittleFS when the file is valid. Otherwise the
// live table is a copy of the compiled factory rows in include/chord_map.inc.
// Matching is unchanged: required ⊆ mask ⊆ required ∪ optional, then more
// required bits, fewer optional bits, smaller id.

void chords_begin();
void chords_poll();  // call after buttons_poll()

static constexpr size_t kChordsMaxRows = 256;
static constexpr size_t kChordsJsonMax = 48 * 1024;
static constexpr char kChordsPath[] = "/chords.json";

struct ChordView {
  uint16_t id;
  uint16_t required;
  uint16_t optional;
  const char *kana;
  const char *output;
};

size_t chords_count();
bool chords_at(size_t index, ChordView *out);
bool chords_using_saved();
bool chords_fs_ready();

// Parse into a side buffer. The live table stays put until commit.
// err receives a short Traditional Chinese reason when this returns false.
bool chords_stage_json(const char *json, size_t len, char *err, size_t err_len);
void chords_commit_staged();
void chords_drop_staged();
void chords_load_factory();

// {"source":"factory"|"saved","count":N,"chords":[...]}
// Key masks are arrays of LL..RT2 names. Caller frees with free().
// Null if the document cannot be built.
char *chords_format_json();
