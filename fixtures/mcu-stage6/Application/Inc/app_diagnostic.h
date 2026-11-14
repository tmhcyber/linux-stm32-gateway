#ifndef APP_DIAGNOSTIC_H
#define APP_DIAGNOSTIC_H
#include <stddef.h>
#include <stdint.h>
#include "bsp_system_diagnostics.h"
#include "app_system_monitor.h"
#include "app_watchdog_gate.h"
#ifndef APP_DIAGNOSTICS_ENABLED
#define APP_DIAGNOSTICS_ENABLED 0
#endif
#if (APP_DIAGNOSTICS_ENABLED != 0) && (APP_DIAGNOSTICS_ENABLED != 1)
#error APP_DIAGNOSTICS_ENABLED_must_be_0_or_1
#endif
#define APP_DIAGNOSTIC_PERIOD_MS 5000U
/* Wire d=1; all values are uint32, hex fields have exactly eight uppercase digits. */
typedef struct {
  uint32_t up_ms, reset_captured, reset_raw, reset_reasons;
  uint32_t fault_status, fault_type, frame_valid, frame_reject, pc, cfsr, hfsr;
  uint32_t monitor_valid, health, active, observed;
  uint32_t watchdog_initialized, watchdog, latched;
} App_Diagnostic_t;
void App_Diagnostic_Map(App_Diagnostic_t *out, const BSP_SystemBootReport_t *boot,
                        const App_SystemMonitorReport_t *monitor,
                        const App_WatchdogGateReport_t *gate, uint32_t now_ms);
/* Returns 1 only for a complete CRLF frame; zero length on failure. */
int App_Diagnostic_Format(const App_Diagnostic_t *r, char *buffer, size_t capacity,
                          uint32_t *length);
#endif
