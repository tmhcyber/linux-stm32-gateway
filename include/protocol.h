#ifndef GATEWAY_PROTOCOL_H
#define GATEWAY_PROTOCOL_H
#include <stddef.h>
#include <stdint.h>
#define MAX_BODY 509
typedef enum { Q_NOT_AVAILABLE, Q_VALID, Q_STALE, Q_OFFLINE } Quality;
typedef enum { S_NOT_RUN, S_OK, S_INVALID_ARGUMENT, S_NACK, S_TIMEOUT,
 S_BUSY, S_BUS_ERROR, S_TEMPERATURE_CRC_ERROR, S_HUMIDITY_CRC_ERROR } Status;
typedef enum { P_OK, P_ASCII, P_SYNTAX, P_DUPLICATE, P_UNKNOWN, P_MISSING,
 P_INTEGER, P_RANGE, P_ENUM, P_VERSION, P_IDENTITY, P_HEADER, P_QUALITY,
 P_BATCH, P_FAMILY, P_COUNT } ParseError;
typedef struct { int32_t value; int8_t scale; Quality quality; Status status;
 uint32_t value_ms, status_ms, seq; } Point;
typedef struct { uint32_t version, flags, gap, coal, snap, n;
 int heartbeat; Point points[2]; } Record;
extern const char *const parse_names[P_COUNT];
extern const char *const quality_names[4];
extern const char *const status_names[9];
ParseError protocol_parse(const unsigned char *body, size_t len, Record *out);
#endif
