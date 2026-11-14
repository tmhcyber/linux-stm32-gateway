#define _POSIX_C_SOURCE 200809L
#include "history.h"
#include <assert.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdio.h>
static void sql(sqlite3 *db, const char *text) {assert(sqlite3_exec(db,text,NULL,NULL,NULL)==SQLITE_OK);}
static int scalar(sqlite3 *db, const char *text) {
 sqlite3_stmt *st=NULL;assert(sqlite3_prepare_v2(db,text,-1,&st,NULL)==SQLITE_OK);
 assert(sqlite3_step(st)==SQLITE_ROW);int value=sqlite3_column_int(st,0);sqlite3_finalize(st);return value;
}
static void add_frame(sqlite3 *db, const char *id, unsigned rx, const char *utc) {
 char text[1024];snprintf(text,sizeof(text),
  "INSERT INTO frames VALUES('%s',%u,0,'%s',0,0,2,X'31',1,'EVENT',22,0,0,4,2,'next');"
  "INSERT INTO points VALUES('%s',%u,0,'SHT30','TEMPERATURE',25000,-3,'DEG_C','VALID','OK',1000,1000,1,'current',1);"
  "INSERT INTO points VALUES('%s',%u,1,'SHT30','HUMIDITY',50000,-3,'PERCENT_RH','VALID','OK',1000,1000,1,'current',1);",
  id,rx,utc,id,rx,id,rx);sql(db,text);
}
int main(void) {
 char directory[]="/tmp/gateway-history-XXXXXX";assert(mkdtemp(directory));
 char path[256];snprintf(path,sizeof(path),"%s/test.sqlite",directory);
 History h;assert(history_open(&h,path,"2000-01-01T00:00:00Z",0,"test",directory)==SQLITE_OK);
 assert(scalar(h.db,"PRAGMA synchronous")==2);assert(scalar(h.db,"PRAGMA foreign_keys")==1);
 sql(h.db,"INSERT INTO runs(run_id,started_utc,monotonic_start,mode,output_dir) VALUES('old','2000-01-01T00:00:00Z',0,'test','test')");
 for(unsigned i=1;i<=130;++i) {
  add_frame(h.db,"old",i,"2000-01-01T00:00:00Z");
  char text[512];snprintf(text,sizeof(text),"INSERT INTO events VALUES('old',%u,'2000-01-01T00:00:00Z',0,0,0,'OLD',0);"
   "INSERT INTO runs(run_id,started_utc,monotonic_start,mode,output_dir) VALUES('empty%u','2000-01-01T00:00:00Z',0,'test','test')",i,i);sql(h.db,text);
 }
 /* Exact UTC cutoff is kept; newer is kept. Current run survives even with an old start. */
 add_frame(h.db,h.id,1,"2026-11-05T00:00:00Z");add_frame(h.db,h.id,2,"2026-11-05T00:00:01Z");
 sqlite3 *lock=NULL;assert(sqlite3_open(path,&lock)==SQLITE_OK);sql(lock,"BEGIN IMMEDIATE;");
 Pruned p;assert((history_prune(&h,"2026-11-05T00:00:00Z",&p)&255)==SQLITE_BUSY);
 assert(!p.frames&&!p.points&&!p.events&&!p.runs);assert(scalar(h.db,"SELECT count(*) FROM frames")==132);
 sql(lock,"ROLLBACK;");assert(sqlite3_close(lock)==SQLITE_OK);
 assert(history_prune(&h,"2026-11-05T00:00:00Z",&p)==SQLITE_OK);
 assert(p.frames==128&&p.points==256&&p.events==128&&p.runs==128);
 assert(history_prune(&h,"2026-11-05T00:00:00Z",&p)==SQLITE_OK);
 assert(p.frames==2&&p.points==4&&p.events==2&&p.runs==3);
 assert(history_prune(&h,"2026-11-05T00:00:00Z",&p)==SQLITE_OK);
 assert(!p.frames&&!p.points&&!p.events&&!p.runs);
 assert(scalar(h.db,"SELECT count(*) FROM frames")==2);
 assert(scalar(h.db,"SELECT count(*) FROM points")==4);
 assert(scalar(h.db,"SELECT count(*) FROM runs")==1);
 sql(h.db,"INSERT INTO runs(run_id,started_utc,monotonic_start,mode,output_dir) VALUES('diag-old','2000-01-01T00:00:00Z',0,'test','test'),('diag-keep','2000-01-01T00:00:00Z',0,'test','test')");
 for(unsigned i=1;i<=130;++i) {
  char text[1024];snprintf(text,sizeof(text),"INSERT INTO diagnostics VALUES('diag-old',%u,'gateway',0,'2000-01-01T00:00:00Z',0,0,2,X'31',1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0)",i);sql(h.db,text);
 }
 sql(h.db,"INSERT INTO diagnostics VALUES('diag-keep',1,'gateway',0,'2026-11-05T00:00:00Z',0,0,2,X'31',1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0)");
 assert(history_prune(&h,"2026-11-05T00:00:00Z",&p)==SQLITE_OK);
 assert(p.diagnostics==128&&!p.runs);
 assert(scalar(h.db,"SELECT count(*) FROM runs")==3);
 assert(history_prune(&h,"2026-11-05T00:00:00Z",&p)==SQLITE_OK);
 assert(p.diagnostics==2&&p.runs==1);
 assert(scalar(h.db,"SELECT count(*) FROM diagnostics")==1);
 assert(scalar(h.db,"SELECT count(*) FROM runs")==2);
 assert(scalar(h.db,"SELECT count(*) FROM pragma_foreign_key_check")==0);
 assert(history_close(&h)==SQLITE_OK);assert(!unlink(path));assert(!rmdir(directory));
 puts("history retention: telemetry/events/diagnostics 128-row batches, atomic busy failure, exact UTC cutoff, foreign keys/current-run preservation passed");
 return 0;
}
