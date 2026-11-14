#include "app_system_monitor.h"

#include <string.h>

#define APP_SYSTEM_MONITOR_CRITICAL_REASONS  \
  (APP_SYSTEM_HEALTH_REASON_LOOP_HARD_OVERRUN | \
   APP_SYSTEM_HEALTH_REASON_COLLECTOR_FAULT | \
   APP_SYSTEM_HEALTH_REASON_MONITOR_INVALID)

#define APP_SYSTEM_MONITOR_DEGRADED_REASONS  \
  (APP_SYSTEM_HEALTH_REASON_LOOP_WARNING | \
   APP_SYSTEM_HEALTH_REASON_DATA_STALE | \
   APP_SYSTEM_HEALTH_REASON_SOURCE_OFFLINE | \
   APP_SYSTEM_HEALTH_REASON_UPLINK_STALLED | \
   APP_SYSTEM_HEALTH_REASON_UPLINK_ERROR)

static App_SystemMonitorReport_t s_report;
static uint32_t s_initialized_at_ms;
static uint32_t s_last_uplink_error_count;
static uint32_t s_uplink_completed_count_at_error;
static uint8_t s_uplink_error_active;
static uint8_t s_initialized;

volatile uint32_t g_app_system_monitor_report_version;
volatile App_SystemMonitorReport_t g_app_system_monitor_report;

static void App_SystemMonitor_Publish(void)
{
  ++g_app_system_monitor_report_version;
  g_app_system_monitor_report = s_report;
  ++g_app_system_monitor_report_version;
}

static uint8_t App_SystemMonitor_ConvertMsToCycles(uint32_t core_clock_hz,
                                                   uint32_t duration_ms,
                                                   uint32_t *cycles)
{
  uint32_t cycles_per_ms;
  uint32_t fractional_cycles;
  uint32_t remainder_hz;
  uint32_t result;

  if ((core_clock_hz == 0U) || (duration_ms == 0U) || (cycles == 0))
  {
    return 0U;
  }

  cycles_per_ms = core_clock_hz / 1000U;
  if (cycles_per_ms > (0xFFFFFFFFUL / duration_ms))
  {
    return 0U;
  }
  result = cycles_per_ms * duration_ms;

  remainder_hz = core_clock_hz % 1000U;
  fractional_cycles =
    (remainder_hz * duration_ms + 999U) / 1000U;
  if (result > (0xFFFFFFFFUL - fractional_cycles))
  {
    return 0U;
  }

  *cycles = result + fractional_cycles;
  return 1U;
}

static void App_SystemMonitor_UpdateMaximum(uint32_t value,
                                            uint32_t *maximum)
{
  if (value > *maximum)
  {
    *maximum = value;
  }
}

static void App_SystemMonitor_UpdateTiming(
  const App_SystemTimingSample_t *timing,
  uint32_t now_ms)
{
  ++s_report.loop_count;
  s_report.loop_work_last_cycles = timing->loop_work_cycles;
  s_report.collector_last_cycles = timing->collector_cycles;
  s_report.snapshot_last_cycles = timing->snapshot_cycles;
  s_report.uplink_last_cycles = timing->uplink_cycles;
  App_SystemMonitor_UpdateMaximum(timing->loop_work_cycles,
                                  &s_report.loop_work_max_cycles);
  App_SystemMonitor_UpdateMaximum(timing->collector_cycles,
                                  &s_report.collector_max_cycles);
  App_SystemMonitor_UpdateMaximum(timing->snapshot_cycles,
                                  &s_report.snapshot_max_cycles);
  App_SystemMonitor_UpdateMaximum(timing->uplink_cycles,
                                  &s_report.uplink_max_cycles);

  if ((s_report.monitor_valid == 0U) ||
      (timing->loop_gap_valid == 0U))
  {
    return;
  }

  s_report.measurement_valid = 1U;
  s_report.loop_gap_last_cycles = timing->loop_gap_cycles;
  App_SystemMonitor_UpdateMaximum(timing->loop_gap_cycles,
                                  &s_report.loop_gap_max_cycles);

  if (timing->loop_gap_cycles >= s_report.loop_hard_cycles)
  {
    ++s_report.hard_overrun_count;
    s_report.last_hard_overrun_at_ms = now_ms;
  }
  else if (timing->loop_gap_cycles >= s_report.loop_warning_cycles)
  {
    ++s_report.warning_count;
    s_report.last_warning_at_ms = now_ms;
  }
}

