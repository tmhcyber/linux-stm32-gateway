#ifndef APP_SHT30_BRINGUP_H
#define APP_SHT30_BRINGUP_H

#include <stdint.h>

typedef enum
{
  APP_SHT30_BRINGUP_STATE_IDLE = 0,
  APP_SHT30_BRINGUP_STATE_MEASUREMENT_STARTED,
  APP_SHT30_BRINGUP_STATE_WAITING,
  APP_SHT30_BRINGUP_STATE_COMPLETE,
  APP_SHT30_BRINGUP_STATE_ERROR
} App_SHT30_BringupState_t;

typedef enum
{
  APP_SHT30_STATUS_NOT_RUN = 0,
  APP_SHT30_STATUS_OK,
  APP_SHT30_STATUS_INVALID_ARGUMENT,
  APP_SHT30_STATUS_NACK,
  APP_SHT30_STATUS_TIMEOUT,
  APP_SHT30_STATUS_BUSY,
  APP_SHT30_STATUS_BUS_ERROR,
  APP_SHT30_STATUS_TEMPERATURE_CRC_ERROR,
  APP_SHT30_STATUS_HUMIDITY_CRC_ERROR
} App_SHT30_Status_t;

typedef struct
{
  App_SHT30_BringupState_t state;
  App_SHT30_Status_t status;
  uint32_t started_at_ms;
  uint32_t ready_at_ms;
  uint16_t temperature_raw;
  uint16_t humidity_raw;
  int32_t temperature_milli_c;
  uint32_t humidity_milli_percent;
  uint32_t completed;
} App_SHT30_BringupReport_t;

extern volatile App_SHT30_BringupReport_t g_app_sht30_bringup_report;

void App_SHT30_Bringup_Init(void);
void App_SHT30_Bringup_Process(uint32_t now_ms);

#endif
