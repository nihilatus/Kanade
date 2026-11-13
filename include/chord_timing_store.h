#pragma once

#include "chord_timing.h"

// NVS override for chord_timing. One versioned blob under namespace chordtime
// (key cfg) is written as a single putBytes. A missing namespace, or no keys
// yet, leaves the factory values in place and stays quiet. A bad blob, a
// partial legacy four-key set, or values chord_timing_accept rejects, falls
// back to the factory and prints one line. Valid legacy four-key sets are
// migrated to the blob on load. The API token lives in a different namespace.

void chord_timing_store_load();
bool chord_timing_store_save(const ChordTiming &timing);
bool chord_timing_store_clear();
