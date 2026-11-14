#ifndef APP_UART_BRINGUP_H
#define APP_UART_BRINGUP_H

#include <stdint.h>

typedef enum
{
  APP_UART_BRINGUP_STATUS_NOT_RUN = 0,
  APP_UART_BRINGUP_STATUS_OK,
  APP_UART_BRINGUP_STATUS_NOT_INITIALIZED,
  APP_UART_BRINGUP_STATUS_INVALID_ARGUMENT,
  APP_UART_BRINGUP_STATUS_TIMEOUT,
  APP_UART_BRINGUP_STATUS_BUSY,
  APP_UART_BRINGUP_STATUS_ERROR
} App_UART_BringupStatus_t;

typedef struct
{
  App_UART_BringupStatus_t init_status;
  App_UART_BringupStatus_t last_send_status;
  uint32_t last_attempt_at_ms;
  uint32_t successful_send_count;
  uint32_t failed_send_count;
} App_UART_BringupReport_t;

extern volatile App_UART_BringupReport_t g_app_uart_bringup_report;

void App_UART_Bringup_Init(void);
void App_UART_Bringup_Process(uint32_t now_ms);

#endif
