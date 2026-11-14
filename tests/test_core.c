#include "protocol.h"
#include "framer.h"
#include "diagnostic.h"
#include "diagnostic_vectors.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static unsigned count;
static size_t length;
static void sink(void *ctx, const unsigned char *s, size_t n, uint64_t end) {
 (void)ctx; (void)s; (void)end; ++count; length=n;
}
static void parse_sink(void *ctx, const unsigned char *s, size_t n, uint64_t end) {
 Record r; (void)ctx; (void)end;
 assert(protocol_parse(s,n,&r)==P_OK);
 assert(r.points[0].value==25000&&r.points[1].value==50000);
 ++count;
}
static void diagnostic_sink(void *ctx, const unsigned char *s, size_t n, uint64_t end) {
 Diagnostic d;(void)ctx;(void)end;assert(diagnostic_parse(s,n,&d)==P_OK);
 assert(d.pc==UINT32_MAX&&d.reset_reasons==127&&d.frame_valid==1);++count;
}
int main(void) {
 for(size_t split=0;split<sizeof(diagnostic_test_vector);++split) {
  Framer f;framer_init(&f,1);count=0;
  framer_feed(&f,diagnostic_test_vector,split,diagnostic_sink,NULL);
  framer_feed(&f,diagnostic_test_vector+split,sizeof(diagnostic_test_vector)-1-split,diagnostic_sink,NULL);
  assert(count==1&&!f.overlong&&!f.len);
 }
 unsigned char bytes[520]; memset(bytes,'X',sizeof(bytes));
 /* Every possible split at the exact payload limit, with a split CRLF. */
 bytes[509]='\r'; bytes[510]='\n';
 for(size_t split=0;split<=511;++split) {
  Framer f; framer_init(&f,1); count=0;
  framer_feed(&f,bytes,split,sink,NULL);
  framer_feed(&f,bytes+split,511-split,sink,NULL);
  assert(count==1&&length==509&&!f.overlong&&f.bytes==511&&!f.len);
 }
 static const unsigned char valid[]=
 "v=1;reason=EVENT;flags=0x00000016;gap=0;coal=0;snap=4;n=2;"
 "p0.src=SHT30;p0.id=TEMPERATURE;p0.value=25000;p0.scale=-3;p0.unit=DEG_C;"
 "p0.quality=VALID;p0.status=OK;p0.value_ms=1000;p0.status_ms=1000;p0.seq=1;"
 "p1.src=SHT30;p1.id=HUMIDITY;p1.value=50000;p1.scale=-3;p1.unit=PERCENT_RH;"
 "p1.quality=VALID;p1.status=OK;p1.value_ms=1000;p1.status_ms=1000;p1.seq=1;\r\n";
 for(size_t split=0;split<sizeof(valid);++split) {
  Framer frame; framer_init(&frame,1); count=0;
  framer_feed(&frame,valid,split,parse_sink,NULL);
  framer_feed(&frame,valid+split,sizeof(valid)-1-split,parse_sink,NULL);
  assert(count==1);
 }
 Framer f; framer_init(&f,0); count=0;
 framer_feed(&f,(const unsigned char *)"fragment\r",9,sink,NULL);
 framer_feed(&f,(const unsigned char *)"\nA\r\nB\r\n",7,sink,NULL);
 assert(count==2&&f.sync_discards==1&&f.discarded==10);
 framer_init(&f,1); count=0;
 memset(bytes,'X',510); bytes[510]='\r'; bytes[511]='\n';
 framer_feed(&f,bytes,512,sink,NULL);
 framer_feed(&f,(const unsigned char *)"OK\r\n",4,sink,NULL);
 assert(count==1&&length==2&&f.overlong==1&&f.resyncs==1&&f.discarded==512);
 framer_init(&f,1); memset(bytes,'X',sizeof(bytes));
 for(int i=0;i<10000;++i) framer_feed(&f,bytes,sizeof(bytes),sink,NULL);
 assert(f.overlong==1&&f.len==0&&f.discarded==5200000);
 framer_feed(&f,(const unsigned char *)"\r\n",2,sink,NULL); assert(f.resyncs==1);
 /* Exhaust all one-byte values, including NUL and high bytes. */
 for(unsigned i=0;i<256;++i) {
  Record r; bytes[0]=(unsigned char)i;
  assert(protocol_parse(bytes,1,&r)!=P_OK);
 }
 /* Deterministic noisy input, arbitrary chunking and parser calls under sanitizers. */
 uint32_t random=1;
 for(unsigned trial=0;trial<20000;++trial) {
  size_t n=trial%510;
  for(size_t j=0;j<n;++j) {
   random=random*UINT32_C(1664525)+UINT32_C(1013904223);
   bytes[j]=(unsigned char)(random>>24);
  }
  Record r; Diagnostic d,before;memset(&d,0xA5,sizeof(d));before=d;
  (void)protocol_parse(bytes,n,&r);
  if(diagnostic_parse(bytes,n,&d)!=P_OK) assert(!memcmp(&d,&before,sizeof(d)));
  framer_feed(&f,bytes,n,sink,NULL);
 }
 puts("core boundary tests passed"); return 0;
}
