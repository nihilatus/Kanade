#include "chords.h"
#include "config_files.h"
#include "config_job.h"

#include <string.h>

#include <string>

#include <unity.h>

static ConfigJobView idle_job() {
  return ConfigJobView{ConfigJobPhase::Idle, ConfigJobKind::None, false};
}

extern "C" void test_queue_without_worker_stays_idle(void) {
  ConfigJobView job = idle_job();
  TEST_ASSERT_TRUE(config_job_queue(&job, ConfigJobKind::Post, false) == ConfigJobQueue::NoWorker);
  TEST_ASSERT_TRUE(job.phase == ConfigJobPhase::Idle);
  TEST_ASSERT_TRUE(job.kind == ConfigJobKind::None);
  TEST_ASSERT_FALSE(job.main_ok);
  TEST_ASSERT_TRUE(config_job_queue(nullptr, ConfigJobKind::Get, true) == ConfigJobQueue::Busy);
}

extern "C" void test_second_queue_is_busy_until_release(void) {
  ConfigJobView job = idle_job();
  TEST_ASSERT_TRUE(config_job_queue(&job, ConfigJobKind::Post, true) == ConfigJobQueue::Ok);
  const ConfigJobView held = job;
  TEST_ASSERT_TRUE(config_job_queue(&job, ConfigJobKind::Get, true) == ConfigJobQueue::Busy);
  TEST_ASSERT_TRUE(job.phase == held.phase);
  TEST_ASSERT_TRUE(job.kind == held.kind);
}

extern "C" void test_post_reaches_main_before_resume(void) {
  ConfigJobView job = idle_job();
  TEST_ASSERT_TRUE(config_job_queue(&job, ConfigJobKind::Post, true) == ConfigJobQueue::Ok);
  TEST_ASSERT_TRUE(job.phase == ConfigJobPhase::Queued);
  TEST_ASSERT_TRUE(job.kind == ConfigJobKind::Post);
  TEST_ASSERT_FALSE(job.main_ok);
  TEST_ASSERT_TRUE(config_job_worker_action(job) == ConfigJobAction::RunPost);

  TEST_ASSERT_TRUE(config_job_need_main(&job));
  TEST_ASSERT_TRUE(job.phase == ConfigJobPhase::NeedMain);
  TEST_ASSERT_TRUE(config_job_worker_action(job) == ConfigJobAction::None);
  TEST_ASSERT_FALSE(config_job_finish(&job));
  TEST_ASSERT_TRUE(job.phase == ConfigJobPhase::NeedMain);

  // Commit failure still resumes the worker. Rollback is not done on the scan.
  TEST_ASSERT_TRUE(config_job_applied(&job, false));
  TEST_ASSERT_TRUE(job.phase == ConfigJobPhase::Resume);
  TEST_ASSERT_FALSE(job.main_ok);
  TEST_ASSERT_TRUE(config_job_worker_action(job) == ConfigJobAction::RunResume);
  TEST_ASSERT_FALSE(config_job_applied(&job, true));

  TEST_ASSERT_TRUE(config_job_finish(&job));
  TEST_ASSERT_TRUE(job.phase == ConfigJobPhase::Done);
  TEST_ASSERT_TRUE(config_job_release(&job));
  TEST_ASSERT_TRUE(job.phase == ConfigJobPhase::Idle);
  TEST_ASSERT_TRUE(job.kind == ConfigJobKind::None);
  TEST_ASSERT_FALSE(job.main_ok);
  TEST_ASSERT_FALSE(config_job_release(&job));
}

extern "C" void test_get_finishes_without_touching_the_live_table(void) {
  ConfigJobView job = idle_job();
  TEST_ASSERT_TRUE(config_job_queue(&job, ConfigJobKind::Get, true) == ConfigJobQueue::Ok);
  TEST_ASSERT_TRUE(config_job_worker_action(job) == ConfigJobAction::RunGet);
  TEST_ASSERT_FALSE(config_job_need_main(&job));
  TEST_ASSERT_TRUE(job.phase == ConfigJobPhase::Queued);
  TEST_ASSERT_TRUE(config_job_finish(&job));
  TEST_ASSERT_TRUE(job.phase == ConfigJobPhase::Done);
  TEST_ASSERT_TRUE(config_job_release(&job));
}

