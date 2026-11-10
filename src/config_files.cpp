#include "config_files.h"

static bool remove_present(const ChordFileOps *ops, const char *path) {
  if (!ops->exists(ops->ctx, path)) return true;
  return ops->remove(ops->ctx, path);
}

static bool ops_ready(const ChordFileOps *ops) {
  return ops != nullptr && ops->exists != nullptr && ops->remove != nullptr && ops->rename != nullptr;
}

bool chord_files_replace(const ChordFileOps *ops, const uint8_t *data, size_t len, const char *live,
                         const char *tmp, const char *bak) {
  if (!ops_ready(ops) || ops->write == nullptr || data == nullptr || live == nullptr || tmp == nullptr ||
      bak == nullptr) {
    return false;
  }
  if (!remove_present(ops, tmp)) return false;
  if (!ops->write(ops->ctx, tmp, data, len)) {
    remove_present(ops, tmp);
    return false;
  }
  if (!remove_present(ops, bak)) {
    remove_present(ops, tmp);
    return false;
  }
  const bool had = ops->exists(ops->ctx, live);
  if (had && !ops->rename(ops->ctx, live, bak)) {
    remove_present(ops, tmp);
    return false;
  }
  if (!ops->rename(ops->ctx, tmp, live)) {
    if (had) ops->rename(ops->ctx, bak, live);
    remove_present(ops, tmp);
    return false;
  }
  return true;
}

bool chord_files_restore(const ChordFileOps *ops, const char *live, const char *bak) {
  if (!ops_ready(ops) || live == nullptr || bak == nullptr) return false;
  if (!ops->exists(ops->ctx, bak)) return remove_present(ops, live);
  if (!remove_present(ops, live)) return false;
  return ops->rename(ops->ctx, bak, live);
}