static uint32_t App_SystemMonitor_GetTimingReasons(uint32_t now_ms)
{
  uint32_t reasons;

  reasons = APP_SYSTEM_HEALTH_REASON_NONE;
  if ((s_report.hard_overrun_count != 0U) &&
      ((uint32_t)(now_ms - s_report.last_hard_overrun_at_ms) <
       APP_SYSTEM_MONITOR_TIMING_RECOVERY_MS))
  {
    reasons |= APP_SYSTEM_HEALTH_REASON_LOOP_HARD_OVERRUN;
  }
  if ((s_report.warning_count != 0U) &&
      ((uint32_t)(now_ms - s_report.last_warning_at_ms) <
       APP_SYSTEM_MONITOR_TIMING_RECOVERY_MS))
  {
    reasons |= APP_SYSTEM_HEALTH_REASON_LOOP_WARNING;
  }

  return reasons;
}

static uint32_t App_SystemMonitor_GetServiceReasons(
  const App_SystemHealthInputs_t *health,
  uint32_t now_ms)
{
  uint32_t reasons;

  reasons = APP_SYSTEM_HEALTH_REASON_NONE;
  if (health->collector_fault != 0U)
  {
    reasons |= APP_SYSTEM_HEALTH_REASON_COLLECTOR_FAULT;
  }
  if (health->source_offline != 0U)
  {
    reasons |= APP_SYSTEM_HEALTH_REASON_SOURCE_OFFLINE;
  }
  else if (health->source_stale != 0U)
  {
    reasons |= APP_SYSTEM_HEALTH_REASON_DATA_STALE;
  }

  if (health->has_valid_data != 0U)
  {
    s_report.data_age_ms = now_ms - health->last_success_at_ms;
    App_SystemMonitor_UpdateMaximum(s_report.data_age_ms,
                                    &s_report.data_age_max_ms);
    if (s_report.data_age_ms > APP_SYSTEM_MONITOR_DATA_FRESHNESS_MS)
    {
      reasons |= APP_SYSTEM_HEALTH_REASON_DATA_STALE;
    }
  }
  else
  {
    s_report.data_age_ms = 0U;
    if (s_report.startup_completed != 0U)
    {
      reasons |= APP_SYSTEM_HEALTH_REASON_DATA_STALE;
    }
  }

  if (health->uplink_init_failed != 0U)
  {
    reasons |= APP_SYSTEM_HEALTH_REASON_UPLINK_ERROR;
  }
  if (health->uplink_error_count != s_last_uplink_error_count)
  {
    s_last_uplink_error_count = health->uplink_error_count;
    s_uplink_completed_count_at_error = health->uplink_completed_count;
    s_uplink_error_active = 1U;
  }
  else if ((s_uplink_error_active != 0U) &&
           (health->uplink_completed_count !=
            s_uplink_completed_count_at_error))
  {
    s_uplink_error_active = 0U;
  }
  if (s_uplink_error_active != 0U)
  {
    reasons |= APP_SYSTEM_HEALTH_REASON_UPLINK_ERROR;
  }
  if (health->has_completed_tx != 0U)
  {
    s_report.tx_silence_ms = now_ms - health->last_completed_at_ms;
    if (s_report.tx_silence_ms > APP_SYSTEM_MONITOR_TX_SILENCE_MS)
    {
      reasons |= APP_SYSTEM_HEALTH_REASON_UPLINK_STALLED;
    }
  }
  else
  {
    s_report.tx_silence_ms = 0U;
    if (s_report.startup_completed != 0U)
    {
      reasons |= APP_SYSTEM_HEALTH_REASON_UPLINK_STALLED;
    }
  }

  return reasons;
}

static void App_SystemMonitor_UpdateState(uint32_t active_reasons,
                                          uint32_t now_ms)
{
  App_SystemHealthState_t next_state;

  if ((active_reasons & APP_SYSTEM_MONITOR_CRITICAL_REASONS) != 0U)
  {
    next_state = APP_SYSTEM_HEALTH_UNHEALTHY;
  }
  else if (s_report.startup_completed == 0U)
  {
    next_state = APP_SYSTEM_HEALTH_STARTING;
  }
  else if ((active_reasons & APP_SYSTEM_MONITOR_DEGRADED_REASONS) != 0U)
  {
    next_state = APP_SYSTEM_HEALTH_DEGRADED;
  }
  else
  {
    next_state = APP_SYSTEM_HEALTH_HEALTHY;
  }

  if (next_state != s_report.health_state)
  {
    s_report.health_state = next_state;
    s_report.state_since_ms = now_ms;
    ++s_report.transition_count;
  }
}

