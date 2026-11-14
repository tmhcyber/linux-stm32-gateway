#include "app_snapshot_text_formatter.h"

typedef struct
{
  char *buffer;
  uint32_t capacity;
  uint32_t length;
  uint8_t failed;
} App_TextWriter_t;

#define APP_SNAPSHOT_EVENT_KNOWN_FLAGS  \
  (APP_SNAPSHOT_EVENT_INITIAL_SNAPSHOT | \
   APP_SNAPSHOT_EVENT_SNAPSHOT_CHANGED | \
   APP_SNAPSHOT_EVENT_NEW_VALID_DATA |   \
   APP_SNAPSHOT_EVENT_QUALITY_CHANGED |  \
   APP_SNAPSHOT_EVENT_STATUS_CHANGED |   \
   APP_SNAPSHOT_EVENT_SOURCE_OFFLINE |   \
   APP_SNAPSHOT_EVENT_SOURCE_RECOVERED | \
   APP_SNAPSHOT_EVENT_VERSION_GAP)

static void App_TextWriter_AppendChar(App_TextWriter_t *writer, char value)
{
  if (writer->failed != 0U)
  {
    return;
  }

  if ((writer->length + 1U) >= writer->capacity)
  {
    writer->failed = 1U;
    return;
  }

  writer->buffer[writer->length] = value;
  ++writer->length;
}

static void App_TextWriter_AppendString(App_TextWriter_t *writer,
                                        const char *value)
{
  if (value == 0)
  {
    writer->failed = 1U;
    return;
  }

  while (*value != '\0')
  {
    App_TextWriter_AppendChar(writer, *value);
    ++value;
  }
}

static void App_TextWriter_AppendUInt32(App_TextWriter_t *writer,
                                        uint32_t value)
{
  char digits[10];
  uint32_t digit_count;

  digit_count = 0U;
  do
  {
    digits[digit_count] = (char)('0' + (value % 10U));
    ++digit_count;
    value /= 10U;
  } while (value != 0U);

  while (digit_count != 0U)
  {
    --digit_count;
    App_TextWriter_AppendChar(writer, digits[digit_count]);
  }
}

static void App_TextWriter_AppendInt32(App_TextWriter_t *writer,
                                       int32_t value)
{
  uint32_t magnitude;

  if (value < 0)
  {
    App_TextWriter_AppendChar(writer, '-');
    magnitude = (uint32_t)(-(value + 1)) + 1U;
  }
  else
  {
    magnitude = (uint32_t)value;
  }

  App_TextWriter_AppendUInt32(writer, magnitude);
}

static void App_TextWriter_AppendHex32(App_TextWriter_t *writer,
                                       uint32_t value)
{
  static const char hex_digits[] = "0123456789ABCDEF";
  uint32_t shift;

  App_TextWriter_AppendString(writer, "0x");
  for (shift = 28U; ; shift -= 4U)
  {
    App_TextWriter_AppendChar(writer,
                              hex_digits[(value >> shift) & 0x0FU]);
    if (shift == 0U)
    {
      break;
    }
  }
}

static const char *App_UplinkReasonName(App_UplinkReason_t reason)
{
  switch (reason)
  {
    case APP_UPLINK_REASON_EVENT:
      return "EVENT";

    case APP_UPLINK_REASON_HEARTBEAT:
      return "HEARTBEAT";

    case APP_UPLINK_REASON_NOT_SET:
    default:
      return 0;
  }
}

static const char *App_DataSourceName(App_DataSourceId_t source)
{
  switch (source)
  {
    case APP_DATA_SOURCE_SHT30:
      return "SHT30";

    case APP_DATA_SOURCE_UNKNOWN:
    default:
      return 0;
  }
}

static const char *App_DataPointName(App_DataPointId_t point)
{
  switch (point)
  {
    case APP_DATA_POINT_TEMPERATURE:
      return "TEMPERATURE";

    case APP_DATA_POINT_HUMIDITY:
      return "HUMIDITY";

    case APP_DATA_POINT_UNKNOWN:
    default:
      return 0;
  }
}

static const char *App_DataUnitName(App_DataUnit_t unit)
{
  switch (unit)
  {
    case APP_DATA_UNIT_DEGREE_CELSIUS:
      return "DEG_C";

    case APP_DATA_UNIT_PERCENT_RH:
      return "PERCENT_RH";

    case APP_DATA_UNIT_NONE:
    default:
      return 0;
  }
}

