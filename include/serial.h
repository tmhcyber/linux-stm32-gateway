#ifndef GATEWAY_SERIAL_H
#define GATEWAY_SERIAL_H
#include <termios.h>
typedef struct { int fd, saved, exclusive; struct termios original; } Serial;
int serial_open(Serial *s, const char *path, int discard);
int serial_close(Serial *s);
#endif
