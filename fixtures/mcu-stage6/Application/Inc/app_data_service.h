#ifndef APP_DATA_SERVICE_H
#define APP_DATA_SERVICE_H

#include <stdint.h>

#define APP_DATA_POINT_COUNT              2U
#define APP_DATA_SNAPSHOT_MAX_RETRIES     3U
#define APP_DATA_SCALE_MILLI              (-3)

typedef enum
{
  APP_DATA_SOURCE_UNKNOWN = 0,
  APP_DATA_SOURCE_SHT30
} App_DataSourceId_t;

typedef enum
{
  APP_DATA_POINT_UNKNOWN = 0,
  APP_DATA_POINT_TEMPERATURE,
  APP_DATA_POINT_HUMIDITY
} App_DataPointId_t;

typedef enum
{
  APP_DATA_UNIT_NONE = 0,
  APP_DATA_UNIT_DEGREE_CELSIUS,
  APP_DATA_UNIT_PERCENT_RH
} App_DataUnit_t;

typedef enum
{
  APP_DATA_QUALITY_NOT_AVAILABLE = 0,
  APP_DATA_QUALITY_VALID,
  APP_DATA_QUALITY_STALE,
  APP_DATA_QUALITY_OFFLINE
} App_DataQuality_t;

typedef enum
{
  APP_DATA_STATUS_NOT_RUN = 0,
  APP_DATA_STATUS_OK,
  APP_DATA_STATUS_INVALID_ARGUMENT,
  APP_DATA_STATUS_NACK,
  APP_DATA_STATUS_TIMEOUT,
  APP_DATA_STATUS_BUSY,
  APP_DATA_STATUS_BUS_ERROR,
  APP_DATA_STATUS_TEMPERATURE_CRC_ERROR,
  APP_DATA_STATUS_HUMIDITY_CRC_ERROR
} App_DataStatus_t;

typedef enum
{
  APP_DATA_RESULT_OK = 0,
  APP_DATA_RESULT_INVALID_ARGUMENT,
  APP_DATA_RESULT_NOT_INITIALIZED,
  APP_DATA_RESULT_NOT_FOUND,
  APP_DATA_RESULT_BUSY
} App_DataResult_t;

typedef struct
{
  App_DataSourceId_t source_id;
  App_DataPointId_t point_id;
  int32_t raw_value;
  int8_t scale;
  App_DataUnit_t unit;
} App_DataValueUpdate_t;

typedef struct
{
  App_DataSourceId_t source_id;
  App_DataPointId_t point_id;
  int32_t raw_value;
  int8_t scale;
  App_DataUnit_t unit;
  App_DataQuality_t quality;
  App_DataStatus_t last_status;
  uint32_t timestamp_ms;
  uint32_t status_timestamp_ms;
  uint32_t sequence;
} App_DataPoint_t;

typedef struct
{
  uint32_t snapshot_version;
  uint32_t point_count;
  App_DataPoint_t points[APP_DATA_POINT_COUNT];
} App_DataSnapshot_t;

void App_DataService_Init(void);

/* Only the cooperative main-loop writer may call the publish APIs. */
App_DataResult_t App_DataService_PublishValues(
  const App_DataValueUpdate_t *updates,
  uint32_t update_count,
  uint32_t timestamp_ms);

App_DataResult_t App_DataService_UpdateSourceQuality(
  App_DataSourceId_t source_id,
  App_DataQuality_t quality,
  App_DataStatus_t status,
  uint32_t status_timestamp_ms);

/*
 * Copies a stable snapshot without exposing internal writable storage.
 * APP_DATA_RESULT_BUSY means the partial destination must be discarded.
 * Do not call this bounded-retry API from an interrupt handler.
 */
App_DataResult_t App_DataService_GetSnapshot(
  App_DataSnapshot_t *snapshot);

#endif
