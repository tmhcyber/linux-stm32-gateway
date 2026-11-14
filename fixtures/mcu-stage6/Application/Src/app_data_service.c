#include "app_data_service.h"

static volatile App_DataPoint_t s_points[APP_DATA_POINT_COUNT];
static volatile uint32_t s_snapshot_version;
static volatile uint32_t s_initialized;
static uint32_t s_next_sequence;

static void App_DataService_BeginWrite(void)
{
  ++s_snapshot_version;
}

static void App_DataService_EndWrite(void)
{
  ++s_snapshot_version;
}

static void App_DataService_SetInitialPoint(
  uint32_t index,
  App_DataSourceId_t source_id,
  App_DataPointId_t point_id,
  App_DataUnit_t unit)
{
  s_points[index].source_id = source_id;
  s_points[index].point_id = point_id;
  s_points[index].raw_value = 0;
  s_points[index].scale = APP_DATA_SCALE_MILLI;
  s_points[index].unit = unit;
  s_points[index].quality = APP_DATA_QUALITY_NOT_AVAILABLE;
  s_points[index].last_status = APP_DATA_STATUS_NOT_RUN;
  s_points[index].timestamp_ms = 0U;
  s_points[index].status_timestamp_ms = 0U;
  s_points[index].sequence = 0U;
}

static int32_t App_DataService_FindPoint(
  App_DataSourceId_t source_id,
  App_DataPointId_t point_id)
{
  uint32_t index;

  for (index = 0U; index < APP_DATA_POINT_COUNT; ++index)
  {
    if ((s_points[index].source_id == source_id) &&
        (s_points[index].point_id == point_id))
    {
      return (int32_t)index;
    }
  }

  return -1;
}

static void App_DataService_CopyPoint(
  App_DataPoint_t *destination,
  const volatile App_DataPoint_t *source)
{
  destination->source_id = source->source_id;
  destination->point_id = source->point_id;
  destination->raw_value = source->raw_value;
  destination->scale = source->scale;
  destination->unit = source->unit;
  destination->quality = source->quality;
  destination->last_status = source->last_status;
  destination->timestamp_ms = source->timestamp_ms;
  destination->status_timestamp_ms = source->status_timestamp_ms;
  destination->sequence = source->sequence;
}

void App_DataService_Init(void)
{
  s_initialized = 0U;
  s_snapshot_version = 1U;
  s_next_sequence = 0U;

  App_DataService_SetInitialPoint(0U,
                                  APP_DATA_SOURCE_SHT30,
                                  APP_DATA_POINT_TEMPERATURE,
                                  APP_DATA_UNIT_DEGREE_CELSIUS);
  App_DataService_SetInitialPoint(1U,
                                  APP_DATA_SOURCE_SHT30,
                                  APP_DATA_POINT_HUMIDITY,
                                  APP_DATA_UNIT_PERCENT_RH);

  s_snapshot_version = 2U;
  s_initialized = 1U;
}

App_DataResult_t App_DataService_PublishValues(
  const App_DataValueUpdate_t *updates,
  uint32_t update_count,
  uint32_t timestamp_ms)
{
  int32_t indices[APP_DATA_POINT_COUNT];
  uint32_t index;
  uint32_t compare_index;
  uint32_t sequence;

  if (s_initialized == 0U)
  {
    return APP_DATA_RESULT_NOT_INITIALIZED;
  }

  if ((updates == 0) ||
      (update_count == 0U) ||
      (update_count > APP_DATA_POINT_COUNT))
  {
    return APP_DATA_RESULT_INVALID_ARGUMENT;
  }

  for (index = 0U; index < update_count; ++index)
  {
    indices[index] = App_DataService_FindPoint(updates[index].source_id,
                                                updates[index].point_id);
    if (indices[index] < 0)
    {
      return APP_DATA_RESULT_NOT_FOUND;
    }

    if (updates[index].unit == APP_DATA_UNIT_NONE)
    {
      return APP_DATA_RESULT_INVALID_ARGUMENT;
    }

    for (compare_index = 0U; compare_index < index; ++compare_index)
    {
      if (indices[index] == indices[compare_index])
      {
        return APP_DATA_RESULT_INVALID_ARGUMENT;
      }
    }
  }

  ++s_next_sequence;
  if (s_next_sequence == 0U)
  {
    ++s_next_sequence;
  }
  sequence = s_next_sequence;

  App_DataService_BeginWrite();

  for (index = 0U; index < update_count; ++index)
  {
    s_points[indices[index]].raw_value = updates[index].raw_value;
    s_points[indices[index]].scale = updates[index].scale;
    s_points[indices[index]].unit = updates[index].unit;
    s_points[indices[index]].quality = APP_DATA_QUALITY_VALID;
    s_points[indices[index]].last_status = APP_DATA_STATUS_OK;
    s_points[indices[index]].timestamp_ms = timestamp_ms;
    s_points[indices[index]].status_timestamp_ms = timestamp_ms;
    s_points[indices[index]].sequence = sequence;
  }

  App_DataService_EndWrite();
  return APP_DATA_RESULT_OK;
}

App_DataResult_t App_DataService_UpdateSourceQuality(
  App_DataSourceId_t source_id,
  App_DataQuality_t quality,
  App_DataStatus_t status,
  uint32_t status_timestamp_ms)
{
  uint32_t index;
  uint32_t found;

  if (s_initialized == 0U)
  {
    return APP_DATA_RESULT_NOT_INITIALIZED;
  }

  if ((source_id == APP_DATA_SOURCE_UNKNOWN) ||
      (quality == APP_DATA_QUALITY_VALID) ||
      (status == APP_DATA_STATUS_OK))
  {
    return APP_DATA_RESULT_INVALID_ARGUMENT;
  }

  found = 0U;
  for (index = 0U; index < APP_DATA_POINT_COUNT; ++index)
  {
    if (s_points[index].source_id == source_id)
    {
      found = 1U;
      break;
    }
  }

  if (found == 0U)
  {
    return APP_DATA_RESULT_NOT_FOUND;
  }

  App_DataService_BeginWrite();

  for (index = 0U; index < APP_DATA_POINT_COUNT; ++index)
  {
    if (s_points[index].source_id == source_id)
    {
      s_points[index].quality = quality;
      s_points[index].last_status = status;
      s_points[index].status_timestamp_ms = status_timestamp_ms;
    }
  }

  App_DataService_EndWrite();
  return APP_DATA_RESULT_OK;
}

App_DataResult_t App_DataService_GetSnapshot(
  App_DataSnapshot_t *snapshot)
{
  uint32_t attempt;
  uint32_t index;
  uint32_t version_before;
  uint32_t version_after;

  if (snapshot == 0)
  {
    return APP_DATA_RESULT_INVALID_ARGUMENT;
  }

  if (s_initialized == 0U)
  {
    return APP_DATA_RESULT_NOT_INITIALIZED;
  }

  for (attempt = 0U; attempt < APP_DATA_SNAPSHOT_MAX_RETRIES; ++attempt)
  {
    version_before = s_snapshot_version;
    if ((version_before & 1U) != 0U)
    {
      continue;
    }

    for (index = 0U; index < APP_DATA_POINT_COUNT; ++index)
    {
      App_DataService_CopyPoint(&snapshot->points[index], &s_points[index]);
    }

    version_after = s_snapshot_version;
    if ((version_before == version_after) &&
        ((version_after & 1U) == 0U))
    {
      snapshot->snapshot_version = version_after;
      snapshot->point_count = APP_DATA_POINT_COUNT;
      return APP_DATA_RESULT_OK;
    }
  }

  return APP_DATA_RESULT_BUSY;
}
