#ifndef APP_SYSTEM_MONITOR_H
#define APP_SYSTEM_MONITOR_H

#include <stdint.h>

#define APP_SYSTEM_MONITOR_LOOP_WARNING_MS       5U
#define APP_SYSTEM_MONITOR_LOOP_HARD_MS         25U
#define APP_SYSTEM_MONITOR_DATA_FRESHNESS_MS  1500U
#define APP_SYSTEM_MONITOR_TX_SILENCE_MS       6000U
#define APP_SYSTEM_MONITOR_TIMING_RECOVERY_MS  1000U
#define APP_SYSTEM_MONITOR_STARTUP_GRACE_MS    6000U

#define APP_SYSTEM_HEALTH_REASON_NONE                 0UL
#define APP_SYSTEM_HEALTH_REASON_LOOP_WARNING         (1UL << 0U)
#define APP_SYSTEM_HEALTH_REASON_LOOP_HARD_OVERRUN    (1UL << 1U)
#define APP_SYSTEM_HEALTH_REASON_DATA_STALE           (1UL << 2U)
#define APP_SYSTEM_HEALTH_REASON_SOURCE_OFFLINE       (1UL << 3U)
#define APP_SYSTEM_HEALTH_REASON_COLLECTOR_FAULT      (1UL << 4U)
#define APP_SYSTEM_HEALTH_REASON_UPLINK_STALLED       (1UL << 5U)
#define APP_SYSTEM_HEALTH_REASON_UPLINK_ERROR         (1UL << 6U)
#define APP_SYSTEM_HEALTH_REASON_MONITOR_INVALID      (1UL << 7U)

typedef enum
{
  APP_SYSTEM_MONITOR_RESULT_OK = 0,
  APP_SYSTEM_MONITOR_RESULT_INVALID_ARGUMENT,
  APP_SYSTEM_MONITOR_RESULT_INVALID_CONFIGURATION,
  APP_SYSTEM_MONITOR_RESULT_NOT_INITIALIZED
} App_SystemMonitorResult_t;

typedef enum
{
  APP_SYSTEM_HEALTH_STARTING = 0,
  APP_SYSTEM_HEALTH_HEALTHY,
  APP_SYSTEM_HEALTH_DEGRADED,
  APP_SYSTEM_HEALTH_UNHEALTHY
} App_SystemHealthState_t;

typedef struct
{
  uint8_t loop_gap_valid;
  uint32_t loop_gap_cycles;
  uint32_t loop_work_cycles;
  uint32_t collector_cycles;
  uint32_t snapshot_cycles;
  uint32_t uplink_cycles;
} App_SystemTimingSample_t;

typedef struct
{
  uint8_t collector_fault;
  uint8_t has_valid_data;
  uint8_t source_stale;
  uint8_t source_offline;
  uint32_t last_success_at_ms;
  uint8_t uplink_init_failed;
  uint8_t has_completed_tx;
  uint32_t last_completed_at_ms;
  uint32_t uplink_completed_count;
  uint32_t uplink_error_count;
} App_SystemHealthInputs_t;

typedef struct
{
  uint8_t monitor_valid;
  uint8_t measurement_valid;
  uint8_t startup_completed;
  uint32_t core_clock_hz;
  uint32_t measurement_overhead_cycles;
  uint32_t loop_warning_cycles;
  uint32_t loop_hard_cycles;
  uint32_t loop_count;
  uint32_t loop_gap_last_cycles;
  uint32_t loop_gap_max_cycles;
  uint32_t loop_work_last_cycles;
  uint32_t loop_work_max_cycles;
  uint32_t collector_last_cycles;
  uint32_t collector_max_cycles;
  uint32_t snapshot_last_cycles;
  uint32_t snapshot_max_cycles;
  uint32_t uplink_last_cycles;
  uint32_t uplink_max_cycles;
  uint32_t warning_count;
  uint32_t hard_overrun_count;
  uint32_t last_warning_at_ms;
  uint32_t last_hard_overrun_at_ms;
  uint32_t data_age_ms;
  uint32_t data_age_max_ms;
  uint32_t tx_silence_ms;
  App_SystemHealthState_t health_state;
  uint32_t active_reasons;
  uint32_t observed_reasons;
  uint32_t transition_count;
  uint32_t state_since_ms;
} App_SystemMonitorReport_t;

extern volatile uint32_t g_app_system_monitor_report_version;
extern volatile App_SystemMonitorReport_t g_app_system_monitor_report;

App_SystemMonitorResult_t App_SystemMonitor_Init(
  uint32_t core_clock_hz,
  uint32_t measurement_overhead_cycles,
  uint32_t now_ms);
App_SystemMonitorResult_t App_SystemMonitor_Process(
  const App_SystemTimingSample_t *timing,
  const App_SystemHealthInputs_t *health,
  uint32_t now_ms);
App_SystemMonitorResult_t App_SystemMonitor_GetReport(
  App_SystemMonitorReport_t *report);

#endif
