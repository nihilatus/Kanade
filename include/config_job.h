#pragma once

#include <stdint.h>

// One chord-table job, shared by the key-scan poll and the HTTP worker.
// The phase is the only synchronization word. kind and main_ok are written
// before a release store of the phase, and read only after an acquire load
// of the phase that grants ownership:
//   Idle      — poll may queue
//   Queued    — worker may parse, read, or write the file
//   NeedMain  — poll may commit or load the live table
//   Resume    — worker may roll the file back or build the response
//   Done      — poll may take the response and return to Idle
// A missing worker never queues. Heavy work does not fall back onto the scan.

enum class ConfigJobPhase : uint8_t { Idle = 0, Queued = 1, NeedMain = 2, Resume = 3, Done = 4 };

enum class ConfigJobKind : uint8_t { None = 0, Get = 1, Post = 2, Reset = 3 };

enum class ConfigJobQueue : uint8_t { Ok = 0, Busy, NoWorker };

enum class ConfigJobAction : uint8_t { None = 0, RunGet, RunPost, RunReset, RunResume };

struct ConfigJobView {
  ConfigJobPhase phase;
  ConfigJobKind kind;
  bool main_ok;
};

// On Ok, *job is Queued for kind and main_ok is false.
// Busy and NoWorker leave *job unchanged. NoWorker is Idle with no task to run it.
ConfigJobQueue config_job_queue(ConfigJobView *job, ConfigJobKind kind, bool worker_up);

// Queued and Resume belong to the worker. Every other phase is None.
ConfigJobAction config_job_worker_action(ConfigJobView job);

// Queued (GET, or a POST/reset that failed before the live table) or Resume → Done.
bool config_job_finish(ConfigJobView *job);

// Queued POST or reset, after the file transaction, → NeedMain.
bool config_job_need_main(ConfigJobView *job);

// NeedMain → Resume. ok is the live-table commit or factory load.
bool config_job_applied(ConfigJobView *job, bool ok);

// Done → Idle. The poll has taken the response.
bool config_job_release(ConfigJobView *job);
