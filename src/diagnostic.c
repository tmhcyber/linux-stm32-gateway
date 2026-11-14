#include "diagnostic.h"
#include <string.h>
#include <inttypes.h>
#include <stddef.h>
typedef struct {const char *name;size_t offset;int hex;} Field;
static const Field fields[]={
#define FIELD(name) {#name,offsetof(Diagnostic,name),0},
 DIAGNOSTIC_FIELDS(FIELD)
#undef FIELD
};
static int hexadecimal(size_t i) {return i==3||i==4||i==8||i==9||i==10||i==11||i==14||i==15||i==18;}
ParseError diagnostic_parse(const unsigned char *body, size_t len, Diagnostic *out) {
 char copy[MAX_BODY+1];uint32_t seen=0;Diagnostic r={0};
 if(len>MAX_BODY) return P_RANGE;
 for(size_t i=0;i<len;++i) if(body[i]>127) return P_ASCII;
 for(size_t i=0;i<len;++i) if(body[i]<32||body[i]==127) return P_SYNTAX;
 if(!len||body[len-1]!=';') return P_SYNTAX;
 memcpy(copy,body,len);copy[len]=0;char *p=copy;
 while(*p) {
  char *end=strchr(p,';');if(!end) return P_SYNTAX;*end=0;
  char *eq=strchr(p,'=');if(!eq||eq==p||!eq[1]||strchr(eq+1,'=')) return P_SYNTAX;
  *eq=0;size_t k=0;for(;k<19;++k) if(!strcmp(p,fields[k].name)) break;
  if(k==19) return P_UNKNOWN;
  if(seen&(1U<<k)) return P_DUPLICATE;
  seen|=1U<<k;const char *s=eq+1;uint32_t value=0;
  if(hexadecimal(k)) {
   if(strlen(s)!=10||s[0]!='0'||s[1]!='x') return P_INTEGER;
   for(size_t i=2;i<10;++i) {
    unsigned n;if(s[i]>='0'&&s[i]<='9') n=(unsigned)(s[i]-'0');
    else if(s[i]>='A'&&s[i]<='F') n=(unsigned)(s[i]-'A'+10);else return P_INTEGER;
    value=value*16+n;
   }
  } else {
   if(!*s||(*s=='0'&&s[1])) return P_INTEGER;
   for(const char *q=s;*q;++q) if(*q<'0'||*q>'9') return P_INTEGER;
   for(;*s;++s) {unsigned n=(unsigned)(*s-'0');if(value>(UINT32_MAX-n)/10) return P_RANGE;value=value*10+n;}
  }
  memcpy((char *)&r+fields[k].offset,&value,sizeof(value));p=end+1;
 }
 if(seen!=((1U<<19)-1)) return P_MISSING;
 if(r.d!=1) return P_VERSION;
 if(r.reset_captured>1||r.frame_valid>1||r.monitor_valid>1||r.watchdog_initialized>1||
    r.fault_status>5||r.fault_type>4||r.health>3||r.watchdog>3) return P_ENUM;
 if((r.reset_reasons&~127U)||(r.frame_reject&~7U)||(r.active&~255U)||
    (r.observed&~255U)||(r.latched&~UINT32_C(0x70090))) return P_HEADER;
 if(r.reset_reasons!=((r.reset_raw>>25)&127U)||(!r.reset_captured&&(r.reset_raw||r.reset_reasons))||
    (r.active&~r.observed)) return P_HEADER;
 if(r.fault_status!=1) {
  if(r.fault_type||r.frame_valid||r.frame_reject||r.pc||r.cfsr||r.hfsr) return P_HEADER;
 } else if(!r.fault_type||(r.frame_valid?r.frame_reject!=0:r.pc!=0)) return P_HEADER;
 if(!r.watchdog_initialized&&(r.watchdog||r.latched)) return P_HEADER;
 *out=r;return P_OK;
}
void diagnostic_fields_json(FILE *f, const Diagnostic *d) {
 for(size_t i=0;i<19;++i) {
  uint32_t value;memcpy(&value,(const char *)d+fields[i].offset,sizeof(value));
  fprintf(f,"%s\"%s\":%"PRIu32,i?",":"",fields[i].name,value);
 }
 fprintf(f,",\"fault_record_valid\":%s,\"pc_valid\":%s",d->fault_status==1?"true":"false",
         d->fault_status==1&&d->frame_valid?"true":"false");
}
