#ifndef BSP_SYSTEM_TIMING_H
#define BSP_SYSTEM_TIMING_H

#include <stdint.h>

typedef enum
{
  BSP_SYSTEM_TIMING_STATUS_NOT_RUN = 0,
  BSP_SYSTEM_TIMING_STATUS_OK,
  BSP_SYSTEM_TIMING_STATUS_UNAVAILABLE
} BSP_SystemTimingStatus_t;

BSP_SystemTimingStatus_t BSP_SystemTiming_Init(void);
uint32_t BSP_SystemTiming_GetCycles(void);
uint32_t BSP_SystemTiming_GetCoreClockHz(void);
uint32_t BSP_SystemTiming_GetReadOverheadCycles(void);

#endif
