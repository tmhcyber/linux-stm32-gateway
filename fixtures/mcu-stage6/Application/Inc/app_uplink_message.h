#ifndef APP_UPLINK_MESSAGE_H
#define APP_UPLINK_MESSAGE_H

#include <stdint.h>

#include "app_snapshot_consumer.h"

typedef enum
{
  APP_UPLINK_REASON_NOT_SET = 0,
  APP_UPLINK_REASON_EVENT,
  APP_UPLINK_REASON_HEARTBEAT
} App_UplinkReason_t;

typedef struct
{
  App_UplinkReason_t reason;
  App_SnapshotEventFlags_t flags;
  uint32_t skipped_count;
  uint32_t coalesced_count;
  App_DataSnapshot_t snapshot;
} App_UplinkMessage_t;

#endif
