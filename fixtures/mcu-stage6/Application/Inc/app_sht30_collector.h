#ifndef APP_SHT30_COLLECTOR_H
#define APP_SHT30_COLLECTOR_H

#include <stdint.h>

/* Experimental policy values for the first periodic-collection exercise. */
#define APP_SHT30_SAMPLE_PERIOD_MS               1000U
#define APP_SHT30_MAX_RETRIES_PER_ROUND          2U
#define APP_SHT30_RETRY_DELAY_MS                 100U
#define APP_SHT30_OFFLINE_THRESHOLD_ROUNDS       3U
#define APP_SHT30_OFFLINE_PROBE_PERIOD_MS        2000U

typedef enum
{
  APP_SHT30_COLLECTOR_STATE_WAIT_SAMPLE = 0,
  APP_SHT30_COLLECTOR_STATE_WAIT_MEASUREMENT,
  APP_SHT30_COLLECTOR_STATE_WAIT_RETRY,
  APP_SHT30_COLLECTOR_STATE_WAIT_OFFLINE_PROBE,
  APP_SHT30_COLLECTOR_STATE_FAULT
} App_SHT30_CollectorState_t;

typedef enum
{
  APP_SHT30_DEVICE_UNKNOWN = 0,
  APP_SHT30_DEVICE_ONLINE,
  APP_SHT30_DEVICE_OFFLINE
} App_SHT30_DeviceState_t;

typedef enum
{
  APP_SHT30_QUALITY_NOT_AVAILABLE = 0,
  APP_SHT30_QUALITY_VALID,
  APP_SHT30_QUALITY_STALE,
  APP_SHT30_QUALITY_OFFLINE
} App_SHT30_Quality_t;

typedef enum
{
  APP_SHT30_COLLECTOR_STATUS_NOT_RUN = 0,
  APP_SHT30_COLLECTOR_STATUS_OK,
  APP_SHT30_COLLECTOR_STATUS_INVALID_ARGUMENT,
  APP_SHT30_COLLECTOR_STATUS_NACK,
  APP_SHT30_COLLECTOR_STATUS_TIMEOUT,
  APP_SHT30_COLLECTOR_STATUS_BUSY,
  APP_SHT30_COLLECTOR_STATUS_BUS_ERROR,
  APP_SHT30_COLLECTOR_STATUS_TEMPERATURE_CRC_ERROR,
  APP_SHT30_COLLECTOR_STATUS_HUMIDITY_CRC_ERROR,
  APP_SHT30_COLLECTOR_STATUS_DATA_SERVICE_ERROR
} App_SHT30_CollectorStatus_t;

typedef struct
{
  App_SHT30_CollectorState_t state;
  App_SHT30_DeviceState_t device_state;
  App_SHT30_Quality_t quality;
  App_SHT30_CollectorStatus_t last_status;
  uint32_t round_started_at_ms;
  uint32_t attempt_started_at_ms;
  uint32_t last_success_at_ms;
  uint32_t last_error_at_ms;
  uint16_t temperature_raw;
  uint16_t humidity_raw;
  int32_t temperature_milli_c;
  uint32_t humidity_milli_percent;
  uint32_t current_attempt;
  uint32_t total_round_count;
  uint32_t success_count;
  uint32_t failed_round_count;
  uint32_t retry_count;
  uint32_t consecutive_failed_rounds;
  uint32_t recovery_count;
  uint32_t invalid_argument_count;
  uint32_t nack_count;
  uint32_t timeout_count;
  uint32_t busy_count;
  uint32_t bus_error_count;
  uint32_t temperature_crc_error_count;
  uint32_t humidity_crc_error_count;
  uint32_t data_service_error_count;
  uint32_t sequence;
  uint32_t has_valid_measurement;
} App_SHT30_CollectorReport_t;

extern volatile App_SHT30_CollectorReport_t g_app_sht30_collector_report;

void App_SHT30_Collector_Init(void);
void App_SHT30_Collector_Process(uint32_t now_ms);

#endif
