#define _POSIX_C_SOURCE 200809L
#include "history.h"
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>
#include <inttypes.h>

#define APPLICATION_ID 1196897073
static const char base_schema[]=
 "CREATE TABLE runs(run_id TEXT PRIMARY KEY,started_utc TEXT NOT NULL,monotonic_start REAL NOT NULL,"
 "mode TEXT NOT NULL,output_dir TEXT NOT NULL,ended_utc TEXT,exit_code INTEGER,signal INTEGER,complete INTEGER,"
 "submitted INTEGER,dropped_full INTEGER,rejected INTEGER,committed INTEGER,commits INTEGER,last_rx_id INTEGER);"
 "CREATE TABLE frames(run_id TEXT NOT NULL REFERENCES runs(run_id),rx_id INTEGER NOT NULL,session INTEGER NOT NULL,"
 "utc TEXT NOT NULL,elapsed_s REAL NOT NULL,raw_start INTEGER NOT NULL,raw_end INTEGER NOT NULL,body BLOB NOT NULL,"
 "v INTEGER NOT NULL,reason TEXT NOT NULL,flags INTEGER NOT NULL,gap INTEGER NOT NULL,coal INTEGER NOT NULL,"
 "snap INTEGER NOT NULL,n INTEGER NOT NULL,sequence_relation TEXT NOT NULL,PRIMARY KEY(run_id,rx_id));"
 "CREATE TABLE points(run_id TEXT NOT NULL,rx_id INTEGER NOT NULL,point_index INTEGER NOT NULL,src TEXT NOT NULL,"
 "id TEXT NOT NULL,value INTEGER NOT NULL,scale INTEGER NOT NULL,unit TEXT NOT NULL,quality TEXT NOT NULL,"
 "status TEXT NOT NULL,value_ms INTEGER NOT NULL,status_ms INTEGER NOT NULL,seq INTEGER NOT NULL,"
 "value_kind TEXT NOT NULL,is_current INTEGER NOT NULL,PRIMARY KEY(run_id,rx_id,point_index),"
 "FOREIGN KEY(run_id,rx_id) REFERENCES frames(run_id,rx_id));"
 "PRAGMA application_id=1196897073;PRAGMA user_version=1;";
static const char migrate_v2[]=
 "CREATE TABLE events(run_id TEXT NOT NULL REFERENCES runs(run_id),event_id INTEGER NOT NULL,"
 "utc TEXT NOT NULL,elapsed_s REAL NOT NULL,raw_offset INTEGER NOT NULL,session INTEGER NOT NULL,"
 "kind TEXT NOT NULL,detail INTEGER NOT NULL,PRIMARY KEY(run_id,event_id));"
 "CREATE INDEX frames_utc ON frames(utc);CREATE INDEX events_utc ON events(utc);"
 "CREATE INDEX runs_utc ON runs(started_utc);"
 "ALTER TABLE runs ADD COLUMN event_submitted INTEGER NOT NULL DEFAULT 0;"
 "ALTER TABLE runs ADD COLUMN event_dropped_full INTEGER NOT NULL DEFAULT 0;"
 "ALTER TABLE runs ADD COLUMN event_rejected INTEGER NOT NULL DEFAULT 0;"
 "ALTER TABLE runs ADD COLUMN event_committed INTEGER NOT NULL DEFAULT 0;"
 "ALTER TABLE runs ADD COLUMN event_failed INTEGER NOT NULL DEFAULT 0;"
 "ALTER TABLE runs ADD COLUMN worker_events_committed INTEGER NOT NULL DEFAULT 0;"
 "ALTER TABLE runs ADD COLUMN failed_records INTEGER NOT NULL DEFAULT 0;"
 "ALTER TABLE runs ADD COLUMN failures INTEGER NOT NULL DEFAULT 0;"
 "ALTER TABLE runs ADD COLUMN recoveries INTEGER NOT NULL DEFAULT 0;"
 "ALTER TABLE runs ADD COLUMN last_sqlite_error INTEGER NOT NULL DEFAULT 0;"
 "PRAGMA user_version=2;";
