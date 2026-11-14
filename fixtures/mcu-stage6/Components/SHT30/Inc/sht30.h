#ifndef SHT30_H
#define SHT30_H

#include <stdint.h>

#define SHT30_ADDRESS_DEFAULT_7BIT               0x44U
#define SHT30_ADDRESS_ALTERNATE_7BIT             0x45U
#define SHT30_HIGH_REPEATABILITY_WAIT_MS         15U
#define SHT30_MEASUREMENT_RESPONSE_LENGTH        6U

typedef enum
{
  SHT30_BUS_STATUS_OK = 0,
  SHT30_BUS_STATUS_INVALID_ARGUMENT,
  SHT30_BUS_STATUS_NACK,
  SHT30_BUS_STATUS_TIMEOUT,
  SHT30_BUS_STATUS_BUSY,
  SHT30_BUS_STATUS_ERROR
} SHT30_BusStatus_t;

typedef SHT30_BusStatus_t (*SHT30_BusWriteFn)(
  void *context,
  uint8_t address_7bit,
  const uint8_t *data,
  uint16_t length,
  uint32_t timeout_ms);

typedef SHT30_BusStatus_t (*SHT30_BusReadFn)(
  void *context,
  uint8_t address_7bit,
  uint8_t *data,
  uint16_t length,
  uint32_t timeout_ms);

typedef struct
{
  SHT30_BusWriteFn write;
  SHT30_BusReadFn read;
  void *context;
  uint32_t timeout_ms;
} SHT30_Bus_t;

typedef enum
{
  SHT30_STATUS_OK = 0,
  SHT30_STATUS_INVALID_ARGUMENT,
  SHT30_STATUS_BUS_NACK,
  SHT30_STATUS_BUS_TIMEOUT,
  SHT30_STATUS_BUS_BUSY,
  SHT30_STATUS_BUS_ERROR,
  SHT30_STATUS_TEMPERATURE_CRC_ERROR,
  SHT30_STATUS_HUMIDITY_CRC_ERROR
} SHT30_Status_t;

typedef struct
{
  uint16_t temperature_raw;
  uint16_t humidity_raw;
  int32_t temperature_milli_c;
  uint32_t humidity_milli_percent;
} SHT30_Measurement_t;

typedef struct
{
  SHT30_Bus_t bus;
  uint8_t address_7bit;
} SHT30_t;

SHT30_Status_t SHT30_Init(SHT30_t *device,
                          const SHT30_Bus_t *bus,
                          uint8_t address_7bit);
SHT30_Status_t SHT30_StartHighRepeatabilityMeasurement(SHT30_t *device);
SHT30_Status_t SHT30_ReadMeasurement(SHT30_t *device,
                                     SHT30_Measurement_t *measurement);
SHT30_Status_t SHT30_DecodeMeasurement(
  const uint8_t response[SHT30_MEASUREMENT_RESPONSE_LENGTH],
  SHT30_Measurement_t *measurement);

#endif