extern "C" void test_timing_post_finishes_on_the_worker(void) {
  ConfigJobView job = idle_job();
  TEST_ASSERT_TRUE(config_job_queue(&job, ConfigJobKind::TimingPost, true) == ConfigJobQueue::Ok);
  TEST_ASSERT_TRUE(config_job_worker_action(job) == ConfigJobAction::RunTimingPost);
  TEST_ASSERT_FALSE(config_job_need_main(&job));
  TEST_ASSERT_TRUE(job.phase == ConfigJobPhase::Queued);
  TEST_ASSERT_TRUE(config_job_finish(&job));
  TEST_ASSERT_TRUE(config_job_release(&job));

  TEST_ASSERT_TRUE(config_job_queue(&job, ConfigJobKind::TimingReset, true) == ConfigJobQueue::Ok);
  TEST_ASSERT_TRUE(config_job_worker_action(job) == ConfigJobAction::RunTimingReset);
  TEST_ASSERT_FALSE(config_job_need_main(&job));
  TEST_ASSERT_TRUE(config_job_finish(&job));
  TEST_ASSERT_TRUE(config_job_release(&job));
  TEST_ASSERT_TRUE(job.phase == ConfigJobPhase::Idle);
}

extern "C" void test_reset_commit_returns_to_the_worker(void) {
  ConfigJobView job = idle_job();
  TEST_ASSERT_TRUE(config_job_queue(&job, ConfigJobKind::Reset, true) == ConfigJobQueue::Ok);
  TEST_ASSERT_TRUE(config_job_worker_action(job) == ConfigJobAction::RunReset);
  TEST_ASSERT_TRUE(config_job_need_main(&job));
  TEST_ASSERT_TRUE(config_job_worker_action(job) == ConfigJobAction::None);
  TEST_ASSERT_TRUE(config_job_applied(&job, true));
  TEST_ASSERT_TRUE(job.main_ok);
  TEST_ASSERT_TRUE(config_job_worker_action(job) == ConfigJobAction::RunResume);
  TEST_ASSERT_TRUE(config_job_finish(&job));
  TEST_ASSERT_TRUE(config_job_release(&job));
  TEST_ASSERT_TRUE(job.phase == ConfigJobPhase::Idle);
}

struct Slot {
  bool present;
  std::string data;
  bool fail_remove;
};

struct Mem {
  Slot live;
  Slot tmp;
  Slot bak;
  bool fail_write;
  bool short_write;
  int rename_n;
  bool fail_rename[4];
};

static Slot *slot_of(Mem *mem, const char *path) {
  if (strcmp(path, kChordsPath) == 0) return &mem->live;
  if (strcmp(path, kChordsTmpPath) == 0) return &mem->tmp;
  if (strcmp(path, kChordsBakPath) == 0) return &mem->bak;
  return nullptr;
}

static bool mem_exists(void *ctx, const char *path) {
  Slot *slot = slot_of(static_cast<Mem *>(ctx), path);
  return slot != nullptr && slot->present;
}

static bool mem_remove(void *ctx, const char *path) {
  Slot *slot = slot_of(static_cast<Mem *>(ctx), path);
  if (slot == nullptr || !slot->present) return false;
  if (slot->fail_remove) return false;
  slot->present = false;
  slot->data.clear();
  return true;
}

static bool mem_rename(void *ctx, const char *from, const char *to) {
  Mem *mem = static_cast<Mem *>(ctx);
  const int index = mem->rename_n;
  if (index < 4) mem->rename_n = index + 1;
  if (index < 4 && mem->fail_rename[index]) return false;
  Slot *src = slot_of(mem, from);
  Slot *dst = slot_of(mem, to);
  if (src == nullptr || dst == nullptr || !src->present || dst->present) return false;
  dst->present = true;
  dst->data = src->data;
  src->present = false;
  src->data.clear();
  return true;
}

