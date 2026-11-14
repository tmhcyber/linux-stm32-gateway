#define _POSIX_C_SOURCE 200809L
#include "storage.h"
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#define PRODUCERS 3
#define EACH 50000
typedef struct {RecordQueue *queue;unsigned id;uint64_t accepted,dropped;} Producer;
typedef struct {RecordQueue *queue;uint64_t consumed;} Consumer;
static void *produce(void *arg) {
 Producer *p=arg;
 for(unsigned i=1;i<=EACH;++i) {
  StoredRecord r={.rx_id=(uint64_t)p->id*EACH+i,.session=p->id,.body_len=8};
  r.record.points[0].value=(int32_t)i;memcpy(r.body,&r.rx_id,8);
  pthread_mutex_lock(&p->queue->mutex);int rc=queue_push(p->queue,&r);pthread_mutex_unlock(&p->queue->mutex);
  assert(rc>=0);if(rc) ++p->accepted;else ++p->dropped;
  memset(&r,0,sizeof(r));
 }
 return NULL;
}
static void *consume(void *arg) {
 Consumer *c=arg;unsigned last[PRODUCERS]={0};
 for(;;) {
  StoredRecord r;pthread_mutex_lock(&c->queue->mutex);
  while(!c->queue->count&&!c->queue->closed) pthread_cond_wait(&c->queue->changed,&c->queue->mutex);
  int have=queue_pop_locked(c->queue,&r);int closed=c->queue->closed;pthread_mutex_unlock(&c->queue->mutex);
  if(!have) {assert(closed);break;}
  unsigned n=(unsigned)r.record.points[0].value;uint64_t body;memcpy(&body,r.body,8);
  assert(r.session<PRODUCERS&&n>last[r.session]&&n<=EACH);
  assert(r.rx_id==r.session*EACH+n&&body==r.rx_id);last[r.session]=n;++c->consumed;
 }
 return NULL;
}
static void *observe(void *arg) {
 RecordQueue *q=arg;
 for(;;) {
  pthread_mutex_lock(&q->mutex);assert(q->count<=q->capacity);int closed=q->closed;pthread_mutex_unlock(&q->mutex);
  if(closed) break;
 }
 return NULL;
}
int main(void) {
 RecordQueue *q=malloc(sizeof(*q));assert(q&&queue_init(q,STORAGE_CAPACITY)==0);
 pthread_t writers[PRODUCERS],reader,observer;Producer p[PRODUCERS];Consumer c={.queue=q};
 assert(!pthread_create(&reader,NULL,consume,&c));assert(!pthread_create(&observer,NULL,observe,q));
 for(unsigned i=0;i<PRODUCERS;++i) {p[i]=(Producer){.queue=q,.id=i};assert(!pthread_create(&writers[i],NULL,produce,&p[i]));}
 uint64_t accepted=0,dropped=0;
 for(unsigned i=0;i<PRODUCERS;++i) {assert(!pthread_join(writers[i],NULL));accepted+=p[i].accepted;dropped+=p[i].dropped;}
 pthread_mutex_lock(&q->mutex);q->closed=1;pthread_cond_broadcast(&q->changed);pthread_mutex_unlock(&q->mutex);
 assert(!pthread_join(reader,NULL));assert(!pthread_join(observer,NULL));
 assert(c.consumed==accepted&&accepted+dropped==PRODUCERS*EACH);
 printf("pthread stress: 150000 attempted, %llu accepted/consumed, %llu dropped, FIFO/copy/counts passed\n",
  (unsigned long long)accepted,(unsigned long long)dropped);
 queue_destroy(q);free(q);return 0;
}
