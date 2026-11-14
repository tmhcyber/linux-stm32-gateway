#ifndef GATEWAY_FRAMER_H
#define GATEWAY_FRAMER_H
#include "protocol.h"
typedef void (*FrameSink)(void *, const unsigned char *, size_t, uint64_t);
typedef struct { unsigned char data[MAX_BODY+2]; size_t len;
 int dropping, prev_cr, startup;
 uint64_t bytes, frames, overlong, sync_discards, resyncs, discarded, abandoned;
} Framer;
void framer_init(Framer *f, int aligned);
void framer_reconnect(Framer *f);
void framer_feed(Framer *f, const unsigned char *data, size_t n,
                 FrameSink sink, void *context);
#endif
