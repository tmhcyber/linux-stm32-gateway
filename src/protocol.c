#include "protocol.h"
#include <string.h>
#include <limits.h>
const char *const parse_names[P_COUNT]={"OK","NON_ASCII","SYNTAX","DUPLICATE_FIELD",
 "UNKNOWN_FIELD","MISSING_FIELD","INTEGER_FORMAT","VALUE_RANGE","ENUM",
 "VERSION","IDENTITY","HEADER","QUALITY","BATCH","FAMILY"};
const char *const quality_names[4]={"NOT_AVAILABLE","VALID","STALE","OFFLINE"};
const char *const status_names[9]={"NOT_RUN","OK","INVALID_ARGUMENT","NACK","TIMEOUT",
 "BUSY","BUS_ERROR","TEMPERATURE_CRC_ERROR","HUMIDITY_CRC_ERROR"};
static const char *const keys[27]={"v","reason","flags","gap","coal","snap","n",
 "p0.src","p0.id","p0.value","p0.scale","p0.unit","p0.quality","p0.status",
 "p0.value_ms","p0.status_ms","p0.seq","p1.src","p1.id","p1.value","p1.scale",
 "p1.unit","p1.quality","p1.status","p1.value_ms","p1.status_ms","p1.seq"};
static ParseError number(const char *s, int sign, int64_t min, int64_t max, int64_t *v) {
 int neg=0; uint64_t x=0;
 if (*s=='-' && sign) { neg=1; ++s; }
 if (!*s || (*s=='0' && (s[1] || neg))) return P_INTEGER;
 for (const char *p=s; *p; ++p) if (*p<'0'||*p>'9') return P_INTEGER;
 for (; *s; ++s) { unsigned d=(unsigned)(*s-'0');
  if (x>(UINT64_C(2147483648)*2-d)/10) return P_RANGE;
  x=x*10+d;
 }
 *v=neg?-(int64_t)x:(int64_t)x;
 return *v<min||*v>max?P_RANGE:P_OK;
}
static int enum_index(const char *s, const char *const *names, int n) {
 for (int i=0; i<n; ++i) if (!strcmp(s,names[i])) return i;
 return -1;
}
#define NUM(index,sgn,lo,hi,target) do { int64_t v; \
 ParseError e=number(fields[index],sgn,lo,hi,&v); if(e) return e; target=v; } while(0)
ParseError protocol_parse(const unsigned char *body, size_t len, Record *out) {
 char copy[MAX_BODY+1]; char *fields[27]={0}; Record r={0};
 if (len>MAX_BODY) return P_RANGE;
 for (size_t i=0;i<len;++i) if (body[i]>127) return P_ASCII;
 for (size_t i=0;i<len;++i) if (body[i]<32||body[i]==127) return P_SYNTAX;
 if (!len||body[len-1]!=';') return P_SYNTAX;
 memcpy(copy,body,len); copy[len]=0;
 char *p=copy;
 while (*p) {
  char *end=strchr(p,';'); if (!end) return P_SYNTAX; *end=0;
  char *eq=strchr(p,'='); if (!eq||eq==p||!eq[1]||strchr(eq+1,'=')) return P_SYNTAX;
  *eq=0; int k=enum_index(p,keys,27); if(k<0) return P_UNKNOWN;
  if(fields[k]) return P_DUPLICATE;
  fields[k]=eq+1; p=end+1;
 }
 for(int i=0;i<27;++i) if(!fields[i]) return P_MISSING;
 NUM(0,0,0,UINT32_MAX,r.version); if(r.version!=1) return P_VERSION;
 if(!strcmp(fields[1],"EVENT")) r.heartbeat=0;
 else if(!strcmp(fields[1],"HEARTBEAT")) r.heartbeat=1; else return P_ENUM;
 const char *hex=fields[2];
 if(strlen(hex)!=10||hex[0]!='0'||hex[1]!='x') return P_INTEGER;
 for(int i=2;i<10;++i) {
  unsigned d;
  if(hex[i]>='0'&&hex[i]<='9') d=(unsigned)(hex[i]-'0');
  else if(hex[i]>='A'&&hex[i]<='F') d=(unsigned)(hex[i]-'A'+10); else return P_INTEGER;
  r.flags=r.flags*16+d;
 }
 NUM(3,0,0,UINT32_MAX,r.gap); NUM(4,0,0,UINT32_MAX,r.coal);
 NUM(5,0,0,UINT32_MAX,r.snap); NUM(6,0,0,UINT32_MAX,r.n);
 if((r.flags&~UINT32_C(255))||(r.snap&1)||r.n!=2||
    (r.heartbeat?(r.flags||r.gap):!r.flags)) return P_HEADER;
 for(int i=0;i<2;++i) {
  int b=7+10*i; Point *pt=&r.points[i];
  if(strcmp(fields[b],"SHT30")||strcmp(fields[b+1],i?"HUMIDITY":"TEMPERATURE")||
     strcmp(fields[b+4],i?"PERCENT_RH":"DEG_C")) return P_IDENTITY;
  NUM(b+2,1,INT32_MIN,INT32_MAX,pt->value);
  NUM(b+3,1,-128,127,pt->scale); if(pt->scale!=-3) return P_IDENTITY;
  if(pt->value<(i?0:-45000)||pt->value>(i?100000:130000)) return P_RANGE;
  int q=enum_index(fields[b+5],quality_names,4), s=enum_index(fields[b+6],status_names,9);
  if(q<0||s<0) return P_ENUM;
  pt->quality=(Quality)q; pt->status=(Status)s;
  NUM(b+7,0,0,UINT32_MAX,pt->value_ms); NUM(b+8,0,0,UINT32_MAX,pt->status_ms);
  NUM(b+9,0,0,UINT32_MAX,pt->seq);
  if(q==Q_VALID) {
   if(s!=S_OK||!pt->seq||pt->value_ms!=pt->status_ms) return P_QUALITY;
  } else {
   if(s==S_OK||(q!=Q_NOT_AVAILABLE&&s==S_NOT_RUN)) return P_QUALITY;
   if(q==Q_STALE&&!pt->seq) return P_QUALITY;
   if(q==Q_NOT_AVAILABLE&&pt->seq) return P_QUALITY;
   if(!pt->seq&&(pt->value||pt->value_ms)) return P_QUALITY;
   if(s==S_NOT_RUN&&pt->status_ms) return P_QUALITY;
  }
 }
 Point *a=&r.points[0], *b=&r.points[1];
 if(a->quality!=b->quality||a->status!=b->status||a->value_ms!=b->value_ms||
    a->status_ms!=b->status_ms||a->seq!=b->seq) return P_BATCH;
 *out=r; return P_OK;
}
