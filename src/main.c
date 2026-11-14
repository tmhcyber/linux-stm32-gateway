#define _POSIX_C_SOURCE 200809L
#include "output.h"
#include "serial.h"
#include "ipc.h"
#include <signal.h>
#include <unistd.h>
#include <fcntl.h>
#include <poll.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
static volatile sig_atomic_t stopped;
static void on_signal(int sig) { stopped=sig; }
static void usage(void) {
 fprintf(stderr,"gateway (--port DEVICE (--duration SECONDS | --continuous) [--discard-buffered] | --replay FILE --boundary aligned|stream) --output NEW_DIR [--socket PATH] [--journal] [--db PATH] [--mqtt [--mqtt-port PORT] [--gateway-id ID] [--device-id ID]]\n");
}
int main(int argc, char **argv) {
 const char *port=NULL,*replay=NULL,*dir=NULL,*boundary=NULL,*db=NULL; double duration=0; int discard=0;
 int mqtt_enabled=0,mqtt_port=1883,mqtt_options=0;
 const char *socket_path=NULL;int continuous=0,journal=0;
 const char *gateway="gw-01",*device="stm32-01";
 for(int i=1;i<argc;++i) {
  if(!strcmp(argv[i],"--discard-buffered")) {discard=1;continue;}
  if(!strcmp(argv[i],"--mqtt")) {mqtt_enabled=1;continue;}
  if(!strcmp(argv[i],"--continuous")) {continuous=1;continue;}
  if(!strcmp(argv[i],"--journal")) {journal=1;continue;}
  if(i+1>=argc) {usage();return 2;}
  const char *key=argv[i],*v=argv[++i];
  if(!strcmp(key,"--port")) port=v;
  else if(!strcmp(key,"--replay")) replay=v;
  else if(!strcmp(key,"--output")) dir=v;
  else if(!strcmp(key,"--boundary")) boundary=v;
  else if(!strcmp(key,"--db")) db=v;
  else if(!strcmp(key,"--socket")) socket_path=v;
  else if(!strcmp(key,"--gateway-id")) {gateway=v;mqtt_options=1;}
  else if(!strcmp(key,"--device-id")) {device=v;mqtt_options=1;}
  else if(!strcmp(key,"--mqtt-port")) {
   mqtt_options=1;char *end;errno=0;long number=strtol(v,&end,10);
   if(errno||!*v||*end||number<1||number>65535) {usage();return 2;}
   mqtt_port=(int)number;
  }
  else if(!strcmp(key,"--duration")) {
   char *end; errno=0; duration=strtod(v,&end);
   if(errno||!*v||*end||!isfinite(duration)||duration<=0||duration>604800) {usage();return 2;}
  } else {usage();return 2;}
 }
 if(!dir||!!port==!!replay||(port&&((!duration&&!continuous)||(duration&&continuous)||boundary))||
    (replay&&(!boundary||duration||discard||continuous))||
    (boundary&&strcmp(boundary,"aligned")&&strcmp(boundary,"stream"))||
    (mqtt_options&&!mqtt_enabled)||!mqtt_identifier_valid(gateway)||!mqtt_identifier_valid(device)) {usage();return 2;}
 struct sigaction sa={0}; sa.sa_handler=on_signal; sigemptyset(&sa.sa_mask);
 sigaction(SIGINT,&sa,NULL); sigaction(SIGTERM,&sa,NULL);
 Output o; if(output_open(&o,dir,replay&&!strcmp(boundary,"aligned"))<0) {perror("output");return 1;}
 o.journal=journal;
 Ipc *ipc=socket_path?ipc_open(socket_path):NULL;
 if(replay) o.port_open=-1;
 int complete=0, fatal=0; unsigned char data[4096];
 if(socket_path&&!ipc) {perror("socket");fatal=1;}
 int mqtt_initialized=0;
 if(mqtt_enabled&&!fatal) {
  o.mqtt_enabled=1;
  if(mqtt_library_init()) fatal=1;else mqtt_initialized=1;
 }
 if(db&&!fatal) {
  o.db_enabled=1;
  o.storage=storage_start(db,o.started_utc,o.start,replay?boundary:"serial",dir,&o.storage_stats);
  if(!o.storage) fatal=1;
 }
 if(mqtt_enabled&&!fatal) {
  o.mqtt=mqtt_start(mqtt_port,gateway,device,dir,o.gateway_run_id);
  if(!o.mqtt) fatal=1;
 }
 output_event(&o,"START",0);
 if(o.db_enabled&&!o.storage) output_event(&o,"STORAGE_START_ERROR",o.storage_stats.error);
 output_storage_status(&o,1);
 if(o.mqtt_enabled&&!o.mqtt) output_event(&o,"MQTT_START_ERROR",0);
 output_mqtt_status(&o,1);
 if(fatal) { /* Output owns cleanup even when storage initialization failed. */ }
 else if(replay) {
  int fd=open(replay,O_RDONLY|O_CLOEXEC);
  if(fd<0) {output_event(&o,"REPLAY_OPEN_ERROR",errno);fatal=1;}
  else {
   while(!stopped&&!o.io_error) {
    struct pollfd local[IPC_POLL_MAX];size_t nfds=ipc_pollfds(ipc,o.storage,local);
    if(nfds) {poll(local,nfds,0);ipc_dispatch(ipc,&o,local);}
    ssize_t n=read(fd,data,sizeof(data));
    if(n>0) output_bytes(&o,data,(size_t)n);
    else if(n==0) {complete=1;break;}
    else if(errno!=EINTR) {output_event(&o,"REPLAY_READ_ERROR",errno);fatal=1;break;}
   }
   if(close(fd)<0) {output_event(&o,"REPLAY_CLOSE_ERROR",errno);fatal=1;}
  }
 } else {
  Serial serial={.fd=-1}; double retry_at=o.start; unsigned backoff=1;
  while(!stopped&&!o.io_error) {
   double now=monotonic_now(); if(!continuous&&now-o.start>=duration) {complete=1;break;}
   output_storage_status(&o,0);
   output_mqtt_status(&o,0);
   output_silence(&o,now);
   if(serial.fd<0&&now>=retry_at) {
    if(serial_open(&serial,port,discard&&o.opens==0)<0) {
     ++o.open_errors; output_event(&o,"OPEN_ERROR",errno);
     retry_at=now+backoff; output_event(&o,"RETRY_SECONDS",(int)backoff);
     backoff=backoff<16?backoff*2:30;
    } else {
     ++o.opens;o.port_open=1; framer_reconnect(&o.framer); output_event(&o,"OPEN",0);
    }
   }
   struct pollfd pfds[1+IPC_POLL_MAX];pfds[0]=(struct pollfd){.fd=serial.fd,.events=POLLIN};
   size_t nfds=1+ipc_pollfds(ipc,o.storage,pfds+1);
   double wait=continuous?.2:o.start+duration-monotonic_now();
   if(serial.fd<0&&retry_at-now<wait) wait=retry_at-now;
   int ms=wait<0?0:wait>.2?200:(int)(wait*1000)+1;
   int ready=poll(pfds,nfds,ms);
   if(ready<0) {
    if(errno==EINTR) continue;
    output_event(&o,"POLL_ERROR",errno); fatal=1; break;
   }
   ipc_dispatch(ipc,&o,pfds+1);
   if(!ready) continue;
   struct pollfd pfd=pfds[0];
   int broken=0; size_t drained=0;
   if(pfd.revents&(POLLIN|POLLHUP|POLLERR)) {
    while(drained<262144&&!stopped&&!o.io_error) {
     ssize_t n=read(serial.fd,data,sizeof(data));
     if(n>0) {
      uint64_t previous_valid=o.valid;
      /* poll/read can resume after the silence deadline; observe it before a valid frame resets age. */
      output_silence(&o,monotonic_now());
      output_bytes(&o,data,(size_t)n); drained+=(size_t)n;
      if(o.valid>previous_valid) backoff=1;
     } else if(n==0) break;
     else if(errno==EINTR) continue;
     else if(errno==EAGAIN||errno==EWOULDBLOCK) break;
     else {output_event(&o,"READ_ERROR",errno);broken=1;break;}
    }
   }
   if(pfd.revents&(POLLHUP|POLLERR|POLLNVAL)) {output_event(&o,"LINK_POLL_FLAGS",pfd.revents);broken=1;}
   if(broken) {
    o.port_open=0;
    ++o.link_errors;
    if(serial_close(&serial)<0) output_event(&o,"CLOSE_ERROR",errno);
    framer_reconnect(&o.framer); retry_at=monotonic_now()+backoff;
    output_event(&o,"RETRY_SECONDS",(int)backoff); backoff=backoff<16?backoff*2:30;
   }
  }
  if(serial_close(&serial)<0) {output_event(&o,"CLOSE_ERROR",errno);fatal=1;}
  o.port_open=0;
 }
 uint64_t errors=o.framer.overlong+o.open_errors+o.link_errors;
 for(int i=1;i<P_COUNT;++i) errors+=o.errors[i];
 if(o.storage) {
  storage_snapshot(o.storage,&o.storage_stats);
  if(storage_has_errors(&o.storage_stats)) fatal=1;
 }
 int code=stopped?128+stopped:fatal||o.io_error||errors?1:!(o.valid||o.diagnostic_received)?2:0;
 ipc_close(ipc);
 code=output_finish(&o,dir,code,stopped,complete,duration,replay?boundary:"serial",discard);
 if(mqtt_initialized) mqtt_library_cleanup();
 return code;
}