static const char migrate_v3[]=
 "CREATE TABLE diagnostics(run_id TEXT NOT NULL REFERENCES runs(run_id),diagnostic_rx_id INTEGER NOT NULL,gateway_run_id TEXT NOT NULL,session INTEGER NOT NULL,utc TEXT NOT NULL,elapsed_s REAL NOT NULL,raw_start INTEGER NOT NULL,raw_end INTEGER NOT NULL,body BLOB NOT NULL,d INTEGER NOT NULL,up_ms INTEGER NOT NULL,reset_captured INTEGER NOT NULL,reset_raw INTEGER NOT NULL,reset_reasons INTEGER NOT NULL,fault_status INTEGER NOT NULL,fault_type INTEGER NOT NULL,frame_valid INTEGER NOT NULL,frame_reject INTEGER NOT NULL,pc INTEGER NOT NULL,cfsr INTEGER NOT NULL,hfsr INTEGER NOT NULL,monitor_valid INTEGER NOT NULL,health INTEGER NOT NULL,active INTEGER NOT NULL,observed INTEGER NOT NULL,watchdog_initialized INTEGER NOT NULL,watchdog INTEGER NOT NULL,latched INTEGER NOT NULL,PRIMARY KEY(run_id,diagnostic_rx_id));CREATE INDEX diagnostics_utc ON diagnostics(utc);ALTER TABLE runs ADD COLUMN mcu_submitted INTEGER NOT NULL DEFAULT 0;ALTER TABLE runs ADD COLUMN mcu_dropped_full INTEGER NOT NULL DEFAULT 0;ALTER TABLE runs ADD COLUMN mcu_rejected INTEGER NOT NULL DEFAULT 0;ALTER TABLE runs ADD COLUMN mcu_committed INTEGER NOT NULL DEFAULT 0;ALTER TABLE runs ADD COLUMN mcu_failed INTEGER NOT NULL DEFAULT 0;PRAGMA user_version=3;";
