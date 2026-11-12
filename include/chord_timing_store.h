#pragma once

#include "chord_timing.h"

// NVS override for chord_timing. A missing namespace, or no keys yet, leaves
// the factory values in place and stays quiet. A partial set, or values
// chord_timing_accept rejects, also falls back to the factory and prints one
// line. The API token lives in a different namespace.

void chord_timing_store_load();
bool chord_timing_store_save(const ChordTiming &timing);
bool chord_timing_store_clear();
