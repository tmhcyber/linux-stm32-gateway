#ifndef APP_BRINGUP_H
#define APP_BRINGUP_H

#include <stdint.h>

typedef enum
{
  APP_BRINGUP_PROBE_NOT_RUN = 0,
  APP_BRINGUP_PROBE_OK,
  APP_BRINGUP_PROBE_NACK,
  APP_BRINGUP_PROBE_TIMEOUT,
  APP_BRINGUP_PROBE_BUSY,
  APP_BRINGUP_PROBE_BUS_ERROR,
  APP_BRINGUP_PROBE_INVALID_ARGUMENT
} App_BringupProbeStatus_t;

typedef struct
{
  App_BringupProbeStatus_t i2c1_init;
  App_BringupProbeStatus_t board_eeprom_0x50;
  App_BringupProbeStatus_t sht30_0x44;
  uint32_t completed;
} App_BringupReport_t;

extern volatile App_BringupReport_t g_app_bringup_report;

void App_Bringup_RunOnce(void);

#endif
