#ifndef BSP_CAN_LOOPBACK_H
#define BSP_CAN_LOOPBACK_H

#include <stdint.h>

typedef enum
{
  BSP_CAN_LOOPBACK_NOT_RUN = 0,
  BSP_CAN_LOOPBACK_SEND,
  BSP_CAN_LOOPBACK_WAIT_TX,
  BSP_CAN_LOOPBACK_WAIT_RX,
  BSP_CAN_LOOPBACK_PASSED,
  BSP_CAN_LOOPBACK_FAILED
} BSP_CAN_LoopbackState_t;

typedef enum
{
  BSP_CAN_LOOPBACK_ERROR_NONE = 0,
  BSP_CAN_LOOPBACK_ERROR_CLOCK,
  BSP_CAN_LOOPBACK_ERROR_INIT,
  BSP_CAN_LOOPBACK_ERROR_FILTER,
  BSP_CAN_LOOPBACK_ERROR_START,
  BSP_CAN_LOOPBACK_ERROR_SUBMIT,
  BSP_CAN_LOOPBACK_ERROR_TX,
  BSP_CAN_LOOPBACK_ERROR_TX_TIMEOUT,
  BSP_CAN_LOOPBACK_ERROR_RX_TIMEOUT,
  BSP_CAN_LOOPBACK_ERROR_RX,
  BSP_CAN_LOOPBACK_ERROR_UNEXPECTED_RX
} BSP_CAN_LoopbackError_t;

typedef struct
{
  BSP_CAN_LoopbackState_t state;
  BSP_CAN_LoopbackError_t error;
  uint32_t pclk1_hz;
  uint32_t step; /* 1: accept, 2: reject, 3: accept again. */
  uint32_t submitted_count;
  uint32_t tx_ok_count;
  uint32_t rx_match_count;
  uint32_t rejected_count;
  uint32_t last_tsr;
  uint32_t last_esr;
  uint32_t hal_error;
  uint32_t last_rx_id;
  uint32_t last_rx_ide;
  uint32_t last_rx_rtr;
  uint32_t last_rx_dlc;
  uint8_t last_rx_data[8];
} BSP_CAN_LoopbackReport_t;

/* Debug view: pause the CPU before interpreting several fields together. */
extern volatile BSP_CAN_LoopbackReport_t g_bsp_can_loopback_report;

/* Standalone CAN2 diagnostic, called once after HAL_Init and before IWDG init.
 * Owns CAN2, PB12 (RX AF9/pull-up) and filter bank 14. No TX GPIO or IRQ.
 * Reset the MCU to rerun; do not combine with another CAN driver.
 */
void BSP_CAN_Loopback_Init(void);
/* Bounded polling: call from the main loop with a fresh HAL_GetTick value. */
void BSP_CAN_Loopback_Process(uint32_t now_ms);

#endif