static bool mem_write(void *ctx, const char *path, const uint8_t *data, size_t len) {
  Mem *mem = static_cast<Mem *>(ctx);
  Slot *slot = slot_of(mem, path);
  if (slot == nullptr) return false;
  if (mem->fail_write) return false;
  if (mem->short_write) {
    slot->present = true;
    slot->data = "partial";
    return false;
  }
  slot->present = true;
  slot->data.assign(reinterpret_cast<const char *>(data), len);
  return true;
}

static ChordFileOps mem_ops(Mem *mem) {
  return ChordFileOps{mem, mem_exists, mem_remove, mem_rename, mem_write};
}

static Mem mem_with_live(const char *bytes) {
  Mem mem = {};
  mem.live.present = true;
  mem.live.data = bytes;
  return mem;
}

static void expect_file(const Slot &slot, bool present, const char *bytes) {
  TEST_ASSERT_TRUE(slot.present == present);
  if (present) TEST_ASSERT_EQUAL_STRING(bytes, slot.data.c_str());
}

extern "C" void test_replace_keeps_previous_bytes_as_bak(void) {
  Mem mem = mem_with_live("old");
  const ChordFileOps ops = mem_ops(&mem);
  const uint8_t neu[] = {'n', 'e', 'w'};
  TEST_ASSERT_TRUE(chord_files_replace(&ops, neu, sizeof(neu), kChordsPath, kChordsTmpPath, kChordsBakPath));
  expect_file(mem.live, true, "new");
  expect_file(mem.bak, true, "old");
  expect_file(mem.tmp, false, "");
}

extern "C" void test_replace_short_write_leaves_live(void) {
  Mem mem = mem_with_live("old");
  mem.short_write = true;
  const ChordFileOps ops = mem_ops(&mem);
  const uint8_t neu[] = {'n', 'e', 'w'};
  TEST_ASSERT_FALSE(chord_files_replace(&ops, neu, sizeof(neu), kChordsPath, kChordsTmpPath, kChordsBakPath));
  expect_file(mem.live, true, "old");
  expect_file(mem.tmp, false, "");
  expect_file(mem.bak, false, "");
}

extern "C" void test_replace_open_failure_leaves_live(void) {
  Mem mem = mem_with_live("old");
  mem.fail_write = true;
  const ChordFileOps ops = mem_ops(&mem);
  const uint8_t neu[] = {'n'};
  TEST_ASSERT_FALSE(chord_files_replace(&ops, neu, sizeof(neu), kChordsPath, kChordsTmpPath, kChordsBakPath));
  expect_file(mem.live, true, "old");
  expect_file(mem.tmp, false, "");
}

extern "C" void test_replace_stops_when_bak_cannot_be_removed(void) {
  Mem mem = mem_with_live("old");
  mem.bak.present = true;
  mem.bak.data = "older";
  mem.bak.fail_remove = true;
  const ChordFileOps ops = mem_ops(&mem);
  const uint8_t neu[] = {'n', 'e', 'w'};
  TEST_ASSERT_FALSE(chord_files_replace(&ops, neu, sizeof(neu), kChordsPath, kChordsTmpPath, kChordsBakPath));
  expect_file(mem.live, true, "old");
  expect_file(mem.bak, true, "older");
  expect_file(mem.tmp, false, "");
}

extern "C" void test_replace_keeps_live_when_backup_rename_fails(void) {
  Mem mem = mem_with_live("old");
  mem.fail_rename[0] = true;
  const ChordFileOps ops = mem_ops(&mem);
  const uint8_t neu[] = {'n', 'e', 'w'};
  TEST_ASSERT_FALSE(chord_files_replace(&ops, neu, sizeof(neu), kChordsPath, kChordsTmpPath, kChordsBakPath));
  expect_file(mem.live, true, "old");
  expect_file(mem.tmp, false, "");
  expect_file(mem.bak, false, "");
}

extern "C" void test_replace_restores_live_when_install_rename_fails(void) {
  Mem mem = mem_with_live("old");
  mem.fail_rename[1] = true;
  const ChordFileOps ops = mem_ops(&mem);
  const uint8_t neu[] = {'n', 'e', 'w'};
  TEST_ASSERT_FALSE(chord_files_replace(&ops, neu, sizeof(neu), kChordsPath, kChordsTmpPath, kChordsBakPath));
  expect_file(mem.live, true, "old");
  expect_file(mem.tmp, false, "");
  expect_file(mem.bak, false, "");
}