static int sql(sqlite3 *db, const char *text) {return sqlite3_exec(db,text,NULL,NULL,NULL);}
static int scalar(sqlite3 *db, const char *text) {
 sqlite3_stmt *st=NULL; int value=-1;
 if(sqlite3_prepare_v2(db,text,-1,&st,NULL)==SQLITE_OK&&sqlite3_step(st)==SQLITE_ROW)
  value=sqlite3_column_int(st,0);
 sqlite3_finalize(st); return value;
}
static int done(sqlite3_stmt *st, int bound) {
 int rc=bound==SQLITE_OK?sqlite3_step(st):bound;
 sqlite3_reset(st); sqlite3_clear_bindings(st);
 return rc==SQLITE_DONE?SQLITE_OK:rc;
}
#define BIND(call) do {int br=(call);if(rc==SQLITE_OK&&br!=SQLITE_OK) rc=br;} while(0)
#define I(st,n,x) BIND(sqlite3_bind_int64(st,n,(sqlite3_int64)(x)))
#define T(st,n,x) BIND(sqlite3_bind_text(st,n,x,-1,SQLITE_TRANSIENT))
int history_open(History *h, const char *path, const char *utc, double start,
                 const char *mode, const char *dir) {
 memset(h,0,sizeof(*h)); int rc=SQLITE_OK, created=0;
 int fd=open(path,O_CREAT|O_EXCL|O_WRONLY|O_CLOEXEC,0600);
 if(fd>=0) {created=1;if(close(fd)) rc=SQLITE_IOERR;}
 else if(errno!=EEXIST) rc=SQLITE_CANTOPEN;
 if(rc==SQLITE_OK) rc=sqlite3_open_v2(path,&h->db,SQLITE_OPEN_READWRITE,NULL);
 if(rc==SQLITE_OK) rc=sqlite3_extended_result_codes(h->db,1);
 if(rc==SQLITE_OK) rc=sqlite3_busy_timeout(h->db,250);
 int version=rc==SQLITE_OK?scalar(h->db,"PRAGMA user_version"):-1;
 if(rc==SQLITE_OK&&!created&&(scalar(h->db,"PRAGMA application_id")!=APPLICATION_ID||
                            (version!=1&&version!=2&&version!=3))) rc=SQLITE_NOTADB;
 if(rc==SQLITE_OK) rc=sql(h->db,"PRAGMA foreign_keys=ON;PRAGMA journal_mode=DELETE;PRAGMA synchronous=FULL;");
 if(rc==SQLITE_OK&&(created||version==1||version==2)) {
  rc=sql(h->db,"BEGIN IMMEDIATE;");
  if(rc==SQLITE_OK&&created) rc=sql(h->db,base_schema);
  if(rc==SQLITE_OK&&(created||version==1)) rc=sql(h->db,migrate_v2);
  if(rc==SQLITE_OK) rc=sql(h->db,migrate_v3);
  if(rc==SQLITE_OK) rc=sql(h->db,"COMMIT;");
  if(rc!=SQLITE_OK) sql(h->db,"ROLLBACK;");
 }
 sqlite3_stmt *st=NULL;
 if(rc==SQLITE_OK) rc=sqlite3_prepare_v2(h->db,"SELECT lower(hex(randomblob(16)))",-1,&st,NULL);
 if(rc==SQLITE_OK) {
  if(sqlite3_step(st)==SQLITE_ROW) snprintf(h->id,sizeof(h->id),"%s",sqlite3_column_text(st,0));
  else rc=SQLITE_ERROR;
 }
 sqlite3_finalize(st);st=NULL;
 if(rc==SQLITE_OK) rc=sqlite3_prepare_v2(h->db,"INSERT INTO runs(run_id,started_utc,monotonic_start,mode,output_dir,submitted,dropped_full,rejected,committed,commits,last_rx_id) VALUES(?,?,?,?,?,0,0,0,0,0,0)",-1,&st,NULL);
 if(rc==SQLITE_OK) {
  T(st,1,h->id);T(st,2,utc);BIND(sqlite3_bind_double(st,3,start));T(st,4,mode);T(st,5,dir);
  rc=done(st,rc);
 }
 sqlite3_finalize(st);
 if(rc==SQLITE_OK) rc=sqlite3_prepare_v2(h->db,"INSERT INTO frames VALUES(?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?)",-1,&h->frame,NULL);
 if(rc==SQLITE_OK) rc=sqlite3_prepare_v2(h->db,"INSERT INTO points VALUES(?,?,?,?,?,?,?,?,?,?,?,?,?,?,?)",-1,&h->point,NULL);
 if(rc==SQLITE_OK) rc=sqlite3_prepare_v2(h->db,"INSERT INTO events VALUES(?,?,?,?,?,?,?,?)",-1,&h->event,NULL);
 if(rc==SQLITE_OK) rc=sqlite3_prepare_v2(h->db,"INSERT INTO diagnostics VALUES(?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?)",-1,&h->diagnostic,NULL);
 return rc;
}
static int write_item(History *h, const StoredRecord *r) {
 int rc=SQLITE_OK;sqlite3_stmt *f=h->frame,*p=h->point,*e=h->event;
 if(r->is_diagnostic) {
  sqlite3_stmt *d=h->diagnostic;const Diagnostic *v=&r->diagnostic;
  T(d,1,h->id);I(d,2,r->rx_id);T(d,3,r->gateway_run_id);I(d,4,r->session);T(d,5,r->utc);
  BIND(sqlite3_bind_double(d,6,r->elapsed_s));I(d,7,r->raw_start);I(d,8,r->raw_end);
  BIND(sqlite3_bind_blob(d,9,r->body,(int)r->body_len,SQLITE_TRANSIENT));
  I(d,10,v->d);
  I(d,11,v->up_ms);
  I(d,12,v->reset_captured);
  I(d,13,v->reset_raw);
  I(d,14,v->reset_reasons);
  I(d,15,v->fault_status);
  I(d,16,v->fault_type);
  I(d,17,v->frame_valid);
  I(d,18,v->frame_reject);
  I(d,19,v->pc);
  I(d,20,v->cfsr);
  I(d,21,v->hfsr);
  I(d,22,v->monitor_valid);
  I(d,23,v->health);
  I(d,24,v->active);
  I(d,25,v->observed);
  I(d,26,v->watchdog_initialized);
  I(d,27,v->watchdog);
  I(d,28,v->latched);
  return done(d,rc);
 }
 if(r->is_event) {
  T(e,1,h->id);I(e,2,r->event_id);T(e,3,r->utc);BIND(sqlite3_bind_double(e,4,r->elapsed_s));
  I(e,5,r->raw_start);I(e,6,r->session);T(e,7,r->kind);I(e,8,r->detail);
  return done(e,rc);
 }
 const Record *v=&r->record;
 T(f,1,h->id);I(f,2,r->rx_id);I(f,3,r->session);T(f,4,r->utc);
 BIND(sqlite3_bind_double(f,5,r->elapsed_s));I(f,6,r->raw_start);I(f,7,r->raw_end);
 BIND(sqlite3_bind_blob(f,8,r->body,(int)r->body_len,SQLITE_TRANSIENT));
 I(f,9,v->version);T(f,10,v->heartbeat?"HEARTBEAT":"EVENT");I(f,11,v->flags);
 I(f,12,v->gap);I(f,13,v->coal);I(f,14,v->snap);I(f,15,v->n);T(f,16,r->relation);
 rc=done(f,rc); if(rc!=SQLITE_OK) return rc;
 for(int i=0;i<2;++i) {
  const Point *point=&v->points[i];
  T(p,1,h->id);I(p,2,r->rx_id);I(p,3,i);T(p,4,"SHT30");T(p,5,i?"HUMIDITY":"TEMPERATURE");
  I(p,6,point->value);I(p,7,point->scale);T(p,8,i?"PERCENT_RH":"DEG_C");
  T(p,9,quality_names[point->quality]);T(p,10,status_names[point->status]);
  I(p,11,point->value_ms);I(p,12,point->status_ms);I(p,13,point->seq);
  T(p,14,!point->seq?"not_available":point->quality==Q_VALID?"current":"last_valid");
  I(p,15,point->quality==Q_VALID);rc=done(p,rc);if(rc!=SQLITE_OK) return rc;
 }
 return rc;
}
int history_batch(History *h, const StoredRecord *items, size_t count,
                  const StoredRecord *diagnostics, size_t diagnostic_count,
                  const StorageStats *after) {
 int rc=sql(h->db,"BEGIN IMMEDIATE;");
 for(size_t i=0;rc==SQLITE_OK&&i<count;++i) rc=write_item(h,&items[i]);
 for(size_t i=0;rc==SQLITE_OK&&i<diagnostic_count;++i) rc=write_item(h,&diagnostics[i]);
 sqlite3_stmt *st=NULL;
 if(rc==SQLITE_OK) rc=sqlite3_prepare_v2(h->db,"UPDATE runs SET committed=?,event_committed=?,worker_events_committed=?,commits=?,last_rx_id=?,mcu_committed=? WHERE run_id=?",-1,&st,NULL);
 if(rc==SQLITE_OK) {
  I(st,1,after->committed);I(st,2,after->event_committed);I(st,3,after->worker_events_committed);
  I(st,4,after->commits);I(st,5,after->last_rx_id);I(st,6,after->mcu_committed);T(st,7,h->id);rc=done(st,rc);
 }
 sqlite3_finalize(st);
 if(rc==SQLITE_OK) rc=sql(h->db,"COMMIT;");
 if(rc!=SQLITE_OK&&!sqlite3_get_autocommit(h->db)) sql(h->db,"ROLLBACK;");
 return rc;
}
/* Each transaction deletes at most 128 frames (256 points), 128 events and 128 empty runs.
   No VACUUM: freed pages are reusable; the file need not shrink. */
