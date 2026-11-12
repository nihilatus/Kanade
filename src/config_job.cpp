#include "config_job.h"

ConfigJobQueue config_job_queue(ConfigJobView *job, ConfigJobKind kind, bool worker_up) {
  if (job == nullptr || job->phase != ConfigJobPhase::Idle) return ConfigJobQueue::Busy;
  if (!worker_up) return ConfigJobQueue::NoWorker;
  if (kind != ConfigJobKind::Get && kind != ConfigJobKind::Post && kind != ConfigJobKind::Reset &&
      kind != ConfigJobKind::TimingPost && kind != ConfigJobKind::TimingReset) {
    return ConfigJobQueue::Busy;
  }
  job->phase = ConfigJobPhase::Queued;
  job->kind = kind;
  job->main_ok = false;
  return ConfigJobQueue::Ok;
}

ConfigJobAction config_job_worker_action(ConfigJobView job) {
  if (job.phase == ConfigJobPhase::Queued) {
    if (job.kind == ConfigJobKind::Get) return ConfigJobAction::RunGet;
    if (job.kind == ConfigJobKind::Post) return ConfigJobAction::RunPost;
    if (job.kind == ConfigJobKind::Reset) return ConfigJobAction::RunReset;
    if (job.kind == ConfigJobKind::TimingPost) return ConfigJobAction::RunTimingPost;
    if (job.kind == ConfigJobKind::TimingReset) return ConfigJobAction::RunTimingReset;
    return ConfigJobAction::None;
  }
  if (job.phase == ConfigJobPhase::Resume) return ConfigJobAction::RunResume;
  return ConfigJobAction::None;
}

bool config_job_finish(ConfigJobView *job) {
  if (job == nullptr) return false;
  if (job->phase != ConfigJobPhase::Queued && job->phase != ConfigJobPhase::Resume) return false;
  job->phase = ConfigJobPhase::Done;
  return true;
}

bool config_job_need_main(ConfigJobView *job) {
  if (job == nullptr || job->phase != ConfigJobPhase::Queued) return false;
  if (job->kind != ConfigJobKind::Post && job->kind != ConfigJobKind::Reset) return false;
  job->phase = ConfigJobPhase::NeedMain;
  return true;
}

bool config_job_applied(ConfigJobView *job, bool ok) {
  if (job == nullptr || job->phase != ConfigJobPhase::NeedMain) return false;
  job->main_ok = ok;
  job->phase = ConfigJobPhase::Resume;
  return true;
}

bool config_job_release(ConfigJobView *job) {
  if (job == nullptr || job->phase != ConfigJobPhase::Done) return false;
  job->phase = ConfigJobPhase::Idle;
  job->kind = ConfigJobKind::None;
  job->main_ok = false;
  return true;
}
