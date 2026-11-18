#pragma once

// Shared by config_server.cpp, config_conn.cpp, and config_worker.cpp.
// The sketch does not include this.

#include <stddef.h>
#include <stdint.h>

#include <atomic>

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include "config_http.h"
#include "config_job.h"
#include "config_route.h"
#include "settings.h"

enum class Conn : uint8_t { Idle, Header, Body, Drain, Wait, Send };

enum class After : uint8_t { Error, Status, Reset, TimingReset };

struct Outgoing {
  char hdr[512];
  size_t hdr_len;
  size_t hdr_sent;
  const char *body;
  size_t body_len;
  size_t body_sent;
  char *owned;
};

extern Conn g_conn;
extern uint32_t g_io_ms;
extern char g_header[kHttpHeaderMax];
extern size_t g_header_len;
extern char *g_body;
extern size_t g_body_len;
extern size_t g_body_got;
extern bool g_cors;
extern ConfigRouteId g_route;
extern After g_after;
extern int g_after_code;
extern const char *g_after_msg;
extern size_t g_drain_left;
extern Outgoing g_out;

extern std::atomic<uint8_t> g_job_phase;
extern ConfigJobKind g_job_kind;
extern char *g_job_body;
extern size_t g_job_len;
extern int g_job_code;
extern char *g_job_resp;
extern bool g_job_main_ok;
extern unsigned g_job_count;
extern TaskHandle_t g_worker;
extern bool g_drop_result;

char *alloc_chars(size_t n);
char *json_error(const char *msg);
void begin_send(int code, const char *type, const char *body, size_t len, char *owned, bool cors);
void begin_error(int code, const char *msg, bool cors);
void drop_conn();

bool config_token_ok(const HttpRequest &req);
void config_conn_listen();
void config_conn_halt();
void config_conn_abandon_wait();
void config_conn_service();

char *config_format_timing_json(bool with_ok);
void config_worker_start();
void config_worker_service();
void queue_or_error(ConfigJobKind kind, char *body, size_t len);
