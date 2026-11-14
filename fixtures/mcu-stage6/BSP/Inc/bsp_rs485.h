#ifndef BSP_RS485_H
#define BSP_RS485_H

#include <stdint.h>

#define BSP_RS485_RX_QUEUE_CAPACITY  64U

typedef enum
{
  BSP_RS485_STATUS_OK = 0,
  BSP_RS485_STATUS_NOT_INITIALIZED,
  BSP_RS485_STATUS_INVALID_ARGUMENT,
  BSP_RS485_STATUS_BUSY,
  BSP_RS485_STATUS_TIMEOUT,
  BSP_RS485_STATUS_ERROR
} BSP_RS485_Status_t;

typedef struct
{
  uint8_t byte;
  uint32_t timestamp_ms;
} BSP_RS485_RxByteEvent_t;

typedef enum
{
  BSP_RS485_TX_EVENT_NONE = 0,
  BSP_RS485_TX_EVENT_COMPLETE,
  BSP_RS485_TX_EVENT_ERROR
} BSP_RS485_TxEventType_t;

typedef struct
{
  BSP_RS485_TxEventType_t type;
  uint32_t timestamp_ms;
  uint32_t uart_error_flags;
} BSP_RS485_TxEvent_t;

typedef struct
{
  uint32_t overflow_count;
  uint32_t last_overflow_at_ms;
  uint32_t uart_error_count;
  uint32_t last_uart_error_at_ms;
  uint32_t last_uart_error_flags;
} BSP_RS485_RxFaultEvent_t;

BSP_RS485_Status_t BSP_RS485_Init(void);
BSP_RS485_Status_t BSP_RS485_WriteAsync(const uint8_t *data,
                                        uint16_t length);
BSP_RS485_Status_t BSP_RS485_AbortTransmit(void);
uint8_t BSP_RS485_TakeRxByte(BSP_RS485_RxByteEvent_t *event);
BSP_RS485_TxEvent_t BSP_RS485_TakeTxEvent(void);
BSP_RS485_RxFaultEvent_t BSP_RS485_TakeRxFaultEvent(void);
uint8_t BSP_RS485_FlushRx(void);

/* Shared HAL callback dispatch hooks. Application code must not call these. */
void BSP_RS485_HandleRxCompleteIRQ(uint32_t timestamp_ms);
void BSP_RS485_HandleTxCompleteIRQ(uint32_t timestamp_ms);
void BSP_RS485_HandleUartErrorIRQ(uint32_t timestamp_ms,
                                 uint32_t uart_error_flags);

#endif