static const char *App_DataQualityName(App_DataQuality_t quality)
{
  switch (quality)
  {
    case APP_DATA_QUALITY_NOT_AVAILABLE:
      return "NOT_AVAILABLE";

    case APP_DATA_QUALITY_VALID:
      return "VALID";

    case APP_DATA_QUALITY_STALE:
      return "STALE";

    case APP_DATA_QUALITY_OFFLINE:
      return "OFFLINE";

    default:
      return 0;
  }
}

static const char *App_DataStatusName(App_DataStatus_t status)
{
  switch (status)
  {
    case APP_DATA_STATUS_NOT_RUN:
      return "NOT_RUN";

    case APP_DATA_STATUS_OK:
      return "OK";

    case APP_DATA_STATUS_INVALID_ARGUMENT:
      return "INVALID_ARGUMENT";

    case APP_DATA_STATUS_NACK:
      return "NACK";

    case APP_DATA_STATUS_TIMEOUT:
      return "TIMEOUT";

    case APP_DATA_STATUS_BUSY:
      return "BUSY";

    case APP_DATA_STATUS_BUS_ERROR:
      return "BUS_ERROR";

    case APP_DATA_STATUS_TEMPERATURE_CRC_ERROR:
      return "TEMPERATURE_CRC_ERROR";

    case APP_DATA_STATUS_HUMIDITY_CRC_ERROR:
      return "HUMIDITY_CRC_ERROR";

    default:
      return 0;
  }
}

static App_UplinkTextResult_t App_UplinkMessage_Validate(
  const App_UplinkMessage_t *message)
{
  uint32_t index;

  if (App_UplinkReasonName(message->reason) == 0)
  {
    return APP_UPLINK_TEXT_RESULT_UNSUPPORTED_VALUE;
  }

  if ((message->flags & ~APP_SNAPSHOT_EVENT_KNOWN_FLAGS) != 0U)
  {
    return APP_UPLINK_TEXT_RESULT_UNSUPPORTED_VALUE;
  }

  if ((message->reason == APP_UPLINK_REASON_EVENT) &&
      (message->flags == APP_SNAPSHOT_EVENT_NONE))
  {
    return APP_UPLINK_TEXT_RESULT_INVALID_MESSAGE;
  }

  if ((message->reason == APP_UPLINK_REASON_HEARTBEAT) &&
      ((message->flags != APP_SNAPSHOT_EVENT_NONE) ||
       (message->skipped_count != 0U)))
  {
    return APP_UPLINK_TEXT_RESULT_INVALID_MESSAGE;
  }

  if (((message->snapshot.snapshot_version & 1U) != 0U) ||
      (message->snapshot.point_count != APP_DATA_POINT_COUNT))
  {
    return APP_UPLINK_TEXT_RESULT_INVALID_MESSAGE;
  }

  for (index = 0U; index < message->snapshot.point_count; ++index)
  {
    const App_DataPoint_t *point;

    point = &message->snapshot.points[index];
    if ((App_DataSourceName(point->source_id) == 0) ||
        (App_DataPointName(point->point_id) == 0) ||
        (App_DataUnitName(point->unit) == 0) ||
        (App_DataQualityName(point->quality) == 0) ||
        (App_DataStatusName(point->last_status) == 0))
    {
      return APP_UPLINK_TEXT_RESULT_UNSUPPORTED_VALUE;
    }
  }

  return APP_UPLINK_TEXT_RESULT_OK;
}

