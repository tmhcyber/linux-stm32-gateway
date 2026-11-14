#define _GNU_SOURCE
#include "ipc.h"
#include "query.h"
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/stat.h>
#include <sys/file.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>
#ifndef GATEWAY_VERSION
#define GATEWAY_VERSION "development"
#endif
#define CLIENTS 8
#define LINE_MAXIMUM 512
typedef struct {
 int fd, waiting, eof, close_after;
 uint64_t id;
 double deadline;
 char input[LINE_MAXIMUM+1];size_t used;
 char *reply;size_t size, sent;
} Client;
struct Ipc {
 int fd, lock;
 char path[sizeof(((struct sockaddr_un *)0)->sun_path)];
 dev_t dev;ino_t ino;
 uint64_t next_id, accepted, rejected, timed_out, requests, late_results;
 Client clients[CLIENTS];
};
static void drop(Client *c) {if(c->fd>=0) close(c->fd);free(c->reply);memset(c,0,sizeof(*c));c->fd=-1;}
static void error_reply(Client *c, int code, const char *error) {
 char json[256];snprintf(json,sizeof(json),"{\"protocol\":1,\"code\":%d,\"error\":\"%s\"}\n",code,error);
 c->reply=strdup(json);c->size=c->reply?strlen(c->reply):0;c->waiting=0;
 if(!c->reply) drop(c);
}
Ipc *ipc_open(const char *path) {
 Ipc *s=calloc(1,sizeof(*s));if(!s) return NULL;s->fd=s->lock=-1;
 for(int i=0;i<CLIENTS;++i) s->clients[i].fd=-1;
 if(strlen(path)>=sizeof(s->path)||path[0]!='/') {errno=ENAMETOOLONG;goto fail;}
 strcpy(s->path,path);char directory[sizeof(s->path)];strcpy(directory,path);
 char *slash=strrchr(directory,'/');if(slash==directory) {errno=EACCES;goto fail;}*slash=0;
 struct stat st;
 if(lstat(directory,&st)||!S_ISDIR(st.st_mode)||st.st_uid!=geteuid()||(st.st_mode&0022)) {errno=EACCES;goto fail;}
 char lockpath[128];snprintf(lockpath,sizeof(lockpath),"%s.lock",path);
 s->lock=open(lockpath,O_CREAT|O_RDWR|O_CLOEXEC|O_NOFOLLOW,0600);
 if(s->lock<0) goto fail;
 if(fstat(s->lock,&st)||!S_ISREG(st.st_mode)||st.st_uid!=geteuid()||st.st_nlink!=1||(st.st_mode&0077)||flock(s->lock,LOCK_EX|LOCK_NB)) {errno=EADDRINUSE;goto fail;}
 struct sockaddr_un address={.sun_family=AF_UNIX};strcpy(address.sun_path,path);
 if(!lstat(path,&st)) {
  if(!S_ISSOCK(st.st_mode)||st.st_uid!=geteuid()) {errno=EEXIST;goto fail;}
  int probe=socket(AF_UNIX,SOCK_STREAM|SOCK_CLOEXEC|SOCK_NONBLOCK,0);if(probe<0) goto fail;
  int rc=connect(probe,(struct sockaddr *)&address,sizeof(address)),e=errno;close(probe);
  if(!rc||e!=ECONNREFUSED) {errno=EADDRINUSE;goto fail;}
  struct stat again;
  if(lstat(path,&again)||again.st_dev!=st.st_dev||again.st_ino!=st.st_ino||!S_ISSOCK(again.st_mode)) {errno=EEXIST;goto fail;}
  if(unlink(path)) goto fail;
 } else if(errno!=ENOENT) goto fail;
 s->fd=socket(AF_UNIX,SOCK_STREAM|SOCK_CLOEXEC|SOCK_NONBLOCK,0);if(s->fd<0) goto fail;
 mode_t mask=umask(0077);int rc=bind(s->fd,(struct sockaddr *)&address,sizeof(address));umask(mask);
 if(rc) goto fail;
 if(lstat(path,&st)) goto fail;
 s->dev=st.st_dev;s->ino=st.st_ino;
 if(chmod(path,0600)) goto fail;
 if(listen(s->fd,CLIENTS)) goto fail;
 return s;
fail: {int e=errno;ipc_close(s);errno=e;return NULL;}
}
size_t ipc_pollfds(Ipc *s, Storage *storage, struct pollfd *fds) {
 if(!s) return 0;
 fds[0]=(struct pollfd){.fd=s->fd,.events=POLLIN};
 fds[1]=(struct pollfd){.fd=storage?storage_query_fd(storage):-1,.events=POLLIN};
 for(int i=0;i<CLIENTS;++i) {
  Client *c=&s->clients[i];
  fds[i+2]=(struct pollfd){.fd=c->fd,.events=c->reply?POLLOUT:c->waiting?0:POLLIN};
 }
 return IPC_POLL_MAX;
}
static void request(Ipc *s, Client *c, Output *o, const char *line) {
 ++s->requests;
 if(!strcmp(line,"1 status")) {
  FILE *f=open_memstream(&c->reply,&c->size);if(!f) {drop(c);return;}
  double age=monotonic_now()-o->last_valid;char utc[40];utc_now(utc,sizeof(utc));
  fprintf(f,"{\"protocol\":1,\"code\":0,\"gateway_run_id\":\"%s\",\"version\":\"%s\",\"pid\":%ld,\"uid\":%ld,\"started_utc\":\"%s\",\"observed_utc\":\"%s\",\"uptime_s\":%.6f,\"serial\":\"%s\",\"legal_frame_age_s\":",
   o->gateway_run_id,GATEWAY_VERSION,(long)getpid(),(long)geteuid(),o->started_utc,utc,monotonic_now()-o->start,
   o->port_open<0?"replay":!o->port_open?"disconnected":o->silent?"silent":o->valid?"receiving":"waiting");
  if(o->valid) fprintf(f,"%.6f",age);else fputs("null",f);
  const Point *p=&o->latest.record.points[0];
  uint64_t errors=o->framer.overlong;for(int i=1;i<P_COUNT;++i) errors+=o->errors[i];
  fprintf(f,",\"legal_frames\":%"PRIu64",\"parse_framing_errors\":%"PRIu64",\"open_errors\":%"PRIu64",\"link_errors\":%"PRIu64",\"source_quality\":\"%s\",\"source_status\":\"%s\",\"sample_seq\":%u,\"value_ms\":%u,\"status_ms\":%u,\"sample_relations\":{\"repeat\":%"PRIu64",\"next\":%"PRIu64",\"jump\":%"PRIu64",\"backward\":%"PRIu64"},\"storage\":",
   o->valid,errors,o->open_errors,o->link_errors,quality_names[p->quality],status_names[p->status],p->seq,p->value_ms,p->status_ms,o->seq_repeat,o->seq_next,o->seq_jump,o->seq_backward);
  if(o->storage) {StorageStats stats;storage_snapshot(o->storage,&stats);storage_write_stats(f,&stats);}else fputs("null",f);
  fprintf(f,",\"diagnostic\":{\"received\":%"PRIu64",\"age_s\":",o->diagnostic_received);
  if(o->diagnostic_received) fprintf(f,"%.6f",monotonic_now()-o->last_diagnostic);else fputs("null",f);
  fprintf(f,",\"transport_fresh\":%s,\"latest\":",o->diagnostic_received&&o->port_open>0&&monotonic_now()-o->last_diagnostic<15?"true":"false");
  if(o->diagnostic_received) diagnostic_record_json(f,&o->latest_diagnostic,o->db_enabled?o->storage_stats.run_id:o->gateway_run_id);else fputs("null",f);
  fputc('}',f);
  fputs(",\"mqtt\":",f);if(o->mqtt) {MqttStats stats;mqtt_snapshot(o->mqtt,&stats);mqtt_write_stats(f,&stats);}else fputs("null",f);
  fprintf(f,",\"ipc\":{\"accepted\":%"PRIu64",\"rejected\":%"PRIu64",\"timed_out\":%"PRIu64",\"requests\":%"PRIu64",\"late_results\":%"PRIu64"}}\n",s->accepted,s->rejected,s->timed_out,s->requests,s->late_results);
  if(fclose(f)) drop(c);
 } else if(!strcmp(line,"1 latest")) {
  if(!o->valid) {error_reply(c,4,"no_data");return;}
  FILE *f=open_memstream(&c->reply,&c->size);if(!f) {drop(c);return;}
  fprintf(f,"{\"protocol\":1,\"code\":0,\"gateway_run_id\":\"%s\",\"legal_frame_age_s\":%.6f,\"transport_fresh\":%s,\"record\":",
   o->gateway_run_id,monotonic_now()-o->last_valid,o->port_open>0&&monotonic_now()-o->last_valid<15?"true":"false");
  record_json(f,&o->latest,o->db_enabled?o->storage_stats.run_id:o->gateway_run_id);fputs("}\n",f);if(fclose(f)) drop(c);
 } else {
  Query q;
  if(query_parse(line,&q)) {error_reply(c,2,"bad_request");return;}
  if(!o->storage) {error_reply(c,6,"history_unavailable");return;}
  q.id=c->id;q.deadline=c->deadline;
  if(!storage_query(o->storage,&q)) {error_reply(c,6,"query_queue_full");return;}c->waiting=1;
 }
}
static void consume(Ipc *s, Client *c, Output *o) {
 if(c->fd<0||c->reply||c->waiting) return;
 char *end=memchr(c->input,'\n',c->used);
 if(end) {
  size_t length=(size_t)(end-c->input);char line[LINE_MAXIMUM+1];memcpy(line,c->input,length);line[length]=0;
  if(memchr(line,0,length)||memchr(line,'\r',length)) {error_reply(c,2,"bad_request");c->close_after=1;return;}
  memmove(c->input,end+1,c->used-length-1);c->used-=length+1;
  c->id=++s->next_id;request(s,c,o,line);
 } else if(c->used==sizeof(c->input)) {error_reply(c,2,"request_too_long");c->close_after=1;}
 else if(c->eof) {if(c->used) {error_reply(c,2,"incomplete_request");c->close_after=1;}else drop(c);}
}
void ipc_dispatch(Ipc *s, Output *o, struct pollfd *fds) {
 if(!s) return;
 if(o->storage&&fds[1].revents&POLLIN) {
  uint64_t value;while(read(fds[1].fd,&value,sizeof(value))<0&&errno==EINTR) {}
  QueryResult r;
  while(storage_query_result(o->storage,&r)) {
   Client *found=NULL;for(int i=0;i<CLIENTS;++i) if(s->clients[i].fd>=0&&s->clients[i].waiting&&s->clients[i].id==r.id) found=&s->clients[i];
   if(found) {found->waiting=0;found->reply=r.json;found->size=r.size;if(!r.json) error_reply(found,6,"allocation_failed");}
   else {free(r.json);++s->late_results;}
  }
 }
 /* Only process descriptors present in this poll generation; accept afterward. */
 for(int i=0;i<CLIENTS;++i) {
  Client *c=&s->clients[i];short flags=fds[i+2].revents;
  if(c->fd<0) continue;
  if(monotonic_now()>=c->deadline) {++s->timed_out;drop(c);continue;}
  if(flags&(POLLERR|POLLNVAL|POLLHUP)) {drop(c);continue;}
  if(!c->reply&&!c->waiting&&(flags&(POLLIN|POLLHUP))) {
   ssize_t n=recv(c->fd,c->input+c->used,sizeof(c->input)-c->used,0);
   if(n>0) c->used+=(size_t)n;
   else if(!n) c->eof=1;
   else if(errno!=EAGAIN&&errno!=EWOULDBLOCK&&errno!=EINTR) {drop(c);continue;}
  }
  consume(s,c,o);
  if(c->fd>=0&&c->reply&&(flags&POLLOUT)) {
   size_t n=c->size-c->sent;if(n>65536) n=65536;
   ssize_t sent=send(c->fd,c->reply+c->sent,n,MSG_NOSIGNAL);
   if(sent>0) c->sent+=(size_t)sent;
   else if(sent<0&&errno!=EAGAIN&&errno!=EWOULDBLOCK&&errno!=EINTR) {drop(c);continue;}
   if(c->sent==c->size) {
    free(c->reply);c->reply=NULL;c->size=c->sent=0;
    if(c->close_after) drop(c);
    else {c->deadline=monotonic_now()+5;consume(s,c,o);}
   }
  }
 }
 if(fds[0].revents&POLLIN) for(int attempt=0;attempt<CLIENTS;++attempt) {
  int fd=accept4(s->fd,NULL,NULL,SOCK_NONBLOCK|SOCK_CLOEXEC);if(fd<0) {if(errno==EINTR) continue;break;}
  Client *c=NULL;for(int i=0;i<CLIENTS;++i) if(s->clients[i].fd<0) {c=&s->clients[i];break;}
  if(!c) {++s->rejected;close(fd);}else {c->fd=fd;c->deadline=monotonic_now()+5;++s->accepted;}
 }
}
void ipc_close(Ipc *s) {
 if(!s) return;
 for(int i=0;i<CLIENTS;++i) drop(&s->clients[i]);
 if(s->fd>=0) close(s->fd);
 struct stat st;if(s->ino&&!lstat(s->path,&st)&&st.st_dev==s->dev&&st.st_ino==s->ino) unlink(s->path);
 if(s->lock>=0) close(s->lock);
 free(s);
}
