#ifndef GATEWAY_OUTPUT_H
#define GATEWAY_OUTPUT_H
#include "framer.h"
#include "storage.h"
#include "mqtt.h"
#include <stdio.h>
typedef struct {
 FILE *diagnostics;
 StoredRecord latest_diagnostic;
 uint64_t diagnostic_received;
 double last_diagnostic;
 FILE *raw, *records, *events; Framer framer;
 double start, last_valid; int io_error, silent, have_seq;
 uint32_t previous_seq, previous_ms;
 uint64_t valid, errors[P_COUNT], open_errors, link_errors, opens, silent_count;
 uint64_t seq_repeat, seq_next, seq_jump, seq_wrap, seq_backward, ms_wrap;
 char started_utc[40];
 Storage *storage;
 StorageStats storage_stats;
 int db_enabled;
 uint64_t event_id;
 const char *directory;
 double last_storage_status;
 Mqtt *mqtt;
 MqttStats mqtt_stats;
 int mqtt_enabled, port_open;
 double last_mqtt_status;
 uint64_t mqtt_observed_failures, mqtt_observed_connacks;
 StoredRecord latest;
 char gateway_run_id[33];
 int journal;
} Output;
double monotonic_now(void);
void utc_now(char *buffer, size_t size);
int output_open(Output *o, const char *dir, int aligned);
void output_event(Output *o, const char *kind, int detail);
void output_bytes(Output *o, const unsigned char *data, size_t size);
void output_silence(Output *o, double now);
void output_storage_status(Output *o, int force);
void output_mqtt_status(Output *o, int force);
int output_finish(Output *o, const char *dir, int code, int sig, int complete,
                  double duration, const char *mode, int discard);
#endif
