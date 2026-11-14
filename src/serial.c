#define _DEFAULT_SOURCE
#include "serial.h"
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/file.h>
#include <errno.h>
int serial_close(Serial *s) {
 int error=0;
 if(s->fd>=0) {
  if(s->saved&&tcsetattr(s->fd,TCSANOW,&s->original)<0) error=errno;
  if(s->exclusive&&ioctl(s->fd,TIOCNXCL)<0&&!error) error=errno;
  if(close(s->fd)<0&&!error) error=errno;
 }
 s->fd=-1; s->saved=0; s->exclusive=0;
 if(error) {errno=error; return -1;} return 0;
}
int serial_open(Serial *s, const char *path, int discard) {
 s->saved=0; s->exclusive=0;
 s->fd=open(path,O_RDONLY|O_NOCTTY|O_NONBLOCK|O_CLOEXEC);
 if(s->fd<0) return -1;
 if(flock(s->fd,LOCK_EX|LOCK_NB)<0||ioctl(s->fd,TIOCEXCL)<0) goto fail;
 s->exclusive=1;
 if(tcgetattr(s->fd,&s->original)<0) goto fail;
 s->saved=1; struct termios t=s->original; cfmakeraw(&t);
 t.c_cflag&=~(CSIZE|PARENB|CSTOPB|CRTSCTS);
 t.c_cflag|=CS8|CREAD|CLOCAL; t.c_iflag&=~(IXON|IXOFF|IXANY);
 t.c_cc[VMIN]=0; t.c_cc[VTIME]=0;
 if(cfsetispeed(&t,B115200)<0||cfsetospeed(&t,B115200)<0||tcsetattr(s->fd,TCSANOW,&t)<0) goto fail;
 if(discard&&tcflush(s->fd,TCIFLUSH)<0) goto fail;
 return 0;
fail: { int e=errno; serial_close(s); errno=e; return -1; }
}
