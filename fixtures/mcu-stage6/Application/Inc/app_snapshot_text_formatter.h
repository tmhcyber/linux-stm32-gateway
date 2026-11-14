#ifndef APP_SNAPSHOT_TEXT_FORMATTER_H
#define APP_SNAPSHOT_TEXT_FORMATTER_H

#include <stdint.h>

#include "app_uplink_message.h"

#define APP_UPLINK_TEXT_PROTOCOL_VERSION  1U
#define APP_UPLINK_TEXT_BUFFER_SIZE       512U

typedef enum
{
  APP_UPLINK_TEXT_RESULT_OK = 0,
  APP_UPLINK_TEXT_RESULT_INVALID_ARGUMENT,
  APP_UPLINK_TEXT_RESULT_INVALID_MESSAGE,
  APP_UPLINK_TEXT_RESULT_UNSUPPORTED_VALUE,
  APP_UPLINK_TEXT_RESULT_BUFFER_TOO_SMALL
} App_UplinkTextResult_t;

App_UplinkTextResult_t App_SnapshotTextFormatter_Format(
  const App_UplinkMessage_t *message,
  char *buffer,
  uint32_t buffer_capacity,
  uint32_t *output_length);

#endif
