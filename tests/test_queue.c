#include "storage.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
int main(void) {
 RecordQueue *q=malloc(sizeof(*q)); assert(q);
 assert(queue_init(q,0)<0); assert(queue_init(q,STORAGE_CAPACITY+1)<0);
 assert(queue_init(q,2)==0);
 pthread_mutex_lock(&q->mutex);
 StoredRecord r={.rx_id=1,.body_len=3}, got;
 memcpy(r.body,"one",3); r.record.points[0].value=123;
 assert(queue_push(q,&r)==1);
 r.rx_id=2; memcpy(r.body,"two",3); r.record.points[0].value=456;
 assert(queue_push(q,&r)==1);
 r.rx_id=3; assert(queue_push(q,&r)==0); /* drop newest, keep FIFO */
 memset(&r,0,sizeof(r));
 assert(queue_pop_locked(q,&got)==1&&got.rx_id==1);
 assert(!memcmp(got.body,"one",3)&&got.record.points[0].value==123);
 assert(queue_push(q,&(StoredRecord){.rx_id=3})==1); /* ring wrap */
 assert(queue_pop_locked(q,&got)==1&&got.rx_id==2);
 assert(!memcmp(got.body,"two",3)&&got.record.points[0].value==456);
 assert(queue_pop_locked(q,&got)==1&&got.rx_id==3);
 assert(queue_pop_locked(q,&got)==0);
 q->closed=1; assert(queue_push(q,&r)==-1);
 pthread_mutex_unlock(&q->mutex); queue_destroy(q); free(q);
 puts("queue FIFO/copy/full/close passed"); return 0;
}
