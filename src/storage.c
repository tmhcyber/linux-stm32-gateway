#define _GNU_SOURCE
#include "history.h"
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <inttypes.h>
#include <errno.h>
#include <signal.h>
#include <sys/eventfd.h>
#include <unistd.h>

#define BATCH 10
#define DIAGNOSTICS 16
struct Storage {
 RecordQueue q;
 pthread_t thread;
 StorageStats stats;
 const char *path, *utc, *mode, *output_dir;
 double start, shutdown_at;
 int ready, init_error, code, sig, complete;
 int query_fd;
 Query queries[QUERY_CAPACITY];
 QueryResult results[QUERY_CAPACITY];
 size_t query_head, query_count, result_head, result_count, outstanding;
};
static double now(void) {
 struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);return t.tv_sec+t.tv_nsec/1e9;
}
static struct timespec at(double value) {
 struct timespec t={.tv_sec=(time_t)value};t.tv_nsec=(long)((value-t.tv_sec)*1e9);return t;
}
static void utc_at(time_t value, char *buffer, size_t size) {
 struct tm tm;gmtime_r(&value,&tm);strftime(buffer,size,"%Y-%m-%dT%H:%M:%SZ",&tm);
}
int storage_has_errors(const StorageStats *s) {
 return s->error||s->failures||s->dropped_full||s->rejected||s->failed_records||
  s->mcu_dropped_full||s->mcu_rejected||s->mcu_failed||s->event_dropped_full||s->event_rejected||s->event_failed||s->diagnostic_dropped||s->diagnostic_io_error;
}
void storage_write_stats(FILE *f, const StorageStats *s) {
 fprintf(f,"{\"capacity\":256,\"run_id\":\"%s\",\"state\":\"%s\",\"queued\":%zu,\"in_flight\":%zu,\"high_water\":%zu",
  s->run_id,s->stopped?"CLOSED":s->degraded?"DEGRADED":"HEALTHY",s->queued,s->in_flight,s->high_water);
#define U(label,member) fprintf(f,",\""label"\":%"PRIu64,s->member)
 U("mcu_submitted",mcu_submitted);U("mcu_dropped_full",mcu_dropped_full);U("mcu_rejected",mcu_rejected);U("mcu_committed",mcu_committed);U("mcu_failed",mcu_failed);
 U("retention_diagnostics",retention_diagnostics);
 U("submitted",submitted);U("dropped_full",dropped_full);U("rejected",rejected);
 U("committed",committed);U("commits",commits);U("last_rx_id",last_rx_id);U("failed_records",failed_records);
 U("event_submitted",event_submitted);U("event_dropped_full",event_dropped_full);
 U("event_rejected",event_rejected);U("event_committed",event_committed);U("event_failed",event_failed);
 U("worker_events_committed",worker_events_committed);U("diagnostic_dropped",diagnostic_dropped);
 U("failures",failures);U("recoveries",recoveries);U("busy_retries",busy_retries);
 U("retention_frames",retention_frames);U("retention_points",retention_points);
 U("retention_events",retention_events);U("retention_runs",retention_runs);U("retention_batches",retention_batches);
#undef U
 fprintf(f,",\"queries\":{\"capacity\":8,\"outstanding\":%zu,\"high_water\":%zu,\"submitted\":%"PRIu64",\"completed\":%"PRIu64",\"failed\":%"PRIu64",\"expired\":%"PRIu64",\"cancelled\":%"PRIu64",\"max_execution_ms\":%.6f}",
  s->query_outstanding,s->query_high_water,s->query_submitted,s->query_completed,s->query_failed,s->query_expired,s->query_cancelled,s->query_max_ms);
 fprintf(f,",\"sqlite_error\":%d,\"last_sqlite_error\":%d,\"diagnostic_io_error\":%d}",
  s->error,s->last_error,s->diagnostic_io_error);
}
static void failure(Storage *s, int rc) {
 pthread_mutex_lock(&s->q.mutex);s->stats.error=s->stats.last_error=rc;
 ++s->stats.failures;s->stats.degraded=1;
 if((rc&255)==SQLITE_BUSY||(rc&255)==SQLITE_LOCKED) ++s->stats.busy_retries;
 pthread_mutex_unlock(&s->q.mutex);
}
static void diagnostic(Storage *s, FILE *log, StoredRecord *pending, size_t *count,
                        int64_t *id, const char *kind, int detail, uint64_t offset) {
 StoredRecord event={.is_event=1,.event_id=--*id,.detail=detail,.raw_start=offset,.elapsed_s=now()-s->start};
 utc_at(time(NULL),event.utc,sizeof(event.utc));snprintf(event.kind,sizeof(event.kind),"%s",kind);
 int bad=fprintf(log,"{\"event_id\":%"PRId64",\"utc\":\"%s\",\"elapsed_s\":%.6f,\"kind\":\"%s\",\"detail\":%d,\"raw_offset\":%"PRIu64"}\n",
   event.event_id,event.utc,event.elapsed_s,kind,detail,offset)<0||fflush(log)!=0;
 pthread_mutex_lock(&s->q.mutex);
 if(bad) s->stats.diagnostic_io_error=1;
 if(*count<DIAGNOSTICS) pending[(*count)++]=event;
 else ++s->stats.diagnostic_dropped;
 pthread_mutex_unlock(&s->q.mutex);
}
/* Wait until the retry deadline, ignoring input wakeups. Shutdown shortens the wait. */
static int retry_wait(Storage *s) {
 double retry=now()+1.;int expired=0;
 pthread_mutex_lock(&s->q.mutex);
 for(;;) {
  double current=now(),until=retry;
  if(s->q.closed&&s->shutdown_at<until) until=s->shutdown_at;
  if(current>=until) {expired=s->q.closed&&current>=s->shutdown_at;break;}
  struct timespec deadline=at(until);pthread_cond_timedwait(&s->q.changed,&s->q.mutex,&deadline);
 }
 pthread_mutex_unlock(&s->q.mutex);return expired;
}
static int shutting_down(Storage *s) {
 pthread_mutex_lock(&s->q.mutex);int result=s->q.closed;pthread_mutex_unlock(&s->q.mutex);return result;
}
static void lost(StorageStats *s, const StoredRecord *r) {
 if(r->is_diagnostic) ++s->mcu_failed;
 else if(r->is_event) ++s->event_failed;else ++s->failed_records;
}
static void process_query(Storage *s, History *h) {
 Query q;int have=0;
 pthread_mutex_lock(&s->q.mutex);
 if(s->query_count&&!s->q.closed) {
  q=s->queries[s->query_head];s->query_head=(s->query_head+1)%QUERY_CAPACITY;--s->query_count;have=1;
 }
 pthread_mutex_unlock(&s->q.mutex);
 if(!have) return;
 double began=now();QueryResult result;history_query(h,&q,&result);double elapsed=(now()-began)*1000;
 pthread_mutex_lock(&s->q.mutex);
 ++s->stats.query_completed;if(result.code==5) ++s->stats.query_expired;else if(result.code==6) ++s->stats.query_failed;
 if(elapsed>s->stats.query_max_ms) s->stats.query_max_ms=elapsed;
 s->results[(s->result_head+s->result_count)%QUERY_CAPACITY]=result;++s->result_count;
 pthread_mutex_unlock(&s->q.mutex);
 uint64_t one=1;while(write(s->query_fd,&one,sizeof(one))<0&&errno==EINTR) {}
}
static void *worker(void *arg) {
 /* A role name lets diagnostics distinguish our worker from sanitizer helpers. */
 (void)pthread_setname_np(pthread_self(),"gw-storage");
 Storage *s=arg;History h;FILE *log=NULL;char path[4096];int rc=SQLITE_OK;
#ifdef STORAGE_TESTING
 storage_test_vfs_init();
#endif
 if(snprintf(path,sizeof(path),"%s/storage-events.jsonl",s->output_dir)>=(int)sizeof(path)) rc=SQLITE_CANTOPEN;
 if(rc==SQLITE_OK) {log=fopen(path,"wx");if(!log) rc=SQLITE_CANTOPEN;}
 memset(&h,0,sizeof(h));
 if(rc==SQLITE_OK) rc=history_open(&h,s->path,s->utc,s->start,s->mode,s->output_dir);
 pthread_mutex_lock(&s->q.mutex);s->ready=1;s->init_error=rc;s->stats.error=rc;s->stats.last_error=rc;
 snprintf(s->stats.run_id,sizeof(s->stats.run_id),"%s",h.id);
 pthread_cond_broadcast(&s->q.changed);pthread_mutex_unlock(&s->q.mutex);
 StoredRecord items[BATCH],diags[DIAGNOSTICS];size_t count=0,diag_count=0;
 int64_t diagnostic_id=0;double deadline=0,cleanup_at=now(),interval=3600.;uint64_t offset=0;
#ifdef STORAGE_TESTING
 interval=storage_test_retention_interval();
#endif
 int degraded=0,aborted=0,cleanup_more=0;
 double query_at=0;
 if(rc==SQLITE_OK) diagnostic(s,log,diags,&diag_count,&diagnostic_id,"STORAGE_READY",0,0);
 while(rc==SQLITE_OK) {
  if(!degraded&&now()>=query_at) {process_query(s,&h);query_at=now()+.1;}
  int closed=0;
  pthread_mutex_lock(&s->q.mutex);
  while(count<BATCH&&s->q.count) {
   if(!count) deadline=now()+1.;
   queue_pop_locked(&s->q,&items[count]);offset=items[count].is_event?items[count].raw_start:items[count].raw_end;
   ++count;
  }
  s->stats.queued=s->q.count;s->stats.in_flight=count;
  closed=s->q.closed&&!s->q.count;
  double current=now();
  if(count<BATCH&&!closed&&current<cleanup_at&&(!count||current<deadline)&&!degraded) {
   double until=cleanup_at;if(count&&deadline<until) until=deadline;
   /* Diagnostics alone have a finite commit deadline as well. */
   if(diag_count&&current+1.<until) until=current+1.;
   if(s->query_count&&query_at<until) until=query_at;
   struct timespec t=at(until);int wait=pthread_cond_timedwait(&s->q.changed,&s->q.mutex,&t);
   if(wait&&wait!=ETIMEDOUT) rc=SQLITE_INTERNAL;
   pthread_mutex_unlock(&s->q.mutex);
   if(rc!=SQLITE_OK) break;
   if(wait!=ETIMEDOUT) continue;
  } else pthread_mutex_unlock(&s->q.mutex);
  current=now();
  if(count||diag_count) {
   /* A partial batch is normally held for one second; a cleanup tick must not force it early. */
   if(count<BATCH&&!closed&&!degraded&&current<deadline) {
    /* cleanup is handled below, then wait for the original batch deadline */
   } else {
    StorageStats after;storage_snapshot(s,&after);
    for(size_t i=0;i<count;++i) {
     if(items[i].is_diagnostic) ++after.mcu_committed;
     else if(items[i].is_event) ++after.event_committed;
     else {++after.committed;after.last_rx_id=items[i].rx_id;}
    }
    after.worker_events_committed+=diag_count;++after.commits;
#ifdef STORAGE_TESTING
    rc=storage_test_before_batch(h.db);
#endif
    if(rc==SQLITE_OK) rc=history_batch(&h,items,count,diags,diag_count,&after);
    if(rc==SQLITE_OK) {
     pthread_mutex_lock(&s->q.mutex);
     s->stats.mcu_committed=after.mcu_committed;
     s->stats.committed=after.committed;s->stats.event_committed=after.event_committed;
     s->stats.worker_events_committed=after.worker_events_committed;
     s->stats.commits=after.commits;s->stats.last_rx_id=after.last_rx_id;s->stats.in_flight=0;
     s->stats.error=0;s->stats.degraded=0;if(degraded) ++s->stats.recoveries;
     pthread_mutex_unlock(&s->q.mutex);count=diag_count=0;
     if(degraded) diagnostic(s,log,diags,&diag_count,&diagnostic_id,"STORAGE_RECOVERED",0,offset);
     degraded=0;
    }
   }
  }
  if(rc==SQLITE_OK&&now()>=cleanup_at&&!shutting_down(s)) {
   char cutoff[40];utc_at(time(NULL)-7*24*3600,cutoff,sizeof(cutoff));Pruned pruned;
   rc=history_prune(&h,cutoff,&pruned);
   if(rc==SQLITE_OK) {
    pthread_mutex_lock(&s->q.mutex);s->stats.retention_frames+=pruned.frames;s->stats.retention_points+=pruned.points;
    s->stats.retention_diagnostics+=pruned.diagnostics;s->stats.retention_events+=pruned.events;s->stats.retention_runs+=pruned.runs;++s->stats.retention_batches;
    pthread_mutex_unlock(&s->q.mutex);
    cleanup_more=pruned.diagnostics==128||pruned.frames==128||pruned.events==128||pruned.runs==128;
    cleanup_at=now()+(cleanup_more?.01:interval);
   }
  }
  if(rc==SQLITE_OK&&closed&&!count&&!diag_count) {
   StorageStats final;storage_snapshot(s,&final);
   int code=s->sig?128+s->sig:storage_has_errors(&final)?1:s->code;
   rc=history_finish(&h,code,s->sig,s->complete,&final);
   if(rc==SQLITE_OK) break;
  }
  if(rc!=SQLITE_OK) {
   failure(s,rc);
   if(!degraded) diagnostic(s,log,diags,&diag_count,&diagnostic_id,"STORAGE_DEGRADED",rc,offset);
   degraded=1;
   if(retry_wait(s)) {aborted=1;break;}
   rc=SQLITE_OK;continue;
  }
 }
 if(aborted||rc!=SQLITE_OK) {
  if(log) diagnostic(s,log,diags,&diag_count,&diagnostic_id,"STORAGE_ABORT",s->stats.last_error,offset);
  pthread_mutex_lock(&s->q.mutex);
  for(size_t i=0;i<count;++i) lost(&s->stats,&items[i]);
  StoredRecord item;while(queue_pop_locked(&s->q,&item)) lost(&s->stats,&item);
  s->stats.diagnostic_dropped+=diag_count;s->stats.queued=s->stats.in_flight=0;
  pthread_mutex_unlock(&s->q.mutex);
 }
 int close_rc=history_close(&h);
 if(log&&fclose(log)) {pthread_mutex_lock(&s->q.mutex);s->stats.diagnostic_io_error=1;pthread_mutex_unlock(&s->q.mutex);}
 pthread_mutex_lock(&s->q.mutex);
 if(close_rc!=SQLITE_OK) s->stats.error=s->stats.last_error=close_rc;
 s->stats.stopped=1;pthread_mutex_unlock(&s->q.mutex);return NULL;
}
Storage *storage_start(const char *path, const char *utc, double start, const char *mode, const char *dir, StorageStats *initial) {
 memset(initial,0,sizeof(*initial));initial->error=initial->last_error=SQLITE_NOMEM;initial->stopped=1;
 Storage *s=calloc(1,sizeof(*s));if(!s) return NULL;
 s->query_fd=eventfd(0,EFD_CLOEXEC|EFD_NONBLOCK);
 if(s->query_fd<0) {free(s);return NULL;}
 s->path=path;s->utc=utc;s->mode=mode;s->output_dir=dir;s->start=start;
 initial->error=initial->last_error=SQLITE_ERROR;
 if(queue_init(&s->q,STORAGE_CAPACITY)) {close(s->query_fd);free(s);return NULL;}
 /* Worker inherits blocked termination signals; only main's handler accesses its stop flag.
    Block before create, rather than racing a mask change inside the new thread. */
 sigset_t blocked,previous;sigemptyset(&blocked);sigaddset(&blocked,SIGINT);sigaddset(&blocked,SIGTERM);
 if(pthread_sigmask(SIG_BLOCK,&blocked,&previous)) {queue_destroy(&s->q);close(s->query_fd);free(s);return NULL;}
 int create=pthread_create(&s->thread,NULL,worker,s);
 (void)pthread_sigmask(SIG_SETMASK,&previous,NULL);
 if(create) {queue_destroy(&s->q);close(s->query_fd);free(s);return NULL;}
 pthread_mutex_lock(&s->q.mutex);while(!s->ready) pthread_cond_wait(&s->q.changed,&s->q.mutex);
 int error=s->init_error;pthread_mutex_unlock(&s->q.mutex);
 if(error) {pthread_join(s->thread,NULL);*initial=s->stats;queue_destroy(&s->q);close(s->query_fd);free(s);return NULL;}
 storage_snapshot(s,initial);
 return s;
}
void storage_submit(Storage *s, const StoredRecord *r) {
 pthread_mutex_lock(&s->q.mutex);int rc=s->stats.stopped?-1:queue_push(&s->q,r);
 if(r->is_diagnostic) {
  if(rc>0) ++s->stats.mcu_submitted;else if(!rc) ++s->stats.mcu_dropped_full;else ++s->stats.mcu_rejected;
 } else if(r->is_event) {
  if(rc>0) ++s->stats.event_submitted;else if(!rc) ++s->stats.event_dropped_full;else ++s->stats.event_rejected;
 } else {
  if(rc>0) ++s->stats.submitted;else if(!rc) ++s->stats.dropped_full;else ++s->stats.rejected;
 }
 s->stats.queued=s->q.count;if(s->q.count>s->stats.high_water) s->stats.high_water=s->q.count;
 pthread_mutex_unlock(&s->q.mutex);
}
void storage_snapshot(Storage *s, StorageStats *stats) {
 pthread_mutex_lock(&s->q.mutex);*stats=s->stats;pthread_mutex_unlock(&s->q.mutex);
}
void storage_finish(Storage *s, int code, int sig, int complete, StorageStats *stats) {
 pthread_mutex_lock(&s->q.mutex);s->code=code;s->sig=sig;s->complete=complete;s->q.closed=1;s->shutdown_at=now()+2.;
 pthread_cond_broadcast(&s->q.changed);pthread_mutex_unlock(&s->q.mutex);
 pthread_join(s->thread,NULL);s->stats.query_cancelled=s->query_count;s->stats.query_outstanding=0;*stats=s->stats;
 for(size_t i=0;i<s->result_count;++i) free(s->results[(s->result_head+i)%QUERY_CAPACITY].json);
 close(s->query_fd);queue_destroy(&s->q);free(s);
}
int storage_query_fd(Storage *s) {return s->query_fd;}
int storage_query(Storage *s, const Query *q) {
 pthread_mutex_lock(&s->q.mutex);
 int ok=!s->q.closed&&!s->stats.stopped&&s->outstanding<QUERY_CAPACITY;
 if(ok) {
  s->queries[(s->query_head+s->query_count)%QUERY_CAPACITY]=*q;++s->query_count;++s->outstanding;
  ++s->stats.query_submitted;s->stats.query_outstanding=s->outstanding;
  if(s->outstanding>s->stats.query_high_water) s->stats.query_high_water=s->outstanding;
  pthread_cond_signal(&s->q.changed);
 }
 pthread_mutex_unlock(&s->q.mutex);return ok;
}
int storage_query_result(Storage *s, QueryResult *r) {
 pthread_mutex_lock(&s->q.mutex);int ok=s->result_count>0;
 if(ok) {*r=s->results[s->result_head];s->result_head=(s->result_head+1)%QUERY_CAPACITY;--s->result_count;--s->outstanding;s->stats.query_outstanding=s->outstanding;}
 pthread_mutex_unlock(&s->q.mutex);return ok;
}