extern "C" void test_replace_keeps_previous_bytes_in_bak_when_rollback_rename_fails(void) {
  Mem mem = mem_with_live("old");
  mem.fail_rename[1] = true;
  mem.fail_rename[2] = true;
  const ChordFileOps ops = mem_ops(&mem);
  const uint8_t neu[] = {'n', 'e', 'w'};
  TEST_ASSERT_FALSE(chord_files_replace(&ops, neu, sizeof(neu), kChordsPath, kChordsTmpPath, kChordsBakPath));
  expect_file(mem.live, false, "");
  expect_file(mem.bak, true, "old");
  expect_file(mem.tmp, false, "");
}

extern "C" void test_restore_puts_bak_back(void) {
  Mem mem = mem_with_live("broken");
  mem.bak.present = true;
  mem.bak.data = "old";
  const ChordFileOps ops = mem_ops(&mem);
  TEST_ASSERT_TRUE(chord_files_restore(&ops, kChordsPath, kChordsBakPath));
  expect_file(mem.live, true, "old");
  expect_file(mem.bak, false, "");
}

extern "C" void test_restore_removes_live_when_there_is_no_bak(void) {
  Mem mem = mem_with_live("broken");
  const ChordFileOps ops = mem_ops(&mem);
  TEST_ASSERT_TRUE(chord_files_restore(&ops, kChordsPath, kChordsBakPath));
  expect_file(mem.live, false, "");
}

extern "C" void test_restore_leaves_both_when_live_cannot_be_removed(void) {
  Mem mem = mem_with_live("broken");
  mem.live.fail_remove = true;
  mem.bak.present = true;
  mem.bak.data = "old";
  const ChordFileOps ops = mem_ops(&mem);
  TEST_ASSERT_FALSE(chord_files_restore(&ops, kChordsPath, kChordsBakPath));
  expect_file(mem.live, true, "broken");
  expect_file(mem.bak, true, "old");
}

extern "C" void test_restore_leaves_previous_bytes_in_bak_when_rename_fails(void) {
  Mem mem = mem_with_live("broken");
  mem.bak.present = true;
  mem.bak.data = "old";
  mem.fail_rename[0] = true;
  const ChordFileOps ops = mem_ops(&mem);
  TEST_ASSERT_FALSE(chord_files_restore(&ops, kChordsPath, kChordsBakPath));
  expect_file(mem.live, false, "");
  expect_file(mem.bak, true, "old");
}

int main(int argc, char **argv) {
  (void)argc;
  (void)argv;
  UNITY_BEGIN();
  RUN_TEST(test_queue_without_worker_stays_idle);
  RUN_TEST(test_second_queue_is_busy_until_release);
  RUN_TEST(test_post_reaches_main_before_resume);
  RUN_TEST(test_get_finishes_without_touching_the_live_table);
  RUN_TEST(test_reset_commit_returns_to_the_worker);
  RUN_TEST(test_timing_post_finishes_on_the_worker);
  RUN_TEST(test_replace_keeps_previous_bytes_as_bak);
  RUN_TEST(test_replace_short_write_leaves_live);
  RUN_TEST(test_replace_open_failure_leaves_live);
  RUN_TEST(test_replace_stops_when_bak_cannot_be_removed);
  RUN_TEST(test_replace_keeps_live_when_backup_rename_fails);
  RUN_TEST(test_replace_restores_live_when_install_rename_fails);
  RUN_TEST(test_replace_keeps_previous_bytes_in_bak_when_rollback_rename_fails);
  RUN_TEST(test_restore_puts_bak_back);
  RUN_TEST(test_restore_removes_live_when_there_is_no_bak);
  RUN_TEST(test_restore_leaves_both_when_live_cannot_be_removed);
  RUN_TEST(test_restore_leaves_previous_bytes_in_bak_when_rename_fails);
  return UNITY_END();
}
