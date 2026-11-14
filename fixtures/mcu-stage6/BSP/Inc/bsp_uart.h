#ifndef BSP_UART_H
#define BSP_UART_H

#include <stdint.h>

typedef enum
{
  BSP_UART_STATUS_OK = 0,
  BSP_UART_STATUS_NOT_INITIALIZED,
  BSP_UART_STATUS_INVALID_ARGUMENT,
  BSP_UART_STATUS_TIMEOUT,
  BSP_UART_STATUS_BUSY,
  BSP_UART_STATUS_ERROR
} BSP_UART_Status_t;

typedef enum
{
  BSP_UART_TX_EVENT_NONE = 0,
  BSP_UART_TX_EVENT_COMPLETE,
  BSP_UART_TX_EVENT_ERROR
} BSP_UART_TxEventType_t;

typedef struct
{
  BSP_UART_TxEventType_t type;
  uint32_t timestamp_ms;
} BSP_UART_TxEvent_t;

BSP_UART_Status_t BSP_UART1_Init(void);
BSP_UART_Status_t BSP_UART1_Write(const uint8_t *data,
                                  uint16_t length,
                                  uint32_t timeout_ms);
BSP_UART_Status_t BSP_UART1_WriteAsync(const uint8_t *data,
                                       uint16_t length);
BSP_UART_TxEvent_t BSP_UART1_TakeTxEvent(void);

#endif
