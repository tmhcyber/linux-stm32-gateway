#define _GNU_SOURCE
#include "query.h"
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include <poll.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
static double now(void) {struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);return t.tv_sec+t.tv_nsec/1e9;}
static int fail(int code,const char *error) {fprintf(stderr,"{\"protocol\":1,\"code\":%d,\"error\":\"%s\"}\n",code,error);return code;}
static void help(void) {
 puts("gatewayctl [--socket PATH] [--timeout SECONDS] status|latest|history\n"
      "history: --from YYYY-MM-DDTHH:MM:SSZ --until YYYY-MM-DDTHH:MM:SSZ [--limit 1..1000] [--cursor UTC,RUN_ID,RX_ID]\n"
      "Defaults: /run/stm32-gateway/control.sock, timeout=6, limit=100\n"
      "Exit: 0 success/help; 2 arguments/request; 3 unavailable; 4 no data; 5 timeout; 6 server busy/failure; 7 protocol/I/O");
}
static int wait_fd(int fd,short events,double deadline) {
 for(;;) {
  double remaining=deadline-now();if(remaining<=0) return 0;
  struct pollfd p={.fd=fd,.events=events};int rc=poll(&p,1,(int)(remaining*1000)+1);
  if(rc<0&&errno==EINTR) continue;
  return rc;
 }
}
int main(int argc,char **argv) {
 const char *path="/run/stm32-gateway/control.sock",*command=NULL,*from=NULL,*until=NULL,*limit="100",*cursor="-";
 double timeout=6;int history_options=0;
 for(int i=1;i<argc;++i) {
  const char *key=argv[i];
  if(!strcmp(key,"--help")) {help();return 0;}
  if(!strcmp(key,"status")||!strcmp(key,"latest")||!strcmp(key,"history")) {
   if(command) return fail(2,"arguments");
   command=key;continue;
  }
  if(i+1==argc) return fail(2,"arguments");
  const char *value=argv[++i];
  if(!strcmp(key,"--socket")) path=value;
  else if(!strcmp(key,"--timeout")) {
   char *end;errno=0;timeout=strtod(value,&end);
   if(errno||!*value||*end||!(timeout>=.1&&timeout<=30)) return fail(2,"arguments");
  } else {
   history_options=1;
   if(!strcmp(key,"--from")) from=value;
   else if(!strcmp(key,"--until")) until=value;
   else if(!strcmp(key,"--limit")) limit=value;
   else if(!strcmp(key,"--cursor")) cursor=value;
   else return fail(2,"arguments");
  }
 }
 if(!command||strlen(path)>=sizeof(((struct sockaddr_un *)0)->sun_path)) return fail(2,"arguments");
 char request[513];int length;
 if(!strcmp(command,"history")) {
  if(!from||!until) return fail(2,"arguments");
  length=snprintf(request,sizeof(request),"1 history %s %s %s %s",from,until,limit,cursor);
  Query q;if(length<0||length>510||query_parse(request,&q)) return fail(2,"arguments");
 } else {if(history_options) return fail(2,"arguments");length=snprintf(request,sizeof(request),"1 %s",command);}
 request[length++]='\n';request[length]=0;
 double deadline=now()+timeout;
 int fd=socket(AF_UNIX,SOCK_STREAM|SOCK_NONBLOCK|SOCK_CLOEXEC,0);if(fd<0) return fail(3,"socket_unavailable");
 struct sockaddr_un address={.sun_family=AF_UNIX};strcpy(address.sun_path,path);
 if(connect(fd,(struct sockaddr *)&address,sizeof(address))<0) {
  if(errno!=EINPROGRESS) {close(fd);return fail(3,"service_unavailable");}
  if(wait_fd(fd,POLLOUT,deadline)<=0) {close(fd);return fail(5,"timeout");}
  int e=0;socklen_t n=sizeof(e);if(getsockopt(fd,SOL_SOCKET,SO_ERROR,&e,&n)||e) {close(fd);return fail(3,"service_unavailable");}
 }
 size_t sent=0;
 while(sent<(size_t)length) {
  ssize_t n=send(fd,request+sent,(size_t)length-sent,MSG_NOSIGNAL);
  if(n>0) sent+=(size_t)n;
  else if(n<0&&errno==EINTR) continue;
  else if(n<0&&(errno==EAGAIN||errno==EWOULDBLOCK)) {if(wait_fd(fd,POLLOUT,deadline)>0) continue;close(fd);return fail(5,"timeout");}
  else {close(fd);return fail(7,"write_failed");}
 }
 shutdown(fd,SHUT_WR);
 char *reply=malloc(QUERY_RESULT_MAX+1);if(!reply) {close(fd);return fail(7,"allocation_failed");}
 size_t used=0;int result=7;
 for(;;) {
  if(now()>=deadline) {result=5;break;}
  ssize_t n=recv(fd,reply+used,QUERY_RESULT_MAX-used,0);
  if(n>0) {
   used+=(size_t)n;reply[used]=0;char *end=memchr(reply,'\n',used);
   if(end) {
    int code=-1,offset=0;
    if(sscanf(reply,"{\"protocol\":1,\"code\":%d%n",&code,&offset)!=1||!offset||code<0||code>7||
       (reply[offset]!=','&&reply[offset]!='}')) break;
    size_t size=(size_t)(end-reply)+1;
    result=fwrite(reply,1,size,stdout)==size&&!fflush(stdout)?code:7;free(reply);close(fd);return result;
   }
   if(used==QUERY_RESULT_MAX) break;
  } else if(n==0) break;
  else if(errno==EINTR) continue;
  else if(errno==EAGAIN||errno==EWOULDBLOCK) {int rc=wait_fd(fd,POLLIN,deadline);if(rc>0) continue;result=rc==0?5:7;break;}
  else break;
 }
 free(reply);close(fd);return fail(result,result==5?"timeout":"invalid_or_closed_response");
}
