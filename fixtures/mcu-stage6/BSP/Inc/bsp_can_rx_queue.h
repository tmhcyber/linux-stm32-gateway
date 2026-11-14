#ifndef BSP_CAN_RX_QUEUE_H
#define BSP_CAN_RX_QUEUE_H

#include <stdint.h>

/* Four slots, one reserved: at most three pending frames. */
#define BSP_CAN_RX_QUEUE_SLOTS 4U

typedef struct {
  uint32_t id;
  uint8_t is_extended;
  uint8_t is_remote;
  uint8_t dlc;
  uint8_t data[8];
} BSP_CAN_QueueFrame_t;

/* Single CAN RX ISR producer, main-loop consumer; not callable from NMI/Fault.
 * Push returns 0 when full, without overwriting queued frames. It copies the
 * frame as received; protocol validation and drop counting belong to callers.
 */
uint8_t BSP_CAN_QueuePush(BSP_CAN_QueueFrame_t frame);
/* Returns 0 on empty/null, leaving output unchanged; 1 copies one whole frame. */
uint8_t BSP_CAN_QueuePop(BSP_CAN_QueueFrame_t *frame);
/* Software only: does not clear peripheral FIFO, pending IRQ or diagnostics.
 * Recovery must quiesce hardware RX before clearing and resuming reception.
 */
void BSP_CAN_QueueClear(void);

#endif
