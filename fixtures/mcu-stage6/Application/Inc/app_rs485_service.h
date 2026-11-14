#ifndef APP_RS485_SERVICE_H
#define APP_RS485_SERVICE_H

#include <stdint.h>

#define APP_RS485_MAX_FRAME_BYTES          32U
#define APP_RS485_FRAME_STORAGE_BYTES      33U
#define APP_RS485_INTERBYTE_TIMEOUT_MS    100U
#define APP_RS485_TX_COMPLETE_TIMEOUT_MS   10U

typedef enum
{
  APP_RS485_SERVICE_RESULT_NOT_RUN = 0,
  APP_RS485_SERVICE_RESULT_OK,
  APP_RS485_SERVICE_RESULT_TRANSPORT_ERROR
} App_RS485ServiceResult_t;

typedef enum
{
  APP_RS485_STATE_NOT_INITIALIZED = 0,
  APP_RS485_STATE_RX_WAIT,
  APP_RS485_STATE_RX_DISCARD,
  APP_RS485_STATE_RESPONSE_PENDING,
  APP_RS485_STATE_TX_WAIT_COMPLETE
} App_RS485State_t;

typedef struct
{
  App_RS485State_t state;
  uint8_t current_frame_length;
  uint8_t has_last_rx_byte;
  uint32_t last_rx_byte_at_ms;
  uint32_t tx_started_at_ms;
  uint32_t last_tx_completed_at_ms;
  uint32_t rx_byte_count;
  uint32_t complete_frame_count;
  uint32_t valid_ping_count;
  uint32_t semantic_invalid_count;
  uint32_t overlength_count;
  uint32_t interbyte_timeout_count;
  uint32_t rx_queue_overflow_count;
  uint32_t uart_error_count;
  uint32_t last_uart_error_flags;
  uint32_t tx_start_count;
  uint32_t tx_start_failure_count;
  uint32_t tx_success_count;
  uint32_t tx_error_count;
  uint32_t tx_timeout_count;
  uint32_t tx_late_complete_count;
  uint32_t unexpected_tx_event_count;
} App_RS485Report_t;

extern volatile App_RS485Report_t g_app_rs485_report;

App_RS485ServiceResult_t App_RS485Service_Init(void);
void App_RS485Service_Process(uint32_t now_ms);

#endif
