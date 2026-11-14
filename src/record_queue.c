#define _POSIX_C_SOURCE 200809L
#include "storage.h"
#include <string.h>
#include <time.h>
int queue_init(RecordQueue *q, size_t capacity) {
 memset(q,0,sizeof(*q));
 if(!capacity||capacity>STORAGE_CAPACITY) return -1;
 if(pthread_mutex_init(&q->mutex,NULL)) return -1;
 pthread_condattr_t attr;
 if(pthread_condattr_init(&attr)) {pthread_mutex_destroy(&q->mutex);return -1;}
 int rc=pthread_condattr_setclock(&attr,CLOCK_MONOTONIC);
 if(!rc) rc=pthread_cond_init(&q->changed,&attr);
 pthread_condattr_destroy(&attr);
 if(rc) {pthread_mutex_destroy(&q->mutex);return -1;}
 q->capacity=capacity; return 0;
}
/* The storage producer uses this while holding the same mutex as its counters. */
int queue_push(RecordQueue *q, const StoredRecord *r) {
 if(q->closed) return -1;
 if(q->count==q->capacity) return 0;
 q->items[(q->head+q->count)%q->capacity]=*r; ++q->count;
 pthread_cond_signal(&q->changed); return 1;
}
int queue_pop_locked(RecordQueue *q, StoredRecord *r) {
 if(!q->count) return 0;
 *r=q->items[q->head]; q->head=(q->head+1)%q->capacity; --q->count;
 return 1;
}
void queue_destroy(RecordQueue *q) {
 pthread_cond_destroy(&q->changed); pthread_mutex_destroy(&q->mutex);
}
