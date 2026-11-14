#include "app_diagnostic.h"
#include <stdio.h>
#include <string.h>
void App_Diagnostic_Map(App_Diagnostic_t *out, const BSP_SystemBootReport_t *boot,
                        const App_SystemMonitorReport_t *monitor,
                        const App_WatchdogGateReport_t *gate, uint32_t now_ms)
{
  (void)memset(out, 0, sizeof(*out));
  out->up_ms = now_ms;
  out->reset_captured = boot->reset.captured;
  out->reset_raw = boot->reset.raw_rcc_csr;
  out->reset_reasons = boot->reset.reasons;
  out->fault_status = (uint32_t)boot->previous_fault_status;
  if (boot->previous_fault_status == BSP_SYSTEM_FAULT_RECORD_VALID) {
    out->fault_type = boot->previous_fault.fault_type;
    out->frame_valid = boot->previous_fault.frame_valid;
    out->frame_reject = boot->previous_fault.frame_reject_reasons;
    if (out->frame_valid != 0U) out->pc = boot->previous_fault.pc;
    out->cfsr = boot->previous_fault.cfsr;
    out->hfsr = boot->previous_fault.hfsr;
  }
  out->monitor_valid = monitor->monitor_valid;
  out->health = (uint32_t)monitor->health_state;
  out->active = monitor->active_reasons;
  out->observed = monitor->observed_reasons;
  out->watchdog_initialized = gate->initialized;
  out->watchdog = (uint32_t)gate->decision;
  out->latched = gate->latched_reasons;
}
int App_Diagnostic_Format(const App_Diagnostic_t *r, char *buffer, size_t capacity,
                          uint32_t *length)
{
  int n;
  if (length == NULL) return 0;
  *length = 0U;
  if ((r == NULL) || (buffer == NULL) || (capacity == 0U)) return 0;
  n = snprintf(buffer, capacity,
    "d=1;up_ms=%lu;reset_captured=%lu;reset_raw=0x%08lX;reset_reasons=0x%08lX;"
    "fault_status=%lu;fault_type=%lu;frame_valid=%lu;frame_reject=0x%08lX;"
    "pc=0x%08lX;cfsr=0x%08lX;hfsr=0x%08lX;monitor_valid=%lu;health=%lu;"
    "active=0x%08lX;observed=0x%08lX;watchdog_initialized=%lu;watchdog=%lu;latched=0x%08lX;\r\n",
    (unsigned long)r->up_ms, (unsigned long)r->reset_captured,
    (unsigned long)r->reset_raw, (unsigned long)r->reset_reasons,
    (unsigned long)r->fault_status, (unsigned long)r->fault_type,
    (unsigned long)r->frame_valid, (unsigned long)r->frame_reject,
    (unsigned long)r->pc, (unsigned long)r->cfsr, (unsigned long)r->hfsr,
    (unsigned long)r->monitor_valid, (unsigned long)r->health,
    (unsigned long)r->active, (unsigned long)r->observed,
    (unsigned long)r->watchdog_initialized, (unsigned long)r->watchdog,
    (unsigned long)r->latched);
  if ((n < 2) || (n > 511) || ((size_t)n >= capacity)) {
    buffer[0] = '\0';
    return 0;
  }
  *length = (uint32_t)n;
  return 1;
}
