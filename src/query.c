#define _GNU_SOURCE
#include "query.h"
#include <string.h>
#include <stdlib.h>
#include <inttypes.h>
#include <time.h>
#include <errno.h>
int query_utc_valid(const char *s) {
 if(strlen(s)!=20) return 0;
 struct tm t={0}; char *end=strptime(s,"%Y-%m-%dT%H:%M:%SZ",&t);
 if(!end||*end) return 0;
 time_t stamp=timegm(&t);struct tm checked;gmtime_r(&stamp,&checked);
 char canonical[32];strftime(canonical,sizeof(canonical),"%Y-%m-%dT%H:%M:%SZ",&checked);
 return !strcmp(s,canonical);
}
static int number(const char *s, uint64_t max, uint64_t *n) {
 if(!*s||(*s=='0'&&s[1])) return 0;
 for(const char *p=s;*p;++p) if(*p<'0'||*p>'9') return 0;
 char *end;errno=0;unsigned long long value=strtoull(s,&end,10);
 if(errno||*end||value>max) return 0;
 *n=value;return 1;
}
int query_parse(const char *line, Query *q) {
 char from[32],until[32],limit[32],cursor[160],extra;uint64_t n;
 if(sscanf(line,"1 history %31s %31s %31s %159s %c",from,until,limit,cursor,&extra)!=4||
    !query_utc_valid(from)||!query_utc_valid(until)||strcmp(from,until)>0||!number(limit,1000,&n)||!n) return -1;
 memset(q,0,sizeof(*q));q->limit=(unsigned)n;strcpy(q->from,from);strcpy(q->until,until);
 if(!strcmp(cursor,"-")) return 0;
 char *run=strchr(cursor,',');if(!run) return -1;*run++=0;
 char *rx=strchr(run,',');if(!rx) return -1;*rx++=0;
 if(!query_utc_valid(cursor)||strlen(run)!=32||!number(rx,INT64_MAX,&n)||!n) return -1;
 for(char *p=run;*p;++p) if(!((*p>='0'&&*p<='9')||(*p>='a'&&*p<='f'))) return -1;
 strcpy(q->after_utc,cursor);strcpy(q->after_run,run);q->after_rx=(int64_t)n;return 0;
}
int diagnostic_record_json(FILE *f, const StoredRecord *r, const char *run) {
 fprintf(f,"{\"run_id\":\"%s\",\"gateway_run_id\":\"%s\",\"diagnostic_rx_id\":%"PRIu64
 ",\"utc\":\"%s\",\"elapsed_s\":%.6f,\"session\":%"PRIu64
 ",\"raw_start\":%"PRIu64",\"raw_end\":%"PRIu64",\"body\":\"%.*s\",",
 run,r->gateway_run_id,r->rx_id,r->utc,r->elapsed_s,r->session,r->raw_start,r->raw_end,(int)r->body_len,r->body);
 diagnostic_fields_json(f,&r->diagnostic);
 return fprintf(f,"}")<0||ferror(f)?-1:0;
}
int record_json(FILE *f, const StoredRecord *r, const char *run) {
 const Record *v=&r->record;
 fprintf(f,"{\"run_id\":\"%s\",\"rx_id\":%"PRIu64",\"utc\":\"%s\",\"elapsed_s\":%.6f,\"session\":%"PRIu64
 ",\"raw_start\":%"PRIu64",\"raw_end\":%"PRIu64",\"v\":%u,\"reason\":\"%s\",\"flags\":%u,\"gap\":%u,\"coal\":%u,\"snap\":%u,\"n\":2,\"sequence_relation\":\"%s\",\"points\":[",
 run,r->rx_id,r->utc,r->elapsed_s,r->session,r->raw_start,r->raw_end,v->version,v->heartbeat?"HEARTBEAT":"EVENT",v->flags,v->gap,v->coal,v->snap,r->relation);
 for(int i=0;i<2;++i) {
  const Point *p=&v->points[i];
  fprintf(f,"%s{\"src\":\"SHT30\",\"id\":\"%s\",\"value\":%"PRId32",\"scale\":%d,\"unit\":\"%s\",\"quality\":\"%s\",\"status\":\"%s\",\"value_ms\":%u,\"status_ms\":%u,\"seq\":%u,\"value_kind\":\"%s\",\"is_current\":%s}",
   i?",":"",i?"HUMIDITY":"TEMPERATURE",p->value,p->scale,i?"PERCENT_RH":"DEG_C",quality_names[p->quality],status_names[p->status],p->value_ms,p->status_ms,p->seq,
   !p->seq?"not_available":p->quality==Q_VALID?"current":"last_valid",p->quality==Q_VALID?"true":"false");
 }
 return fprintf(f,"]}")<0||ferror(f)?-1:0;
}