static void App_TextWriter_AppendPoint(App_TextWriter_t *writer,
                                       uint32_t index,
                                       const App_DataPoint_t *point)
{
  App_TextWriter_AppendChar(writer, 'p');
  App_TextWriter_AppendUInt32(writer, index);
  App_TextWriter_AppendString(writer, ".src=");
  App_TextWriter_AppendString(writer, App_DataSourceName(point->source_id));
  App_TextWriter_AppendString(writer, ";p");
  App_TextWriter_AppendUInt32(writer, index);
  App_TextWriter_AppendString(writer, ".id=");
  App_TextWriter_AppendString(writer, App_DataPointName(point->point_id));
  App_TextWriter_AppendString(writer, ";p");
  App_TextWriter_AppendUInt32(writer, index);
  App_TextWriter_AppendString(writer, ".value=");
  App_TextWriter_AppendInt32(writer, point->raw_value);
  App_TextWriter_AppendString(writer, ";p");
  App_TextWriter_AppendUInt32(writer, index);
  App_TextWriter_AppendString(writer, ".scale=");
  App_TextWriter_AppendInt32(writer, (int32_t)point->scale);
  App_TextWriter_AppendString(writer, ";p");
  App_TextWriter_AppendUInt32(writer, index);
  App_TextWriter_AppendString(writer, ".unit=");
  App_TextWriter_AppendString(writer, App_DataUnitName(point->unit));
  App_TextWriter_AppendString(writer, ";p");
  App_TextWriter_AppendUInt32(writer, index);
  App_TextWriter_AppendString(writer, ".quality=");
  App_TextWriter_AppendString(writer, App_DataQualityName(point->quality));
  App_TextWriter_AppendString(writer, ";p");
  App_TextWriter_AppendUInt32(writer, index);
  App_TextWriter_AppendString(writer, ".status=");
  App_TextWriter_AppendString(writer, App_DataStatusName(point->last_status));
  App_TextWriter_AppendString(writer, ";p");
  App_TextWriter_AppendUInt32(writer, index);
  App_TextWriter_AppendString(writer, ".value_ms=");
  App_TextWriter_AppendUInt32(writer, point->timestamp_ms);
  App_TextWriter_AppendString(writer, ";p");
  App_TextWriter_AppendUInt32(writer, index);
  App_TextWriter_AppendString(writer, ".status_ms=");
  App_TextWriter_AppendUInt32(writer, point->status_timestamp_ms);
  App_TextWriter_AppendString(writer, ";p");
  App_TextWriter_AppendUInt32(writer, index);
  App_TextWriter_AppendString(writer, ".seq=");
  App_TextWriter_AppendUInt32(writer, point->sequence);
  App_TextWriter_AppendChar(writer, ';');
}

App_UplinkTextResult_t App_SnapshotTextFormatter_Format(
  const App_UplinkMessage_t *message,
  char *buffer,
  uint32_t buffer_capacity,
  uint32_t *output_length)
{
  App_TextWriter_t writer;
  App_UplinkTextResult_t result;
  uint32_t index;

  if (output_length != 0)
  {
    *output_length = 0U;
  }
  if ((buffer != 0) && (buffer_capacity != 0U))
  {
    buffer[0] = '\0';
  }

  if ((message == 0) || (buffer == 0) ||
      (buffer_capacity == 0U) || (output_length == 0))
  {
    return APP_UPLINK_TEXT_RESULT_INVALID_ARGUMENT;
  }

  result = App_UplinkMessage_Validate(message);
  if (result != APP_UPLINK_TEXT_RESULT_OK)
  {
    return result;
  }

  writer.buffer = buffer;
  writer.capacity = buffer_capacity;
  writer.length = 0U;
  writer.failed = 0U;

  App_TextWriter_AppendString(&writer, "v=");
  App_TextWriter_AppendUInt32(&writer, APP_UPLINK_TEXT_PROTOCOL_VERSION);
  App_TextWriter_AppendString(&writer, ";reason=");
  App_TextWriter_AppendString(&writer, App_UplinkReasonName(message->reason));
  App_TextWriter_AppendString(&writer, ";flags=");
  App_TextWriter_AppendHex32(&writer, message->flags);
  App_TextWriter_AppendString(&writer, ";gap=");
  App_TextWriter_AppendUInt32(&writer, message->skipped_count);
  App_TextWriter_AppendString(&writer, ";coal=");
  App_TextWriter_AppendUInt32(&writer, message->coalesced_count);
  App_TextWriter_AppendString(&writer, ";snap=");
  App_TextWriter_AppendUInt32(&writer, message->snapshot.snapshot_version);
  App_TextWriter_AppendString(&writer, ";n=");
  App_TextWriter_AppendUInt32(&writer, message->snapshot.point_count);
  App_TextWriter_AppendChar(&writer, ';');

  for (index = 0U; index < message->snapshot.point_count; ++index)
  {
    App_TextWriter_AppendPoint(&writer,
                               index,
                               &message->snapshot.points[index]);
  }

  App_TextWriter_AppendString(&writer, "\r\n");
  if (writer.failed != 0U)
  {
    buffer[0] = '\0';
    return APP_UPLINK_TEXT_RESULT_BUFFER_TOO_SMALL;
  }

  buffer[writer.length] = '\0';
  *output_length = writer.length;
  return APP_UPLINK_TEXT_RESULT_OK;
}
