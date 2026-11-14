#include "framer.h"
#include <string.h>
void framer_init(Framer *f, int aligned) {
 memset(f, 0, sizeof(*f)); f->dropping = !aligned; f->startup = !aligned;
}
void framer_reconnect(Framer *f) {
 f->abandoned += f->len; f->len = 0;
 f->dropping = 1; f->startup = 1; f->prev_cr = 0;
}
void framer_feed(Framer *f, const unsigned char *data, size_t n,
                 FrameSink sink, void *context) {
 for (size_t i=0; i<n; ++i) {
  unsigned char c=data[i]; ++f->bytes;
  if (f->dropping) {
   ++f->discarded;
   if (f->prev_cr && c=='\n') {
    f->dropping=0; f->prev_cr=0;
    if (f->startup) { ++f->sync_discards; f->startup=0; }
    else ++f->resyncs;
   } else f->prev_cr=(c=='\r');
   continue;
  }
  f->data[f->len++]=c;
  if (f->len>=2 && f->data[f->len-2]=='\r' && c=='\n') {
   ++f->frames; sink(context,f->data,f->len-2,f->bytes); f->len=0;
  } else if (f->len>MAX_BODY+(size_t)(c=='\r')) {
   ++f->overlong; f->discarded+=f->len; f->len=0;
   f->dropping=1; f->startup=0; f->prev_cr=(c=='\r');
  }
 }
}
