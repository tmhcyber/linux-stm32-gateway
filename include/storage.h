#ifndef GATEWAY_STORAGE_H
#define GATEWAY_STORAGE_H
#include "protocol.h"
#include "diagnostic.h"
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#define STORAGE_CAPACITY 256
/* Value object: never contains a pointer to producer memory. */
typedef struct {
 Record record;
 Diagnostic diagnostic;
 int is_diagnostic;
 char gateway_run_id[33];
 uint64_t rx_id, session, raw_start, raw_end;
 double elapsed_s;
 char utc[40], relation[16];
 unsigned char body[MAX_BODY];
 size_t body_len;
 int is_event;
 int64_t event_id; /* main > 0; storage diagnostics < 0 */
 char kind[48];
 int detail;
} StoredRecord;
typedef struct {
 uint64_t mcu_submitted, mcu_dropped_full, mcu_rejected, mcu_committed, mcu_failed;
 uint64_t retention_diagnostics;
 uint64_t submitted, dropped_full, rejected, committed, commits, last_rx_id;
 uint64_t failed_records;
 uint64_t event_submitted, event_dropped_full, event_rejected, event_committed, event_failed;
 uint64_t worker_events_committed, diagnostic_dropped, failures, recoveries, busy_retries;
 uint64_t retention_frames, retention_points, retention_events, retention_runs, retention_batches;
 size_t queued, in_flight, high_water;
 int error, last_error, degraded, stopped, diagnostic_io_error;
 char run_id[33];
 uint64_t query_submitted, query_completed, query_failed, query_expired, query_cancelled;
 size_t query_outstanding, query_high_water;
 double query_max_ms;
} StorageStats;
/* Queue fields and all live statistics are protected by mutex. SQLite is worker-only. */
typedef struct {
 pthread_mutex_t mutex;
 pthread_cond_t changed;
 StoredRecord items[STORAGE_CAPACITY];
 size_t head, count, capacity;
 int closed;
} RecordQueue;
int queue_init(RecordQueue *q, size_t capacity);
int queue_push(RecordQueue *q, const StoredRecord *r); /* caller holds mutex; 1 accepted, 0 full, -1 closed */
int queue_pop_locked(RecordQueue *q, StoredRecord *r); /* caller holds mutex */
void queue_destroy(RecordQueue *q);
typedef struct Storage Storage;
Storage *storage_start(const char *path, const char *utc, double monotonic_start,
                       const char *mode, const char *output_dir, StorageStats *initial);
void storage_submit(Storage *s, const StoredRecord *r);
void storage_snapshot(Storage *s, StorageStats *stats);
/* Close producer, drain, commit, join, then free. No caller uses s afterward. */
void storage_finish(Storage *s, int code, int sig, int complete, StorageStats *stats);
int storage_has_errors(const StorageStats *stats);
void storage_write_stats(FILE *f, const StorageStats *stats);
#endif