int history_prune(History *h, const char *cutoff, Pruned *pruned) {
 memset(pruned,0,sizeof(*pruned));int rc=sql(h->db,"BEGIN IMMEDIATE;");
 const char *commands[]={
  "DELETE FROM points WHERE (run_id,rx_id) IN (SELECT run_id,rx_id FROM frames WHERE utc<? ORDER BY utc LIMIT 128)",
  "DELETE FROM frames WHERE (run_id,rx_id) IN (SELECT run_id,rx_id FROM frames WHERE utc<? ORDER BY utc LIMIT 128)",
  "DELETE FROM events WHERE (run_id,event_id) IN (SELECT run_id,event_id FROM events WHERE utc<? ORDER BY utc LIMIT 128)",
  "DELETE FROM diagnostics WHERE (run_id,diagnostic_rx_id) IN (SELECT run_id,diagnostic_rx_id FROM diagnostics WHERE utc<? ORDER BY utc LIMIT 128)",
  "DELETE FROM runs WHERE run_id IN (SELECT r.run_id FROM runs r WHERE started_utc<? AND run_id!=? AND NOT EXISTS(SELECT 1 FROM frames f WHERE f.run_id=r.run_id) AND NOT EXISTS(SELECT 1 FROM events e WHERE e.run_id=r.run_id) AND NOT EXISTS(SELECT 1 FROM diagnostics d WHERE d.run_id=r.run_id) LIMIT 128)"};
 uint64_t *counts[]={&pruned->points,&pruned->frames,&pruned->events,&pruned->diagnostics,&pruned->runs};
 for(size_t i=0;rc==SQLITE_OK&&i<5;++i) {
  sqlite3_stmt *st=NULL;rc=sqlite3_prepare_v2(h->db,commands[i],-1,&st,NULL);
  if(rc==SQLITE_OK) {
   T(st,1,cutoff);if(i==4) T(st,2,h->id);rc=done(st,rc);
   if(rc==SQLITE_OK) *counts[i]=(uint64_t)sqlite3_changes(h->db);
  }
  sqlite3_finalize(st);
 }
 if(rc==SQLITE_OK) rc=sql(h->db,"COMMIT;");
 if(rc!=SQLITE_OK) {if(!sqlite3_get_autocommit(h->db)) sql(h->db,"ROLLBACK;");memset(pruned,0,sizeof(*pruned));}
 return rc;
}
int history_finish(History *h, int code, int sig, int complete, const StorageStats *s) {
 int rc=SQLITE_OK;sqlite3_stmt *st=NULL;
 rc=sqlite3_prepare_v2(h->db,"UPDATE runs SET ended_utc=strftime('%Y-%m-%dT%H:%M:%SZ','now'),exit_code=?,signal=?,complete=?,submitted=?,dropped_full=?,rejected=?,failed_records=?,event_submitted=?,event_dropped_full=?,event_rejected=?,event_failed=?,failures=?,recoveries=?,last_sqlite_error=?,mcu_submitted=?,mcu_dropped_full=?,mcu_rejected=?,mcu_committed=?,mcu_failed=? WHERE run_id=?",-1,&st,NULL);
 if(rc==SQLITE_OK) {
  I(st,1,code);I(st,2,sig);I(st,3,complete);I(st,4,s->submitted);I(st,5,s->dropped_full);I(st,6,s->rejected);
  I(st,7,s->failed_records);I(st,8,s->event_submitted);I(st,9,s->event_dropped_full);I(st,10,s->event_rejected);
  I(st,11,s->event_failed);I(st,12,s->failures);I(st,13,s->recoveries);I(st,14,s->last_error);I(st,15,s->mcu_submitted);I(st,16,s->mcu_dropped_full);I(st,17,s->mcu_rejected);I(st,18,s->mcu_committed);I(st,19,s->mcu_failed);T(st,20,h->id);rc=done(st,rc);
 }
 sqlite3_finalize(st);return rc;
}
int history_close(History *h) {
 sqlite3_finalize(h->frame);sqlite3_finalize(h->point);sqlite3_finalize(h->event);sqlite3_finalize(h->diagnostic);
 if(h->db&&!sqlite3_get_autocommit(h->db)) sql(h->db,"ROLLBACK;");
 return h->db?sqlite3_close(h->db):SQLITE_OK;
}
static double query_now(void) {
 struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);return t.tv_sec+t.tv_nsec/1e9;
}
static int query_progress(void *context) {return query_now()>=*(double *)context;}
void history_query(History *h, const Query *q, QueryResult *result) {
 char *json=NULL;size_t size=0;FILE *f=open_memstream(&json,&size);
 result->id=q->id;result->json=NULL;result->size=0;result->code=6;if(!f) return;
 double deadline=query_now()+.050;if(q->deadline<deadline) deadline=q->deadline;
 sqlite3_progress_handler(h->db,1000,query_progress,&deadline);sqlite3_busy_timeout(h->db,25);
 sqlite3_stmt *st=NULL;
 int rc=sqlite3_prepare_v2(h->db,
  "SELECT run_id,rx_id,utc,elapsed_s,session,raw_start,raw_end,body,sequence_relation FROM frames "
  "WHERE utc>=? AND utc<=? AND (utc,run_id,rx_id)>(?,?,?) ORDER BY utc,run_id,rx_id LIMIT ?",-1,&st,NULL);
 if(rc==SQLITE_OK) {
  T(st,1,q->from);T(st,2,q->until);T(st,3,q->after_utc);T(st,4,q->after_run);I(st,5,q->after_rx);I(st,6,q->limit+1);
 }
 fprintf(f,"{\"protocol\":1,\"code\":0,\"records\":[");
 unsigned count=0;int more=0;char cursor[128]="";
 while(rc==SQLITE_OK&&(rc=sqlite3_step(st))==SQLITE_ROW) {
  if(query_now()>=deadline) {rc=SQLITE_INTERRUPT;break;}
  if(count==q->limit) {more=1;rc=SQLITE_OK;break;}
  StoredRecord r={0};char run[33];
  snprintf(run,sizeof(run),"%s",sqlite3_column_text(st,0));r.rx_id=(uint64_t)sqlite3_column_int64(st,1);
  snprintf(r.utc,sizeof(r.utc),"%s",sqlite3_column_text(st,2));r.elapsed_s=sqlite3_column_double(st,3);
  r.session=(uint64_t)sqlite3_column_int64(st,4);r.raw_start=(uint64_t)sqlite3_column_int64(st,5);r.raw_end=(uint64_t)sqlite3_column_int64(st,6);
  if(protocol_parse(sqlite3_column_blob(st,7),(size_t)sqlite3_column_bytes(st,7),&r.record)!=P_OK) {rc=SQLITE_CORRUPT;break;}
  snprintf(r.relation,sizeof(r.relation),"%s",sqlite3_column_text(st,8));
  if(count) fputc(',',f);
  if(record_json(f,&r,run)||ftell(f)>QUERY_RESULT_MAX-512) {rc=SQLITE_TOOBIG;break;}
  snprintf(cursor,sizeof(cursor),"%s,%s,%"PRIu64,r.utc,run,r.rx_id);++count;rc=SQLITE_OK;
 }
 if(rc==SQLITE_DONE) rc=SQLITE_OK;
 sqlite3_finalize(st);sqlite3_progress_handler(h->db,0,NULL,NULL);sqlite3_busy_timeout(h->db,250);
 fprintf(f,"],\"count\":%u,\"more\":%s,\"next_cursor\":",count,more?"true":"false");
 if(count) fprintf(f,"\"%s\"",cursor);else fputs("null",f);
 fputs("}\n",f);int bad=ferror(f);if(fclose(f)) bad=1;
 if(rc!=SQLITE_OK||bad||!count) {
  free(json);const char *message=rc==SQLITE_OK&&!bad?"no_data":rc==SQLITE_INTERRUPT?"query_timeout":"query_failed";
  int code=rc==SQLITE_OK&&!bad?4:rc==SQLITE_INTERRUPT?5:6;
  char error[256];snprintf(error,sizeof(error),"{\"protocol\":1,\"code\":%d,\"error\":\"%s\",\"sqlite_error\":%d,\"records\":[],\"count\":0,\"more\":false,\"next_cursor\":null}\n",code,message,rc);
  json=strdup(error);size=json?strlen(json):0;
 }
 result->json=json;result->size=size;
 result->code=rc==SQLITE_OK&&!bad?(count?0:4):rc==SQLITE_INTERRUPT?5:6;
}
