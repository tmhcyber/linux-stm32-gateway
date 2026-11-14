#ifndef GATEWAY_MQTT_H
#define GATEWAY_MQTT_H
#include "storage.h"
#define MQTT_JSON_CAPACITY 4096
typedef struct Mqtt Mqtt;
typedef struct {
 uint64_t received, overwritten[3], attempts[3], pubacks[3];
 uint64_t connects, connacks, failures, recoveries, ack_timeouts, connect_timeouts;
 unsigned pending, in_flight, high_water;
 int connected, stopped, last_error, format_errors, diagnostic_io_error, offline_acked;
} MqttStats;
int mqtt_library_init(void);
void mqtt_library_cleanup(void);
int mqtt_identifier_valid(const char *id);
Mqtt *mqtt_start(int port, const char *gateway, const char *device, const char *dir, const char *run);
void mqtt_record(Mqtt *m, const StoredRecord *record);
void mqtt_state(Mqtt *m, int serial_open, int silent, double age, uint64_t received,
                int db_enabled, const StorageStats *storage, int force,
                const StoredRecord *diagnostic, uint64_t diagnostic_received, double diagnostic_age);
void mqtt_snapshot(Mqtt *m, MqttStats *stats);
void mqtt_finish(Mqtt *m, MqttStats *stats);
void mqtt_write_stats(FILE *f, const MqttStats *stats);
int mqtt_has_errors(const MqttStats *stats);
#endif
