/* Test-only SQLite VFS wrapper. Production gateway does not link this file. */
#define _POSIX_C_SOURCE 200809L
#include "history.h"
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <stddef.h>
#include <errno.h>
static long option(const char *key, long fallback) {
 const char *path=getenv("GATEWAY_STORAGE_TEST_CONTROL");if(!path) return fallback;
 FILE *f=fopen(path,"r");if(!f) return fallback;
 char line[128],name[80];long value;
 while(fgets(line,sizeof(line),f)) {
  if(sscanf(line,"%79[^=]=%ld",name,&value)==2&&!strcmp(name,key)) {fclose(f);return value;}
 }
 fclose(f);return fallback;
}
typedef struct {sqlite3_file base;sqlite3_file *real;max_align_t alignment;} Wrapped;
static sqlite3_vfs *parent;
static int close_file(sqlite3_file *f) {Wrapped *w=(Wrapped *)f;return w->real->pMethods->xClose(w->real);}
static int read_file(sqlite3_file *f, void *p, int n, sqlite3_int64 off) {Wrapped *w=(Wrapped *)f;return w->real->pMethods->xRead(w->real,p,n,off);}
static int write_file(sqlite3_file *f, const void *p, int n, sqlite3_int64 off) {
 Wrapped *w=(Wrapped *)f;long error=option("write_error",0);
 return error?(int)error:w->real->pMethods->xWrite(w->real,p,n,off);
}
static int truncate_file(sqlite3_file *f, sqlite3_int64 n) {Wrapped *w=(Wrapped *)f;return w->real->pMethods->xTruncate(w->real,n);}
static int sync_file(sqlite3_file *f, int flags) {
 Wrapped *w=(Wrapped *)f;long pause=option("pause_sync_ms",0);
 if(pause>0&&pause<=2000) {
  const char *control=getenv("GATEWAY_STORAGE_TEST_CONTROL");char path[4096];
  if(control&&snprintf(path,sizeof(path),"%s.sync",control)<(int)sizeof(path)) {
   FILE *mark=fopen(path,"w");if(mark) {fputs("SQLite xSync reached during transaction\n",mark);fclose(mark);}
  }
  struct timespec t={.tv_sec=pause/1000,.tv_nsec=(pause%1000)*1000000};
  while(nanosleep(&t,&t)&&errno==EINTR) {}
 }
 return w->real->pMethods->xSync(w->real,flags);
}
static int size_file(sqlite3_file *f, sqlite3_int64 *n) {Wrapped *w=(Wrapped *)f;return w->real->pMethods->xFileSize(w->real,n);}
static int lock_file(sqlite3_file *f, int kind) {Wrapped *w=(Wrapped *)f;return w->real->pMethods->xLock(w->real,kind);}
static int unlock_file(sqlite3_file *f, int kind) {Wrapped *w=(Wrapped *)f;return w->real->pMethods->xUnlock(w->real,kind);}
static int check_file(sqlite3_file *f, int *v) {Wrapped *w=(Wrapped *)f;return w->real->pMethods->xCheckReservedLock(w->real,v);}
static int control_file(sqlite3_file *f, int op, void *v) {Wrapped *w=(Wrapped *)f;return w->real->pMethods->xFileControl(w->real,op,v);}
static int sector_file(sqlite3_file *f) {Wrapped *w=(Wrapped *)f;return w->real->pMethods->xSectorSize(w->real);}
static int device_file(sqlite3_file *f) {Wrapped *w=(Wrapped *)f;return w->real->pMethods->xDeviceCharacteristics(w->real);}
static const sqlite3_io_methods methods={.iVersion=1,.xClose=close_file,.xRead=read_file,.xWrite=write_file,
 .xTruncate=truncate_file,.xSync=sync_file,.xFileSize=size_file,.xLock=lock_file,.xUnlock=unlock_file,
 .xCheckReservedLock=check_file,.xFileControl=control_file,.xSectorSize=sector_file,.xDeviceCharacteristics=device_file};
static int open_file(sqlite3_vfs *vfs, const char *name, sqlite3_file *file, int flags, int *out) {
 (void)vfs;Wrapped *w=(Wrapped *)file;memset(w,0,sizeof(*w));w->real=(sqlite3_file *)(w+1);
 int rc=parent->xOpen(parent,name,w->real,flags,out);if(rc==SQLITE_OK) w->base.pMethods=&methods;return rc;
}
static int delete_file(sqlite3_vfs *v, const char *p, int sync) {(void)v;return parent->xDelete(parent,p,sync);}
static int access_file(sqlite3_vfs *v, const char *p, int flags, int *r) {(void)v;return parent->xAccess(parent,p,flags,r);}
static int full_path(sqlite3_vfs *v, const char *p, int n, char *r) {(void)v;return parent->xFullPathname(parent,p,n,r);}
void storage_test_vfs_init(void) {
 static sqlite3_vfs vfs;parent=sqlite3_vfs_find(NULL);vfs=*parent;
 vfs.zName="stage3-fault-vfs";vfs.szOsFile=(int)sizeof(Wrapped)+parent->szOsFile;
 vfs.xOpen=open_file;vfs.xDelete=delete_file;vfs.xAccess=access_file;vfs.xFullPathname=full_path;
 sqlite3_vfs_register(&vfs,1);
}
int storage_test_before_batch(sqlite3 *db) {
 long delay=option("delay_ms",0);if(delay>0&&delay<=100) {
  struct timespec t={.tv_nsec=delay*1000000};while(nanosleep(&t,&t)&&errno==EINTR) {}
 }
 long pages=option("max_pages",0);
 if(pages>0&&pages<=1000000) {
  char text[80];snprintf(text,sizeof(text),"PRAGMA max_page_count=%ld",pages);
  int rc=sqlite3_exec(db,text,NULL,NULL,NULL);if(rc!=SQLITE_OK) return rc;
 }
 return SQLITE_OK;
}
double storage_test_retention_interval(void) {
 long ms=option("retention_ms",3600000);return ms>=10&&ms<=3600000?ms/1000.:3600.;
}