App_SystemMonitorResult_t App_SystemMonitor_Init(
  uint32_t core_clock_hz,
  uint32_t measurement_overhead_cycles,
  uint32_t now_ms)
{
  uint8_t warning_valid;
  uint8_t hard_valid;

  (void)memset(&s_report, 0, sizeof(s_report));
  g_app_system_monitor_report_version = 0U;
  s_initialized_at_ms = now_ms;
  s_last_uplink_error_count = 0U;
  s_uplink_completed_count_at_error = 0U;
  s_uplink_error_active = 0U;
  s_initialized = 1U;
  s_report.core_clock_hz = core_clock_hz;
  s_report.measurement_overhead_cycles = measurement_overhead_cycles;
  s_report.health_state = APP_SYSTEM_HEALTH_STARTING;
  s_report.state_since_ms = now_ms;

  warning_valid = App_SystemMonitor_ConvertMsToCycles(
    core_clock_hz,
    APP_SYSTEM_MONITOR_LOOP_WARNING_MS,
    &s_report.loop_warning_cycles);
  hard_valid = App_SystemMonitor_ConvertMsToCycles(
    core_clock_hz,
    APP_SYSTEM_MONITOR_LOOP_HARD_MS,
    &s_report.loop_hard_cycles);
  if ((warning_valid == 0U) || (hard_valid == 0U) ||
      (s_report.loop_warning_cycles >= s_report.loop_hard_cycles))
  {
    s_report.monitor_valid = 0U;
    s_report.health_state = APP_SYSTEM_HEALTH_UNHEALTHY;
    s_report.active_reasons = APP_SYSTEM_HEALTH_REASON_MONITOR_INVALID;
    s_report.observed_reasons = APP_SYSTEM_HEALTH_REASON_MONITOR_INVALID;
    App_SystemMonitor_Publish();
    return APP_SYSTEM_MONITOR_RESULT_INVALID_CONFIGURATION;
  }

  s_report.monitor_valid = 1U;
  App_SystemMonitor_Publish();
  return APP_SYSTEM_MONITOR_RESULT_OK;
}

App_SystemMonitorResult_t App_SystemMonitor_Process(
  const App_SystemTimingSample_t *timing,
  const App_SystemHealthInputs_t *health,
  uint32_t now_ms)
{
  uint32_t active_reasons;

  if (s_initialized == 0U)
  {
    return APP_SYSTEM_MONITOR_RESULT_NOT_INITIALIZED;
  }
  if ((timing == 0) || (health == 0))
  {
    return APP_SYSTEM_MONITOR_RESULT_INVALID_ARGUMENT;
  }

  App_SystemMonitor_UpdateTiming(timing, now_ms);
  if ((s_report.startup_completed == 0U) &&
      (((health->has_valid_data != 0U) &&
        (health->has_completed_tx != 0U)) ||
       ((uint32_t)(now_ms - s_initialized_at_ms) >=
        APP_SYSTEM_MONITOR_STARTUP_GRACE_MS)))
  {
    s_report.startup_completed = 1U;
  }

  active_reasons = App_SystemMonitor_GetTimingReasons(now_ms);
  active_reasons |= App_SystemMonitor_GetServiceReasons(health, now_ms);
  if (s_report.monitor_valid == 0U)
  {
    active_reasons |= APP_SYSTEM_HEALTH_REASON_MONITOR_INVALID;
  }

  s_report.active_reasons = active_reasons;
  s_report.observed_reasons |= active_reasons;
  App_SystemMonitor_UpdateState(active_reasons, now_ms);
  App_SystemMonitor_Publish();
  return APP_SYSTEM_MONITOR_RESULT_OK;
}

App_SystemMonitorResult_t App_SystemMonitor_GetReport(
  App_SystemMonitorReport_t *report)
{
  if (s_initialized == 0U)
  {
    return APP_SYSTEM_MONITOR_RESULT_NOT_INITIALIZED;
  }
  if (report == 0)
  {
    return APP_SYSTEM_MONITOR_RESULT_INVALID_ARGUMENT;
  }

  *report = s_report;
  return APP_SYSTEM_MONITOR_RESULT_OK;
}
