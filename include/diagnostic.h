#ifndef GATEWAY_DIAGNOSTIC_H
#define GATEWAY_DIAGNOSTIC_H
#include "protocol.h"
#include <stdio.h>
/* Value object copied once at reception, then immutable through the queue. */
#define DIAGNOSTIC_FIELDS(X) \
 X(d) X(up_ms) X(reset_captured) X(reset_raw) X(reset_reasons) \
 X(fault_status) X(fault_type) X(frame_valid) X(frame_reject) X(pc) X(cfsr) X(hfsr) \
 X(monitor_valid) X(health) X(active) X(observed) X(watchdog_initialized) X(watchdog) X(latched)
typedef struct {
#define FIELD(name) uint32_t name;
 DIAGNOSTIC_FIELDS(FIELD)
#undef FIELD
} Diagnostic;
ParseError diagnostic_parse(const unsigned char *body, size_t len, Diagnostic *out);
/* No braces; all protocol fields as unsigned JSON numbers. */
void diagnostic_fields_json(FILE *f, const Diagnostic *d);
#endif
