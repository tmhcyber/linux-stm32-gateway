#define _GNU_SOURCE
#include "mqtt.h"
#include "output.h"
#include <mosquitto.h>
#include <errno.h>
#include <inttypes.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <sys/random.h>
#include <sys/syscall.h>
#include <time.h>
#include <unistd.h>

/* Linux timer signal targets only gw-mqtt, interrupting synchronous TCP connect.
 * No helper thread, alarm(), process-wide timer or global kernel policy change. */
static _Thread_local volatile sig_atomic_t tcp_expired;
static struct sigaction previous_timer_action;
static void connect_alarm(int sig) { (void)sig;tcp_expired=1; }

/* Fixed latest mailboxes. Only the worker touches mosq, mids and wire deadlines.
 * The mutex protects mailboxes, stop and published statistics; never network/I/O. */
typedef struct {
 char json[MQTT_JSON_CAPACITY];
 uint64_t generation, sent;
 int have, mid;
 double ack_deadline;
} Slot;
struct Mqtt {
 pthread_t thread;
 pthread_mutex_t mutex;
 int stop, port;
 double stop_deadline;
 char gateway[33], device[33], run[33], topics[3][128], client[64];
 Slot slots[3];
 MqttStats stats;
 FILE *events;
 struct mosquitto *mosq;
 int connected, connection_error, ever_connected, ack_seen;
 double connect_deadline, state_at;
 int last_open, last_silent, last_db_state;
 Point source;
 uint64_t last_diagnostic_received;
 timer_t connect_timer;
};
int mqtt_library_init(void) {
 int rc=mosquitto_lib_init();if(rc) return rc;
 struct sigaction action={0};action.sa_handler=connect_alarm;sigemptyset(&action.sa_mask);
 if(sigaction(SIGRTMIN,&action,&previous_timer_action)) {mosquitto_lib_cleanup();return 1;}
 return 0;
}
void mqtt_library_cleanup(void) {
 (void)mosquitto_lib_cleanup();(void)sigaction(SIGRTMIN,&previous_timer_action,NULL);
}
int mqtt_identifier_valid(const char *s) {
 if(!s||!*s||strlen(s)>32) return 0;
 for(;*s;++s) if(!((*s>='a'&&*s<='z')||(*s>='A'&&*s<='Z')||
                 (*s>='0'&&*s<='9')||*s=='-'||*s=='_')) return 0;
 return 1;
}
static void diagnostic(Mqtt *m, const char *kind, int detail) {
 char utc[40]; utc_now(utc,sizeof(utc));
 int bad=fprintf(m->events,"{\"utc\":\"%s\",\"monotonic_s\":%.6f,\"kind\":\"%s\",\"detail\":%d}\n",
                 utc,monotonic_now(),kind,detail)<0;
 if(fflush(m->events)) bad=1;
 if(bad) {pthread_mutex_lock(&m->mutex);m->stats.diagnostic_io_error=1;pthread_mutex_unlock(&m->mutex);}
}
/* Reject truncation rather than publishing malformed JSON. */
static int append(char *json, size_t *used, const char *format, ...) {
 if(*used>=MQTT_JSON_CAPACITY) return -1;
 va_list args;va_start(args,format);
 int n=vsnprintf(json+*used,MQTT_JSON_CAPACITY-*used,format,args);va_end(args);
 if(n<0||(size_t)n>=MQTT_JSON_CAPACITY-*used) {*used=MQTT_JSON_CAPACITY;return -1;}
 *used+=(size_t)n;return 0;
}
static void offer(Mqtt *m, int topic, const char *json, size_t length) {
 pthread_mutex_lock(&m->mutex);
 if(length>=MQTT_JSON_CAPACITY) ++m->stats.format_errors;
 else {
  Slot *s=&m->slots[topic];
  if(s->have&&s->generation!=s->sent) ++m->stats.overwritten[topic];
  memcpy(s->json,json,length+1);++s->generation;s->have=1;
 }
 pthread_mutex_unlock(&m->mutex);
}
void mqtt_record(Mqtt *m, const StoredRecord *r) {
 if(m->source.quality!=r->record.points[0].quality||m->source.status!=r->record.points[0].status) m->state_at=0;
 m->source=r->record.points[0];
 char json[MQTT_JSON_CAPACITY];size_t n=0;
 const Record *v=&r->record;
 append(json,&n,"{\"gateway_id\":\"%s\",\"device_id\":\"%s\",\"gateway_run_id\":\"%s\","
 "\"rx_id\":%"PRIu64",\"utc\":\"%s\",\"elapsed_s\":%.6f,\"session\":%"PRIu64","
 "\"raw_start\":%"PRIu64",\"raw_end\":%"PRIu64",\"v\":%u,\"reason\":\"%s\","
 "\"flags\":%u,\"gap\":%u,\"coal\":%u,\"snap\":%u,\"n\":2,\"sequence_relation\":\"%s\",\"points\":[",
 m->gateway,m->device,m->run,r->rx_id,r->utc,r->elapsed_s,r->session,r->raw_start,r->raw_end,
 v->version,v->heartbeat?"HEARTBEAT":"EVENT",v->flags,v->gap,v->coal,v->snap,r->relation);
 for(int i=0;i<2;++i) {
  const Point *p=&v->points[i];
  append(json,&n,"%s{\"src\":\"SHT30\",\"id\":\"%s\",\"value\":%"PRId32",\"scale\":%d,\"unit\":\"%s\","
   "\"quality\":\"%s\",\"status\":\"%s\",\"value_ms\":%u,\"status_ms\":%u,\"seq\":%u,"
   "\"value_kind\":\"%s\",\"is_current\":%s}",i?",":"",i?"HUMIDITY":"TEMPERATURE",p->value,p->scale,
   i?"PERCENT_RH":"DEG_C",quality_names[p->quality],status_names[p->status],p->value_ms,p->status_ms,p->seq,
   !p->seq?"not_available":p->quality==Q_VALID?"current":"last_valid",p->quality==Q_VALID?"true":"false");
 }
 append(json,&n,"]}");offer(m,0,json,n);
 pthread_mutex_lock(&m->mutex);++m->stats.received;pthread_mutex_unlock(&m->mutex);
}
void mqtt_state(Mqtt *m, int opened, int silent, double age, uint64_t received,
                int db, const StorageStats *s, int force,
                const StoredRecord *diagnostic, uint64_t diagnostic_received, double diagnostic_age) {
 double now=monotonic_now();int db_state=!db?0:s->degraded?2:s->stopped?3:1;
 if(!force&&now-m->state_at<5&&m->last_open==opened&&m->last_silent==silent&&m->last_db_state==db_state&&m->last_diagnostic_received==diagnostic_received) return;
 m->last_diagnostic_received=diagnostic_received;
 m->state_at=now;m->last_open=opened;m->last_silent=silent;m->last_db_state=db_state;
 char json[MQTT_JSON_CAPACITY],utc[40];size_t n=0;utc_now(utc,sizeof(utc));
 append(json,&n,"{\"gateway_id\":\"%s\",\"device_id\":\"%s\",\"gateway_run_id\":\"%s\",\"observed_utc\":\"%s\","
  "\"serial\":\"%s\",\"legal_frames\":%"PRIu64",\"legal_frame_age_s\":",m->gateway,m->device,m->run,utc,
  opened<0?"replay":!opened?"disconnected":silent?"silent":received?"receiving":"waiting",received);
 if(received) append(json,&n,"%.6f",age);else append(json,&n,"null");
 append(json,&n,",\"source_quality\":\"%s\",\"source_status\":\"%s\",\"sample_seq\":%u,\"value_ms\":%u,\"status_ms\":%u,"
  "\"storage\":\"%s\",\"storage_committed\":%"PRIu64",\"storage_failures\":%"PRIu64,
  quality_names[m->source.quality],status_names[m->source.status],m->source.seq,m->source.value_ms,m->source.status_ms,
  !db?"disabled":s->degraded?"degraded":s->stopped?"stopped":"ready",s->committed,s->failures);
 append(json,&n,",\"diagnostic\":{\"received\":%"PRIu64",\"age_s\":",diagnostic_received);
 if(diagnostic_received) append(json,&n,"%.6f",diagnostic_age);else append(json,&n,"null");
 append(json,&n,",\"transport_fresh\":%s,\"latest\":",diagnostic_received&&opened>0&&diagnostic_age<15?"true":"false");
 if(diagnostic_received) {
  const Diagnostic *d=&diagnostic->diagnostic;
  append(json,&n,"{\"diagnostic_rx_id\":%"PRIu64",\"utc\":\"%s\",\"session\":%"PRIu64",\"elapsed_s\":%.6f",
         diagnostic->rx_id,diagnostic->utc,diagnostic->session,diagnostic->elapsed_s);
#define FIELD(name) append(json,&n,",\""#name"\":%"PRIu32,d->name);
  DIAGNOSTIC_FIELDS(FIELD)
#undef FIELD
  append(json,&n,",\"fault_record_valid\":%s,\"pc_valid\":%s}",d->fault_status==1?"true":"false",d->fault_status==1&&d->frame_valid?"true":"false");
 } else append(json,&n,"null");
 append(json,&n,"}}");offer(m,1,json,n);
}
static void availability(Mqtt *m, const char *value) {
 char json[MQTT_JSON_CAPACITY],utc[40];size_t n=0;utc_now(utc,sizeof(utc));
 append(json,&n,"{\"gateway_id\":\"%s\",\"gateway_run_id\":\"%s\",\"status\":\"%s\",\"observed_utc\":\"%s\"}",
 m->gateway,m->run,value,utc);offer(m,2,json,n);
}
static void on_connect(struct mosquitto *mosq, void *context, int code) {
 (void)mosq;Mqtt *m=context;
 if(code) {m->connection_error=MOSQ_ERR_CONN_REFUSED;return;}
 /* Callbacks only record state; publication and destruction stay in the loop. */
 m->connected=1;
}
static void on_disconnect(struct mosquitto *mosq, void *context, int code) {
 (void)mosq;Mqtt *m=context;m->connected=0;
 if(code) m->connection_error=code;
}
static void on_publish(struct mosquitto *mosq, void *context, int mid) {
 (void)mosq;Mqtt *m=context;
 m->ack_seen=1;
 pthread_mutex_lock(&m->mutex);
 for(int i=0;i<3;++i) if(m->slots[i].mid==mid) {
  m->slots[i].mid=0;++m->stats.pubacks[i];break;
 }
 pthread_mutex_unlock(&m->mutex);
}
static void counts_locked(Mqtt *m) {
 unsigned pending=0,flight=0;
 for(int i=0;i<3;++i) {pending+=m->slots[i].have&&m->slots[i].generation!=m->slots[i].sent;flight+=m->slots[i].mid!=0;}
 m->stats.pending=pending;m->stats.in_flight=flight;
 if(pending+flight>m->stats.high_water) m->stats.high_water=pending+flight;
}
static int publish_pending(Mqtt *m) {
 for(int i=0;i<3;++i) {
  char json[MQTT_JSON_CAPACITY];uint64_t generation=0;
  pthread_mutex_lock(&m->mutex);Slot *s=&m->slots[i];
  if(!s->mid&&s->have&&s->generation!=s->sent) {memcpy(json,s->json,sizeof(json));generation=s->generation;++m->stats.attempts[i];}
  pthread_mutex_unlock(&m->mutex);
  if(!generation) continue;
  int mid=0,rc=mosquitto_publish(m->mosq,&mid,m->topics[i],(int)strlen(json),json,1,i!=0);
  if(rc) return rc;
  pthread_mutex_lock(&m->mutex);s->mid=mid;s->sent=generation;s->ack_deadline=monotonic_now()+5;
  counts_locked(m);pthread_mutex_unlock(&m->mutex);
 }
 return 0;
}
static void destroy_client(Mqtt *m, int reschedule) {
 if(m->mosq) {mosquitto_destroy(m->mosq);m->mosq=NULL;}
 m->connected=0;m->connection_error=0;m->ack_seen=0;
 pthread_mutex_lock(&m->mutex);m->stats.connected=0;
 /* Drop the entire library retransmission queue. Retain only current snapshots. */
 for(int i=0;i<3;++i) {
  m->slots[i].mid=0;
  if(reschedule) m->slots[i].sent=0;else m->slots[i].have=0;
 }
 counts_locked(m);pthread_mutex_unlock(&m->mutex);
}
static int connect_client(Mqtt *m, double shutdown_at) {
 m->mosq=mosquitto_new(m->client,true,m);if(!m->mosq) return MOSQ_ERR_NOMEM;
 int rc=mosquitto_threaded_set(m->mosq,true);
 if(!rc) rc=mosquitto_int_option(m->mosq,MOSQ_OPT_PROTOCOL_VERSION,MQTT_PROTOCOL_V311);
 if(!rc) rc=mosquitto_max_inflight_messages_set(m->mosq,3);
 char will[256];
 int n=snprintf(will,sizeof(will),"{\"gateway_id\":\"%s\",\"gateway_run_id\":\"%s\",\"status\":\"offline\",\"reason\":\"connection_lost\"}",m->gateway,m->run);
 if(n<0||n>=(int)sizeof(will)) return MOSQ_ERR_INVAL;
 if(!rc) rc=mosquitto_will_set(m->mosq,m->topics[2],n,will,1,true);
 mosquitto_connect_callback_set(m->mosq,on_connect);
 mosquitto_disconnect_callback_set(m->mosq,on_disconnect);
 mosquitto_publish_callback_set(m->mosq,on_publish);
 pthread_mutex_lock(&m->mutex);++m->stats.connects;pthread_mutex_unlock(&m->mutex);
 m->connect_deadline=monotonic_now()+5;
 if(!rc) {
  double timeout=1;
  if(shutdown_at&&shutdown_at-monotonic_now()<timeout) timeout=shutdown_at-monotonic_now();
  if(timeout<=0) return MOSQ_ERR_TIMEOUT;
  struct itimerspec limit={0},disarmed={0};
  limit.it_value.tv_sec=(time_t)timeout;
  limit.it_value.tv_nsec=(long)((timeout-(double)limit.it_value.tv_sec)*1e9);
  if(!limit.it_value.tv_sec&&!limit.it_value.tv_nsec) limit.it_value.tv_nsec=1;
  tcp_expired=0;
  if(timer_settime(m->connect_timer,0,&limit,NULL)) return MOSQ_ERR_ERRNO;
  rc=mosquitto_connect(m->mosq,"127.0.0.1",m->port,10);
  (void)timer_settime(m->connect_timer,0,&disarmed,NULL);
  if(tcp_expired) {
   pthread_mutex_lock(&m->mutex);++m->stats.connect_timeouts;pthread_mutex_unlock(&m->mutex);
   rc=MOSQ_ERR_TIMEOUT;
  }
 }
 return rc;
}
static void *worker(void *context) {
 Mqtt *m=context;pthread_setname_np(pthread_self(),"gw-mqtt");
 struct sigevent timer_event={.sigev_notify=SIGEV_THREAD_ID,.sigev_signo=SIGRTMIN};
 timer_event._sigev_un._tid=(pid_t)syscall(SYS_gettid);
 int timer_ready=timer_create(CLOCK_MONOTONIC,&timer_event,&m->connect_timer)==0;
 if(!timer_ready) {
  pthread_mutex_lock(&m->mutex);++m->stats.failures;m->stats.last_error=MOSQ_ERR_ERRNO;pthread_mutex_unlock(&m->mutex);
  diagnostic(m,"TIMER_ERROR",errno);
 }
 double retry=0,shutdown_at=0;unsigned backoff=1;int online_announced=0,closing=0;
 for(;timer_ready;) {
  double now=monotonic_now();int stop;
  pthread_mutex_lock(&m->mutex);stop=m->stop;shutdown_at=m->stop_deadline;pthread_mutex_unlock(&m->mutex);
  if(stop&&now>=shutdown_at) {diagnostic(m,"SHUTDOWN_DEADLINE",0);break;}
  int rc=0;
  if(!m->mosq&&now>=retry) {rc=connect_client(m,shutdown_at);online_announced=0;closing=0;}
  if(!rc&&m->mosq) {
   rc=mosquitto_loop(m->mosq,50,1);
   if(!rc&&m->connection_error) rc=m->connection_error;
   now=monotonic_now();
   if(!rc&&!m->connected&&now>=m->connect_deadline) {
    rc=MOSQ_ERR_TIMEOUT;pthread_mutex_lock(&m->mutex);++m->stats.connect_timeouts;pthread_mutex_unlock(&m->mutex);
   }
   if(!rc&&m->connected&&!online_announced) {
    pthread_mutex_lock(&m->mutex);++m->stats.connacks;m->stats.connected=1;
    if(m->ever_connected||m->stats.failures) ++m->stats.recoveries;
    pthread_mutex_unlock(&m->mutex);m->ever_connected=1;online_announced=1;
    availability(m,"online");diagnostic(m,"CONNACK",0);
   }
   if(!rc&&m->connected) {
    pthread_mutex_lock(&m->mutex);counts_locked(m);
    int drained=m->stats.pending==0&&m->stats.in_flight==0;
    int timed_out=0;
    for(int i=0;i<3;++i) if(m->slots[i].mid&&now>=m->slots[i].ack_deadline) timed_out=1;
    if(timed_out) ++m->stats.ack_timeouts;
    pthread_mutex_unlock(&m->mutex);
    if(timed_out) {rc=MOSQ_ERR_TIMEOUT;diagnostic(m,"ACK_TIMEOUT",0);}
    else if(stop&&closing&&drained) {
     pthread_mutex_lock(&m->mutex);m->stats.offline_acked=1;pthread_mutex_unlock(&m->mutex);
     (void)mosquitto_disconnect(m->mosq);
     /* DISCONNECT may be queued in threaded mode; drive it before destruction. */
     (void)mosquitto_loop(m->mosq,50,1);diagnostic(m,"OFFLINE_ACKED",0);break;
    } else {
     if(stop&&!closing&&drained) {availability(m,"offline");closing=1;}
     rc=publish_pending(m);
    }
   }
  } else if(!rc) {struct timespec delay={.tv_nsec=50000000};nanosleep(&delay,NULL);}
  if(rc) {
   pthread_mutex_lock(&m->mutex);++m->stats.failures;m->stats.last_error=rc;pthread_mutex_unlock(&m->mutex);
   diagnostic(m,"CONNECTION_ERROR",rc);destroy_client(m,1);
   retry=monotonic_now()+backoff;diagnostic(m,"RETRY_SECONDS",(int)backoff);
   backoff=backoff<16?backoff*2:30;
  }
  /* Stable acknowledged connection resets retry delay, not TCP connect alone. */
  if(m->connected&&m->ack_seen) backoff=1;
 }
 destroy_client(m,0);
 if(timer_ready) (void)timer_delete(m->connect_timer);
 if(fclose(m->events)) {pthread_mutex_lock(&m->mutex);m->stats.diagnostic_io_error=1;pthread_mutex_unlock(&m->mutex);}
 pthread_mutex_lock(&m->mutex);m->stats.stopped=1;pthread_mutex_unlock(&m->mutex);return NULL;
}
Mqtt *mqtt_start(int port, const char *gateway, const char *device, const char *dir, const char *run) {
 Mqtt *m=calloc(1,sizeof(*m));if(!m) return NULL;
 if(pthread_mutex_init(&m->mutex,NULL)) {free(m);return NULL;}
 m->port=port;snprintf(m->gateway,sizeof(m->gateway),"%s",gateway);snprintf(m->device,sizeof(m->device),"%s",device);
 snprintf(m->run,sizeof(m->run),"%s",run);
 snprintf(m->client,sizeof(m->client),"gateway-%s",gateway);
 snprintf(m->topics[0],sizeof(m->topics[0]),"lab/%s/telemetry",device);
 snprintf(m->topics[1],sizeof(m->topics[1]),"lab/%s/device/state",device);
 snprintf(m->topics[2],sizeof(m->topics[2]),"lab/gateway/%s/availability",gateway);
 char path[4096];if(snprintf(path,sizeof(path),"%s/mqtt-events.jsonl",dir)>=(int)sizeof(path)) goto fail;
 m->events=fopen(path,"wx");if(!m->events) goto fail;
 sigset_t blocked,old;sigemptyset(&blocked);sigaddset(&blocked,SIGINT);sigaddset(&blocked,SIGTERM);
 int rc=pthread_sigmask(SIG_BLOCK,&blocked,&old);if(rc) goto fail;
 rc=pthread_create(&m->thread,NULL,worker,m);pthread_sigmask(SIG_SETMASK,&old,NULL);
 if(rc) goto fail;
 return m;
fail:
 if(m->events) fclose(m->events);
 pthread_mutex_destroy(&m->mutex);free(m);return NULL;
}
void mqtt_snapshot(Mqtt *m, MqttStats *stats) {
 pthread_mutex_lock(&m->mutex);counts_locked(m);*stats=m->stats;pthread_mutex_unlock(&m->mutex);
}
void mqtt_finish(Mqtt *m, MqttStats *stats) {
 pthread_mutex_lock(&m->mutex);m->stop=1;m->stop_deadline=monotonic_now()+2;pthread_mutex_unlock(&m->mutex);
 pthread_join(m->thread,NULL);mqtt_snapshot(m,stats);pthread_mutex_destroy(&m->mutex);free(m);
}
int mqtt_has_errors(const MqttStats *s) {return s->failures||s->format_errors||s->diagnostic_io_error||!s->offline_acked;}
void mqtt_write_stats(FILE *f, const MqttStats *s) {
 fprintf(f,"{\"received\":%"PRIu64",\"connected\":%s,\"stopped\":%s,\"connects\":%"PRIu64",\"connacks\":%"PRIu64
 ",\"failures\":%"PRIu64",\"recoveries\":%"PRIu64",\"ack_timeouts\":%"PRIu64",\"connect_timeouts\":%"PRIu64
 ",\"pending\":%u,\"in_flight\":%u,\"high_water\":%u,\"last_error\":%d,\"format_errors\":%d,"
 "\"diagnostic_io_error\":%s,\"offline_acked\":%s,\"topics\":[",s->received,s->connected?"true":"false",s->stopped?"true":"false",
 s->connects,s->connacks,s->failures,s->recoveries,s->ack_timeouts,s->connect_timeouts,s->pending,s->in_flight,s->high_water,
 s->last_error,s->format_errors,s->diagnostic_io_error?"true":"false",s->offline_acked?"true":"false");
 for(int i=0;i<3;++i) fprintf(f,"%s{\"overwritten\":%"PRIu64",\"attempts\":%"PRIu64",\"pubacks\":%"PRIu64"}",
 i?",":"",s->overwritten[i],s->attempts[i],s->pubacks[i]);
 fprintf(f,"]}");
}
