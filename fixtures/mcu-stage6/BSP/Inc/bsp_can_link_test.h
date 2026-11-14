#ifndef BSP_CAN_LINK_TEST_H
#define BSP_CAN_LINK_TEST_H
#include <stdint.h>

typedef enum {
  BSP_CAN_LINK_NOT_RUN = 0, BSP_CAN_LINK_READY,
  BSP_CAN_LINK_WAIT_TX, BSP_CAN_LINK_FAILED,
  BSP_CAN_LINK_RECOVER_INIT, BSP_CAN_LINK_RECOVER_START
} BSP_CAN_LinkState_t;
typedef enum {
  BSP_CAN_LINK_ERROR_NONE = 0, BSP_CAN_LINK_ERROR_CLOCK,
  BSP_CAN_LINK_ERROR_INIT, BSP_CAN_LINK_ERROR_FILTER,
  BSP_CAN_LINK_ERROR_START, BSP_CAN_LINK_ERROR_SUBMIT,
  BSP_CAN_LINK_ERROR_TX, BSP_CAN_LINK_ERROR_TX_TIMEOUT,
  BSP_CAN_LINK_ERROR_RX
} BSP_CAN_LinkError_t;
typedef struct {
  BSP_CAN_LinkState_t state;
  BSP_CAN_LinkError_t error;
  uint32_t pclk1_hz, submitted_count, tx_ok_count;
  uint32_t rx_count, rx_match_count, invalid_count, busy_count;
  uint32_t last_tsr, last_esr, hal_error;
  uint32_t last_rx_id, last_rx_dlc;
  uint8_t last_rx_data[8];
  uint32_t failure_count, recovery_attempts, recovery_count;
  BSP_CAN_LinkError_t last_failure_error;
  uint32_t last_failure_tsr, last_failure_esr;
  /* ISR-owned cumulative counters; rx_count above counts main-loop dequeues.
   * Overrun counts observed sticky flags, not an exact lost-frame count. */
  uint32_t rx_irq_count, rx_enqueued_count, rx_queue_drop_count;
  uint32_t rx_fifo_overrun_count, rx_read_error_count;
} BSP_CAN_LinkReport_t;
extern volatile BSP_CAN_LinkReport_t g_bsp_can_link_report;
/* Write 1 in Watch to request ONE 0x321 / 01 02 transmission. */
extern volatile uint32_t g_bsp_can_link_send_request;
/* Write 1 after fixing the peer to request ONE CAN2-only recovery.
 * Accepted only in FAILED after CAN clocks were enabled. No old TX replay.
 */
extern volatile uint32_t g_bsp_can_link_recover_request;
/* Bench only: nonzero pauses software dequeue, NOT RX IRQ or sensor sampling.
 * Cleared by failure/recovery. Default zero. */
extern volatile uint32_t g_bsp_can_link_hold_rx_processing;
/* Diagnostic only: switches SYSCLK/HCLK/PCLK to board HSE 25 MHz.
 * Call after HAL_Init, BEFORE all other peripheral/timing/IWDG init.
 * Exclusive with loopback. Recovery does not change system clocks.
 */
void BSP_CAN_LinkTest_Init(void);
void BSP_CAN_LinkTest_Process(uint32_t now_ms);
void BSP_CAN_LinkTest_RX0_IRQHandler(void);
/* Optional application callback injected by Core, invoked from main loop.
 * Returns 0 for no response, otherwise 1..8 bytes to transmit on ID 322.
 * Set only while not processing CAN. Null keeps the fixed-frame baseline.
 */
typedef uint8_t (*BSP_CAN_RequestHandler_t)(uint32_t id, uint8_t extended,
    uint8_t remote, uint8_t dlc, const uint8_t *data, uint8_t *response);
void BSP_CAN_LinkTest_SetRequestHandler(BSP_CAN_RequestHandler_t handler);
#endif
