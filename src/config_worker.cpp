#include "config_runtime.h"

#include <stdio.h>
#include <string.h>

#include <Arduino.h>
#include <FS.h>
#include <LittleFS.h>

#undef write
#undef read
#undef send
#undef recv

#include "console_log.h"

#include "chord_timing.h"
#include "chord_timing_store.h"
#include "chords.h"
#include "config_files.h"
#include "config_timing_json.h"

static bool remove_present(const char *path) {
  if (!LittleFS.exists(path)) return true;
  return LittleFS.remove(path);
}

static bool lfs_exists(void *, const char *path) { return LittleFS.exists(path); }

static bool lfs_remove(void *, const char *path) { return LittleFS.remove(path); }

static bool lfs_rename(void *, const char *from, const char *to) { return LittleFS.rename(from, to); }

// flush() is fflush plus fsync. close() alone can leave the new bytes in a
// buffer, and the next step renames the previous chords.json away.
static bool lfs_write(void *, const char *path, const uint8_t *data, size_t len) {
  File file = LittleFS.open(path, "w");
  if (!file) return false;
  const size_t wrote = file.write(data, len);
  file.flush();
  file.close();
  return wrote == len;
}

static ChordFileOps lfs_ops() {
  return ChordFileOps{nullptr, lfs_exists, lfs_remove, lfs_rename, lfs_write};
}

static bool write_chords_file(const uint8_t *data, size_t len) {
  const ChordFileOps ops = lfs_ops();
  return chord_files_replace(&ops, data, len, kChordsPath, kChordsTmpPath, kChordsBakPath);
}

static bool restore_previous_chords() {
  const ChordFileOps ops = lfs_ops();
  return chord_files_restore(&ops, kChordsPath, kChordsBakPath);
}

// kind and main_ok are published before the phase store, and read only after
// an acquire load of the phase. See config_job.h.
static ConfigJobView load_job() {
  ConfigJobView view;
  view.phase = static_cast<ConfigJobPhase>(g_job_phase.load(std::memory_order_acquire));
  view.kind = g_job_kind;
  view.main_ok = g_job_main_ok;
  return view;
}

static void publish_job(const ConfigJobView &view) {
  g_job_kind = view.kind;
  g_job_main_ok = view.main_ok;
  g_job_phase.store(static_cast<uint8_t>(view.phase), std::memory_order_release);
}

static void mark_done() {
  ConfigJobView view = load_job();
  if (!config_job_finish(&view)) return;
  publish_job(view);
}

static void mark_need_main() {
  ConfigJobView view = load_job();
  if (!config_job_need_main(&view)) return;
  publish_job(view);
}

static void remember_error(int code, const char *msg) {
  g_job_code = code;
  chords_release(g_job_resp);
  g_job_resp = json_error(msg);
  mark_done();
}

char *config_format_timing_json(bool with_ok) {
  char *buf = alloc_chars(768);
  if (buf == nullptr) return nullptr;
  size_t wrote = 0;
  if (!config_timing_format(chord_timing_current(), chord_timing_factory(), chord_timing_limits(), with_ok,
                            buf, 768, &wrote)) {
    chords_release(buf);
    return nullptr;
  }
  return buf;
}

static void run_timing_post() {
  char err[120];
  err[0] = '\0';
  ChordTiming timing;
  if (!config_timing_parse(g_job_body, g_job_len, &timing, err, sizeof(err))) {
    chords_release(g_job_body);
    g_job_body = nullptr;
    remember_error(400, err[0] != '\0' ? err : "JSON 格式不對");
    return;
  }
  chords_release(g_job_body);
  g_job_body = nullptr;
  if (!chord_timing_accept(timing, err, sizeof(err))) {
    remember_error(400, err[0] != '\0' ? err : "時間參數不對");
    return;
  }
  if (!chord_timing_store_save(timing)) {
    remember_error(500, "存檔失敗");
    return;
  }
  chord_timing_apply(timing);
  char *json = config_format_timing_json(true);
  if (json == nullptr) {
    remember_error(500, "記憶體不足");
    return;
  }
  g_job_resp = json;
  g_job_code = 200;
  mark_done();
}

static void run_timing_reset() {
  if (!chord_timing_store_clear()) {
    remember_error(500, "存檔失敗");
    return;
  }
  chord_timing_use_factory();
  char *json = config_format_timing_json(true);
  if (json == nullptr) {
    remember_error(500, "記憶體不足");
    return;
  }
  g_job_resp = json;
  g_job_code = 200;
  mark_done();
}

static void run_queued() {
  if (g_job_kind == ConfigJobKind::Get) {
    char *json = chords_format_json();
    if (json == nullptr) {
      remember_error(500, "記憶體不足");
      return;
    }
    g_job_resp = json;
    g_job_code = 200;
    mark_done();
    return;
  }

  if (g_job_kind == ConfigJobKind::Reset) {
    if (!chords_fs_ready() || !remove_present(kChordsBakPath) || !remove_present(kChordsTmpPath) ||
        !remove_present(kChordsPath)) {
      remember_error(500, chords_fs_ready() ? "存檔失敗" : "檔案系統還沒備好");
      return;
    }
    mark_need_main();
    return;
  }
  remember_error(500, "失敗");
}

