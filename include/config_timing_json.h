#pragma once

#include <stddef.h>
#include <stdint.h>

#include "chord_timing.h"

// Parse and format the hotspot timing JSON. Does not accept the numbers,
// store them, or allocate.

bool config_timing_parse(const char *body, size_t len, ChordTiming *out, char *err, size_t err_len);

// Writes a NUL-terminated JSON object. False when the buffer cannot hold it.
// with_ok adds "ok":true. *wrote is the byte count without the NUL.
bool config_timing_format(const ChordTiming &cur, const ChordTiming &factory,
                          const ChordTimingLimits &limits, bool with_ok, char *buf, size_t cap,
                          size_t *wrote);
