#ifndef BSP_I2C_H
#define BSP_I2C_H

#include <stdint.h>

typedef enum
{
  BSP_I2C_STATUS_OK = 0,
  BSP_I2C_STATUS_INVALID_ARGUMENT,
  BSP_I2C_STATUS_NACK,
  BSP_I2C_STATUS_TIMEOUT,
  BSP_I2C_STATUS_BUSY,
  BSP_I2C_STATUS_BUS_ERROR
} BSP_I2C_Status_t;

BSP_I2C_Status_t BSP_I2C1_Init(void);
BSP_I2C_Status_t BSP_I2C1_IsReady(uint8_t address_7bit,
                                  uint32_t timeout_ms);
BSP_I2C_Status_t BSP_I2C1_Write(uint8_t address_7bit,
                                const uint8_t *data,
                                uint16_t length,
                                uint32_t timeout_ms);
BSP_I2C_Status_t BSP_I2C1_Read(uint8_t address_7bit,
                               uint8_t *data,
                               uint16_t length,
                               uint32_t timeout_ms);

#endif