static void run_post() {
  if (!chords_fs_ready()) {
    chords_release(g_job_body);
    g_job_body = nullptr;
    remember_error(500, "檔案系統還沒備好");
    return;
  }
  char err[160];
  err[0] = '\0';
  const ChordStageStatus staged = chords_stage_json(g_job_body, g_job_len, err, sizeof(err));
  if (staged != ChordStageStatus::Ok) {
    chords_drop_staged();
    chords_release(g_job_body);
    g_job_body = nullptr;
    remember_error(staged == ChordStageStatus::NoMemory ? 500 : 400,
                   err[0] != '\0' ? err : "JSON 格式不對");
    return;
  }
  const bool wrote = write_chords_file(reinterpret_cast<const uint8_t *>(g_job_body), g_job_len);
  chords_release(g_job_body);
  g_job_body = nullptr;
  if (!wrote) {
    chords_drop_staged();
    remember_error(500, "存檔失敗");
    return;
  }
  mark_need_main();
}

static void run_resume() {
  if (g_job_kind == ConfigJobKind::Post) {
    if (!g_job_main_ok) {
      restore_previous_chords();
      remember_error(500, "存檔失敗");
      return;
    }
    remove_present(kChordsBakPath);
    char *buf = alloc_chars(80);
    if (buf == nullptr) {
      remember_error(500, "記憶體不足");
      return;
    }
    snprintf(buf, 80, "{\"ok\":true,\"source\":\"saved\",\"count\":%u}", g_job_count);
    g_job_resp = buf;
    g_job_code = 200;
    mark_done();
    return;
  }
  if (!g_job_main_ok) {
    remember_error(500, "記憶體不足");
    return;
  }
  char *json = chords_format_json();
  if (json == nullptr) {
    remember_error(500, "記憶體不足");
    return;
  }
  g_job_resp = json;
  g_job_code = 200;
  mark_done();
}

static void worker_fn(void *) {
  for (;;) {
    ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
    switch (config_job_worker_action(load_job())) {
      case ConfigJobAction::RunPost:
        run_post();
        break;
      case ConfigJobAction::RunGet:
      case ConfigJobAction::RunReset:
        run_queued();
        break;
      case ConfigJobAction::RunTimingPost:
        run_timing_post();
        break;
      case ConfigJobAction::RunTimingReset:
        run_timing_reset();
        break;
      case ConfigJobAction::RunResume:
        run_resume();
        break;
      case ConfigJobAction::None:
        break;
    }
  }
}

static void apply_main(bool wake) {
  ConfigJobView view = load_job();
  bool ok = false;
  if (view.kind == ConfigJobKind::Post) {
    ok = chords_commit_staged();
    if (!ok) chords_drop_staged();
    else g_job_count = static_cast<unsigned>(chords_count());
  } else if (view.kind == ConfigJobKind::Reset) {
    ok = chords_load_factory();
  }
  if (!config_job_applied(&view, ok)) return;
  publish_job(view);
  if (wake && g_worker != nullptr) xTaskNotifyGive(g_worker);
}

static ConfigJobQueue queue_job(ConfigJobKind kind, char *body, size_t len) {
  ConfigJobView view = load_job();
  const ConfigJobQueue result = config_job_queue(&view, kind, g_worker != nullptr);
  if (result != ConfigJobQueue::Ok) {
    chords_release(body);
    return result;
  }
  g_job_body = body;
  g_job_len = len;
  g_job_resp = nullptr;
  g_job_code = 500;
  publish_job(view);
  g_conn = Conn::Wait;
  g_io_ms = millis();
  xTaskNotifyGive(g_worker);
  return ConfigJobQueue::Ok;
}

void queue_or_error(ConfigJobKind kind, char *body, size_t len) {
  switch (queue_job(kind, body, len)) {
    case ConfigJobQueue::Ok:
      return;
    case ConfigJobQueue::NoWorker:
      begin_error(503, "設定服務還沒備好", g_cors);
      return;
    case ConfigJobQueue::Busy:
      begin_error(500, "失敗", g_cors);
      return;
  }
}

void config_worker_service() {
  const ConfigJobPhase phase = load_job().phase;
  if (phase == ConfigJobPhase::NeedMain) {
    apply_main(true);
    return;
  }
  if (phase != ConfigJobPhase::Done) return;
  char *resp = g_job_resp;
  g_job_resp = nullptr;
  const int code = g_job_code;
  ConfigJobView view = load_job();
  if (config_job_release(&view)) publish_job(view);
  if (g_drop_result || g_conn != Conn::Wait) {
    chords_release(resp);
    g_drop_result = false;
    config_conn_abandon_wait();
    return;
  }
  if (resp == nullptr) {
    begin_error(code != 0 ? code : 500, "失敗", g_cors);
    return;
  }
  begin_send(code, "application/json; charset=utf-8", resp, strlen(resp), resp, g_cors);
}

void config_worker_start() {
  if (g_worker != nullptr) return;
  const BaseType_t ok = xTaskCreatePinnedToCore(worker_fn, "cfghttp", 16384, nullptr, 1, &g_worker, 0);
  if (ok != pdPASS) {
    g_worker = nullptr;
    console_println("HTTP worker failed; chord save and load stay off");
  }
}
