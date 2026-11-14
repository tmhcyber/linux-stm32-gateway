#ifndef APP_SNAPSHOT_CONSUMER_H
#define APP_SNAPSHOT_CONSUMER_H

#include <stdint.h>

#include "app_data_service.h"

typedef uint32_t App_SnapshotEventFlags_t;

#define APP_SNAPSHOT_EVENT_NONE                 0U
#define APP_SNAPSHOT_EVENT_INITIAL_SNAPSHOT     (1U << 0)
#define APP_SNAPSHOT_EVENT_SNAPSHOT_CHANGED     (1U << 1)
#define APP_SNAPSHOT_EVENT_NEW_VALID_DATA       (1U << 2)
#define APP_SNAPSHOT_EVENT_QUALITY_CHANGED      (1U << 3)
#define APP_SNAPSHOT_EVENT_STATUS_CHANGED       (1U << 4)
#define APP_SNAPSHOT_EVENT_SOURCE_OFFLINE       (1U << 5)
#define APP_SNAPSHOT_EVENT_SOURCE_RECOVERED     (1U << 6)
#define APP_SNAPSHOT_EVENT_VERSION_GAP          (1U << 7)

typedef struct
{
  App_SnapshotEventFlags_t flags;
  uint32_t skipped_count;
  App_DataSnapshot_t snapshot;
} App_SnapshotEvent_t;

typedef struct
{
  uint8_t has_previous_snapshot;
  App_DataSnapshot_t previous_snapshot;
  uint32_t total_skipped_count;
} App_SnapshotConsumer_t;

void App_SnapshotConsumer_Init(App_SnapshotConsumer_t *consumer);

/*
 * Produces a change-triggered event carrying the complete current snapshot.
 * event_ready is zero when the stable snapshot has not changed.
 * A non-OK result produces no event and does not replace the comparison base.
 * Call only from the cooperative main loop, not from an interrupt handler.
 */
App_DataResult_t App_SnapshotConsumer_Process(
  App_SnapshotConsumer_t *consumer,
  App_SnapshotEvent_t *event,
  uint8_t *event_ready);

#endif
