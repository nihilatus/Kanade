#pragma once

#include <stddef.h>
#include <stdint.h>

// Replace and restore the chord JSON file. The live name stays in place until
// the temp file is fully written. The previous live file is renamed aside and
// stays there until the caller deletes it, after the live table accepts the
// new bytes. A failed install puts that previous file back when the rename
// still works.

struct ChordFileOps {
  void *ctx;
  bool (*exists)(void *ctx, const char *path);
  bool (*remove)(void *ctx, const char *path);
  bool (*rename)(void *ctx, const char *from, const char *to);
  bool (*write)(void *ctx, const char *path, const uint8_t *data, size_t len);
};

// Writes data to tmp, moves live to bak when live exists, then moves tmp to live.
// False leaves the previous live bytes in place when a rollback rename succeeds.
// If that rename also fails, those bytes stay in bak and live is absent.
bool chord_files_replace(const ChordFileOps *ops, const uint8_t *data, size_t len, const char *live,
                         const char *tmp, const char *bak);

// Moves bak back to live. With no bak, removes live. False means the previous
// bytes are still in bak, or live could not be removed.
bool chord_files_restore(const ChordFileOps *ops, const char *live, const char *bak);
