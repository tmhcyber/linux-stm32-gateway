#ifndef APP_CAN_PROTOCOL_H
#define APP_CAN_PROTOCOL_H
#include <stdint.h>
/* Classic CAN: standard data request 123/1/01; response 322/5.
 * quality: 0 unavailable, 1 valid, 2 stale, 3 offline.
 * Temperatures in 0.01 C; humidity in 0.01 %RH; big endian.
 */
uint8_t CAN_RequestIsValid(uint32_t id, uint8_t is_extended,
    uint8_t is_remote, uint8_t dlc, const uint8_t *data);
/* Non-null data must designate data_capacity writable bytes.
 * Rejected calls leave the output untouched.
 */
uint8_t CAN_EncodeResponse(uint8_t quality, int16_t temperature,
    uint16_t humidity, uint8_t *data, uint32_t data_capacity);
#endif
