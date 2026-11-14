#ifndef APP_CAN_SERVICE_H
#define APP_CAN_SERVICE_H
#include <stdint.h>
typedef struct {
  uint32_t valid_requests, invalid_requests, snapshot_errors, encoded_count;
  uint8_t last_quality;
  int16_t last_temperature;
  uint16_t last_humidity;
} App_CAN_Report_t;
extern volatile App_CAN_Report_t g_app_can_report;
/* Main-loop only. Returns response DLC (5), or 0 for no response.
 * Caller supplies eight writable output bytes; no measurement is triggered.
 */
uint8_t App_CAN_HandleRequest(uint32_t id, uint8_t extended,
    uint8_t remote, uint8_t dlc, const uint8_t *data, uint8_t *response);
#endif
