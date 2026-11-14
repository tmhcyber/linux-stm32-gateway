#define _POSIX_C_SOURCE 200809L
#include "output.h"
#include "query.h"
#include <sys/stat.h>
#include <time.h>
#include <string.h>
#include <inttypes.h>
#include <errno.h>
#include <sys/random.h>
double monotonic_now(void) {
 struct timespec t; if(clock_gettime(CLOCK_MONOTONIC,&t)<0) return 0;
 return (double)t.tv_sec+(double)t.tv_nsec/1e9;
}
void utc_now(char *buffer, size_t size) {
 struct timespec t; struct tm tm;
 clock_gettime(CLOCK_REALTIME,&t); gmtime_r(&t.tv_sec,&tm);
 strftime(buffer,size,"%Y-%m-%dT%H:%M:%SZ",&tm);
}
static FILE *new_file(const char *dir, const char *name, const char *mode) {
 char path[4096];
 if(snprintf(path,sizeof(path),"%s/%s",dir,name)>=(int)sizeof(path)) {errno=ENAMETOOLONG; return NULL;}
 return fopen(path,mode);
}
int output_open(Output *o, const char *dir, int aligned) {
 memset(o,0,sizeof(*o)); o->start=o->last_valid=monotonic_now();
 unsigned char identity[16];size_t received=0;
 while(received<sizeof(identity)) {
  ssize_t n=getrandom(identity+received,sizeof(identity)-received,0);
  if(n<0&&errno==EINTR) continue;
  if(n<=0) return -1;
  received+=(size_t)n;
 }
 for(size_t i=0;i<sizeof(identity);++i) snprintf(o->gateway_run_id+2*i,3,"%02x",identity[i]);
 o->directory=dir;
 utc_now(o->started_utc,sizeof(o->started_utc)); framer_init(&o->framer,aligned);
 if(mkdir(dir,0755)<0) return -1;
 o->raw=new_file(dir,"raw.bin","wbx"); o->records=new_file(dir,"records.jsonl","wx");
 o->events=new_file(dir,"events.jsonl","wx");
 o->diagnostics=new_file(dir,"diagnostics.jsonl","wx");
 if(!o->raw||!o->records||!o->events||!o->diagnostics) {
  int e=errno; if(o->raw) fclose(o->raw); if(o->records) fclose(o->records);
  if(o->events) fclose(o->events);
  if(o->diagnostics) fclose(o->diagnostics);
  errno=e; return -1;
 }
 setvbuf(o->diagnostics,NULL,_IOLBF,0);
 setvbuf(o->records,NULL,_IOLBF,0); setvbuf(o->events,NULL,_IOLBF,0);
 return 0;
}
void output_event(Output *o, const char *kind, int detail) {
 if(o->journal&&(!strcmp(kind,"START")||!strcmp(kind,"EXIT")||!strcmp(kind,"SILENT")||
    !strcmp(kind,"TELEMETRY_RESUMED")||!strcmp(kind,"MQTT_FAILURE")||!strcmp(kind,"MQTT_CONNECTED")))
  fprintf(stderr,"level=%s event=%s detail=%d gateway_run_id=%s\n",!strcmp(kind,"SILENT")||!strcmp(kind,"MQTT_FAILURE")?"warning":"info",kind,detail,o->gateway_run_id);
 char utc[40]; utc_now(utc,sizeof(utc));
 double elapsed=monotonic_now()-o->start;++o->event_id;
 if(fprintf(o->events,"{\"utc\":\"%s\",\"elapsed_s\":%.6f,\"kind\":\"%s\",\"detail\":%d,\"raw_offset\":%"PRIu64"}\n",
 utc,elapsed,kind,detail,o->framer.bytes)<0) o->io_error=1;
 if(o->storage) {
  StoredRecord copy={.is_event=1,.event_id=(int64_t)o->event_id,.detail=detail,
   .elapsed_s=elapsed,.raw_start=o->framer.bytes,.session=o->opens};
  snprintf(copy.utc,sizeof(copy.utc),"%s",utc);snprintf(copy.kind,sizeof(copy.kind),"%s",kind);
  storage_submit(o->storage,&copy);
 }
}
void output_storage_status(Output *o, int force) {
 if(!o->db_enabled) return;
 double now=monotonic_now();if(!force&&now-o->last_storage_status<.2) return;
 o->last_storage_status=now;
 int previous=o->storage_stats.degraded;
 if(o->storage) storage_snapshot(o->storage,&o->storage_stats);
 if(o->journal&&previous!=o->storage_stats.degraded)
  fprintf(stderr,"level=%s event=STORAGE_%s sqlite_error=%d gateway_run_id=%s\n",
   o->storage_stats.degraded?"error":"info",o->storage_stats.degraded?"DEGRADED":"RECOVERED",o->storage_stats.error,o->gateway_run_id);
 FILE *f=new_file(o->directory,"storage-status.tmp","w");
 if(!f) {o->io_error=1;return;}
 storage_write_stats(f,&o->storage_stats);fprintf(f,"\n");
 int bad=ferror(f);if(fclose(f)) bad=1;
 char tmp[4096],path[4096];
 if(snprintf(tmp,sizeof(tmp),"%s/storage-status.tmp",o->directory)>=(int)sizeof(tmp)||
    snprintf(path,sizeof(path),"%s/storage-status.json",o->directory)>=(int)sizeof(path)) bad=1;
 if(!bad&&rename(tmp,path)) bad=1;
 if(bad) o->io_error=1;
}
void output_mqtt_status(Output *o, int force) {
 if(!o->mqtt_enabled) return;
 double now=monotonic_now();if(!force&&now-o->last_mqtt_status<.2) return;
 o->last_mqtt_status=now;
 if(o->mqtt) {
  mqtt_snapshot(o->mqtt,&o->mqtt_stats);
  if(o->mqtt_stats.failures!=o->mqtt_observed_failures) {
   output_event(o,"MQTT_FAILURE",(int)(o->mqtt_stats.failures-o->mqtt_observed_failures));
   o->mqtt_observed_failures=o->mqtt_stats.failures;
  }
  int reconnected=o->mqtt_stats.connacks!=o->mqtt_observed_connacks;
  if(reconnected) {
   output_event(o,"MQTT_CONNECTED",0);o->mqtt_observed_connacks=o->mqtt_stats.connacks;
  }
  mqtt_state(o->mqtt,o->port_open,o->silent,now-o->last_valid,o->valid,
             o->db_enabled,&o->storage_stats,force||reconnected,
             &o->latest_diagnostic,o->diagnostic_received,now-o->last_diagnostic);
 }
 FILE *f=new_file(o->directory,"mqtt-status.tmp","w");
 if(!f) {o->io_error=1;return;}
 mqtt_write_stats(f,&o->mqtt_stats);fprintf(f,"\n");
 int bad=ferror(f);if(fclose(f)) bad=1;
 char tmp[4096],path[4096];
 if(snprintf(tmp,sizeof(tmp),"%s/mqtt-status.tmp",o->directory)>=(int)sizeof(tmp)||
    snprintf(path,sizeof(path),"%s/mqtt-status.json",o->directory)>=(int)sizeof(path)) bad=1;
 if(!bad&&rename(tmp,path)) bad=1;
 if(bad) o->io_error=1;
}
static const char *sequence(Output *o, const Point *p) {
 const char *result="first";
 if(o->have_seq) {
  uint32_t prev=o->previous_seq, next=prev==UINT32_MAX?1:prev+1;
  if(p->seq==prev) {++o->seq_repeat; result="repeat";}
  else if(p->seq==next) {
   if(prev==UINT32_MAX) {++o->seq_wrap; result="wrap";}
   else {++o->seq_next; result="next";}
  } else if(p->seq>prev) {++o->seq_jump; result="jump";}
  else {++o->seq_backward; result="backward";}
  if(p->seq!=prev&&p->value_ms<o->previous_ms&&
     (uint32_t)(p->value_ms-o->previous_ms)<UINT32_C(0x80000000)) ++o->ms_wrap;
 }
 o->have_seq=1; o->previous_seq=p->seq; o->previous_ms=p->value_ms;
 return result;
}
static void record_sink(void *context, const unsigned char *body, size_t len, uint64_t end) {
 Output *o=context; Record r; Diagnostic diagnostic;ParseError e;
 int family=0;
 for(size_t i=0;i+1<len;++i) if(!i||body[i-1]==';') {
  if(body[i]=='v'&&body[i+1]=='=') family|=1;
  if(body[i]=='d'&&body[i+1]=='=') family|=2;
 }
 if(family==3) e=P_FAMILY;
 else if(family==2) e=diagnostic_parse(body,len,&diagnostic);
 else {e=protocol_parse(body,len,&r);if(!family&&e==P_UNKNOWN) e=P_FAMILY;}
 if(!e&&family==2) {
  o->last_diagnostic=monotonic_now();++o->diagnostic_received;
  StoredRecord copy={.diagnostic=diagnostic,.is_diagnostic=1,.rx_id=o->diagnostic_received,
   .session=o->opens,.raw_start=end-len-2,.raw_end=end,.body_len=len,
   .elapsed_s=o->last_diagnostic-o->start};
  utc_now(copy.utc,sizeof(copy.utc));memcpy(copy.body,body,len);
  snprintf(copy.gateway_run_id,sizeof(copy.gateway_run_id),"%s",o->gateway_run_id);
  o->latest_diagnostic=copy;
  if(o->storage) storage_submit(o->storage,&copy);
  if(diagnostic_record_json(o->diagnostics,&copy,o->db_enabled?o->storage_stats.run_id:o->gateway_run_id)||fputc('\n',o->diagnostics)==EOF) o->io_error=1;
  return;
 }
 if(e) {++o->errors[e]; output_event(o,parse_names[e],0); return;}
 ++o->valid; o->last_valid=monotonic_now();
 if(o->silent) {o->silent=0; output_event(o,"TELEMETRY_RESUMED",0);}
 const char *seq=sequence(o,&r.points[0]); char utc[40]; utc_now(utc,sizeof(utc));
 {
  StoredRecord copy={.record=r,.rx_id=o->valid,.session=o->opens,
   .raw_start=end-len-2,.raw_end=end,.body_len=len,
   .elapsed_s=o->last_valid-o->start};
  memcpy(copy.body,body,len); snprintf(copy.utc,sizeof(copy.utc),"%s",utc);
  snprintf(copy.relation,sizeof(copy.relation),"%s",seq);
  o->latest=copy;
  if(o->storage) storage_submit(o->storage,&copy);
  if(o->mqtt) mqtt_record(o->mqtt,&copy);
 }
 FILE *f=o->records;
 fprintf(f,"{\"rx_id\":%"PRIu64",\"utc\":\"%s\",\"elapsed_s\":%.6f,\"raw_start\":%"PRIu64",\"raw_end\":%"PRIu64
 ",\"v\":1,\"reason\":\"%s\",\"flags\":%"PRIu32",\"gap\":%"PRIu32",\"coal\":%"PRIu32",\"snap\":%"PRIu32
 ",\"n\":2,\"sequence_relation\":\"%s\",\"points\":[",
 o->valid,utc,o->last_valid-o->start,end-len-2,end,r.heartbeat?"HEARTBEAT":"EVENT",r.flags,r.gap,r.coal,r.snap,seq);
 for(int i=0;i<2;++i) {
  Point *p=&r.points[i]; const char *kind=!p->seq?"not_available":p->quality==Q_VALID?"current":"last_valid";
  fprintf(f,"%s{\"src\":\"SHT30\",\"id\":\"%s\",\"value\":%"PRId32",\"scale\":-3,\"unit\":\"%s\","
  "\"quality\":\"%s\",\"status\":\"%s\",\"value_ms\":%"PRIu32",\"status_ms\":%"PRIu32",\"seq\":%"PRIu32
  ",\"value_kind\":\"%s\",\"is_current\":%s}",i?",":"",i?"HUMIDITY":"TEMPERATURE",p->value,
  i?"PERCENT_RH":"DEG_C",quality_names[p->quality],status_names[p->status],p->value_ms,p->status_ms,p->seq,kind,
  p->quality==Q_VALID?"true":"false");
 }
 fprintf(f,"]}\n"); if(ferror(f)) o->io_error=1;
}
void output_bytes(Output *o, const unsigned char *data, size_t size) {
 if(fwrite(data,1,size,o->raw)!=size||fflush(o->raw)) {o->io_error=1; return;}
 uint64_t before=o->framer.overlong;
 framer_feed(&o->framer,data,size,record_sink,o);
 if(o->framer.overlong!=before) output_event(o,"OVERLONG",(int)(o->framer.overlong-before));
 output_storage_status(o,0);
 output_mqtt_status(o,0);
}
void output_silence(Output *o, double now) {
 if(!o->silent&&now-o->last_valid>=15) {
  o->silent=1; ++o->silent_count; output_event(o,"SILENT",0);
 }
}
int output_finish(Output *o, const char *dir, int code, int sig, int complete,
                  double duration, const char *mode, int discard) {
 output_mqtt_status(o,1);
 if(o->mqtt) {
  mqtt_finish(o->mqtt,&o->mqtt_stats);o->mqtt=NULL;
  if(!sig&&mqtt_has_errors(&o->mqtt_stats)) code=1;
  output_event(o,"MQTT_STOPPED",o->mqtt_stats.offline_acked);
 }
 output_mqtt_status(o,1);
 output_event(o,"EXIT",code);
 if(o->storage) {
  storage_finish(o->storage,code,sig,complete,&o->storage_stats); o->storage=NULL;
  if(!sig&&storage_has_errors(&o->storage_stats)) code=1;
 }
 output_storage_status(o,1);
 if(fclose(o->raw)) o->io_error=1;
 if(ferror(o->records)||ferror(o->events)) o->io_error=1;
 if(fclose(o->records)) o->io_error=1;
 if(fclose(o->events)) o->io_error=1;
 if(ferror(o->diagnostics)) o->io_error=1;
 if(fclose(o->diagnostics)) o->io_error=1;
 if(o->io_error&&!sig) code=1;
 FILE *f=new_file(dir,"summary.json","wx"); if(!f) return 1;
 char utc[40]; utc_now(utc,sizeof(utc)); Framer *s=&o->framer;
 fprintf(f,"{\"gateway_run_id\":\"%s\",\"diagnostic_records\":%"PRIu64",",o->gateway_run_id,o->diagnostic_received);
 fprintf(f,"\n\"schema\":1,\"mode\":\"%s\",\"started_utc\":\"%s\",\"ended_utc\":\"%s\","
 "\"elapsed_s\":%.6f,\"requested_duration_s\":%.3f,\"complete\":%s,\"exit_code\":%d,\"signal\":%d,"
 "\"discard_buffered\":%s,\"io_error\":%s,\"silent\":%s,\"silent_count\":%"PRIu64",\n",
 mode,o->started_utc,utc,monotonic_now()-o->start,duration,complete?"true":"false",code,sig,
 discard?"true":"false",o->io_error?"true":"false",o->silent?"true":"false",o->silent_count);
 fprintf(f,"\"bytes\":%"PRIu64",\"frames\":%"PRIu64",\"valid_records\":%"PRIu64
 ",\"overlong\":%"PRIu64",\"sync_discards\":%"PRIu64",\"resyncs\":%"PRIu64
 ",\"discarded_bytes\":%"PRIu64",\"abandoned_bytes\":%"PRIu64",\"pending_bytes\":%zu,\"dropping\":%s,\n",
 s->bytes,s->frames,o->valid,s->overlong,s->sync_discards,s->resyncs,s->discarded,s->abandoned,s->len,s->dropping?"true":"false");
 fprintf(f,"\"open_errors\":%"PRIu64",\"link_errors\":%"PRIu64",\"opens\":%"PRIu64
 ",\"sequence\":{\"repeat\":%"PRIu64",\"next\":%"PRIu64",\"jump\":%"PRIu64",\"wrap\":%"PRIu64
 ",\"backward\":%"PRIu64",\"millis_wrap_candidates\":%"PRIu64"},\n\"parse_errors\":{",
 o->open_errors,o->link_errors,o->opens,o->seq_repeat,o->seq_next,o->seq_jump,o->seq_wrap,o->seq_backward,o->ms_wrap);
 for(int i=1;i<P_COUNT;++i) fprintf(f,"%s\"%s\":%"PRIu64,i>1?",":"",parse_names[i],o->errors[i]);
 fprintf(f,"}");
 if(o->db_enabled) {
  fprintf(f,",\n\"storage\":");storage_write_stats(f,&o->storage_stats);
 }
 if(o->mqtt_enabled) {
  fprintf(f,",\n\"mqtt\":");mqtt_write_stats(f,&o->mqtt_stats);
 }
 fprintf(f,"\n}\n"); int bad=ferror(f); if(fclose(f)) bad=1;
 return bad?1:code;
}
