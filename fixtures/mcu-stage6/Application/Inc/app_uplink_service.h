#ifndef APP_UPLINK_SERVICE_H
#define APP_UPLINK_SERVICE_H

#include <stdint.h>

#include "app_snapshot_text_formatter.h"
#include "app_diagnostic.h"

#define APP_UPLINK_HEARTBEAT_PERIOD_MS  5000U
#define APP_UPLINK_RETRY_PERIOD_MS       100U

typedef enum
{
  APP_UPLINK_SERVICE_RESULT_NOT_RUN = 0,
  APP_UPLINK_SERVICE_RESULT_OK,
  APP_UPLINK_SERVICE_RESULT_NOT_INITIALIZED,
  APP_UPLINK_SERVICE_RESULT_INVALID_ARGUMENT,
  APP_UPLINK_SERVICE_RESULT_TRANSPORT_ERROR
} App_UplinkServiceResult_t;

typedef enum
{
  APP_UPLINK_SERVICE_STATE_NOT_INITIALIZED = 0,
  APP_UPLINK_SERVICE_STATE_IDLE,
  APP_UPLINK_SERVICE_STATE_SENDING
} App_UplinkServiceState_t;

typedef struct
{
  App_UplinkServiceState_t state;
  App_UplinkTextResult_t last_format_result;
  App_UplinkReason_t active_reason;
  App_UplinkReason_t pending_reason;
  uint8_t has_latest_snapshot;
  uint8_t pending_valid;
  uint8_t has_successful_transmission;
  uint32_t last_completed_at_ms;
  uint32_t tx_start_count;
  uint32_t completed_count;
  uint32_t event_completed_count;
  uint32_t heartbeat_completed_count;
  uint32_t format_error_count;
  uint32_t coalesced_count;
  uint32_t tx_busy_count;
  uint32_t tx_error_count;
  uint32_t diagnostic_started, diagnostic_completed, diagnostic_busy;
  uint32_t diagnostic_errors, diagnostic_format_errors, diagnostic_replaced;
} App_UplinkServiceReport_t;

extern volatile App_UplinkServiceReport_t g_app_uplink_service_report;

App_UplinkServiceResult_t App_UplinkService_Init(void);
App_UplinkServiceResult_t App_UplinkService_SubmitEvent(
  const App_SnapshotEvent_t *event);
void App_UplinkService_Process(uint32_t now_ms);
#if APP_DIAGNOSTICS_ENABLED
int App_UplinkService_DiagnosticDue(uint32_t now_ms);
void App_UplinkService_SubmitDiagnostic(const App_Diagnostic_t *record);
#endif

#endif
