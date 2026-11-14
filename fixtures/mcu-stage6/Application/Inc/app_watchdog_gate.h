#ifndef APP_WATCHDOG_GATE_H
#define APP_WATCHDOG_GATE_H

#include <stdint.h>

#include "app_system_monitor.h"

#define APP_WATCHDOG_GATE_REFRESH_PERIOD_MS  250U

#define APP_WATCHDOG_GATE_ALLOW_REASONS  \
  (APP_SYSTEM_HEALTH_REASON_LOOP_WARNING | \
   APP_SYSTEM_HEALTH_REASON_DATA_STALE | \
   APP_SYSTEM_HEALTH_REASON_SOURCE_OFFLINE)

#define APP_WATCHDOG_GATE_HOLD_REASONS  \
  (APP_SYSTEM_HEALTH_REASON_LOOP_HARD_OVERRUN | \
   APP_SYSTEM_HEALTH_REASON_UPLINK_STALLED | \
   APP_SYSTEM_HEALTH_REASON_UPLINK_ERROR)

#define APP_WATCHDOG_GATE_LATCH_REASONS  \
  (APP_SYSTEM_HEALTH_REASON_COLLECTOR_FAULT | \
   APP_SYSTEM_HEALTH_REASON_MONITOR_INVALID)

#define APP_WATCHDOG_GATE_KNOWN_REASONS  \
  (APP_WATCHDOG_GATE_ALLOW_REASONS | \
   APP_WATCHDOG_GATE_HOLD_REASONS | \
   APP_WATCHDOG_GATE_LATCH_REASONS)

#define APP_WATCHDOG_GATE_INTERNAL_INPUT_INVALID   (1UL << 16U)
#define APP_WATCHDOG_GATE_INTERNAL_UNKNOWN_REASON  (1UL << 17U)
#define APP_WATCHDOG_GATE_INTERNAL_REFRESH_FAILED  (1UL << 18U)

typedef enum
{
  APP_WATCHDOG_GATE_RESULT_OK = 0,
  APP_WATCHDOG_GATE_RESULT_INVALID_ARGUMENT,
  APP_WATCHDOG_GATE_RESULT_INVALID_INPUT,
  APP_WATCHDOG_GATE_RESULT_NOT_INITIALIZED,
  APP_WATCHDOG_GATE_RESULT_INVALID_STATE
} App_WatchdogGateResult_t;

typedef enum
{
  APP_WATCHDOG_GATE_DECISION_WAIT_PERIOD = 0,
  APP_WATCHDOG_GATE_DECISION_REFRESH_DUE,
  APP_WATCHDOG_GATE_DECISION_HOLD,
  APP_WATCHDOG_GATE_DECISION_LATCHED
} App_WatchdogGateDecision_t;

typedef enum
{
  APP_WATCHDOG_GATE_REFRESH_SUCCESS = 0,
  APP_WATCHDOG_GATE_REFRESH_FAILURE
} App_WatchdogGateRefreshResult_t;

typedef struct
{
  uint8_t monitor_evaluation_valid;
  uint32_t active_reasons;
} App_WatchdogGateInputs_t;

typedef struct
{
  uint8_t initialized;
  App_WatchdogGateDecision_t decision;
  uint32_t active_reasons;
  uint32_t active_hold_reasons;
  uint32_t latched_reasons;
  uint32_t unknown_active_reasons;
  uint32_t last_successful_refresh_at_ms;
  uint32_t refresh_completed_count;
  uint32_t refresh_failed_count;
  uint32_t evaluation_count;
  uint32_t decision_transition_count;
  uint32_t decision_since_ms;
} App_WatchdogGateReport_t;

extern volatile uint32_t g_app_watchdog_gate_report_version;
extern volatile App_WatchdogGateReport_t g_app_watchdog_gate_report;

App_WatchdogGateResult_t App_WatchdogGate_Init(uint32_t now_ms);
App_WatchdogGateResult_t App_WatchdogGate_Evaluate(
  const App_WatchdogGateInputs_t *inputs,
  uint32_t now_ms,
  App_WatchdogGateDecision_t *decision);
App_WatchdogGateResult_t App_WatchdogGate_RecordRefreshResult(
  App_WatchdogGateRefreshResult_t refresh_result,
  uint32_t now_ms);

#endif
