/* Exercise actual fixed JSON serializer and offer() without a network thread. */
#include "../src/mqtt.c"
#include <assert.h>
#include <float.h>
double monotonic_now(void) {return 1000.;}
void utc_now(char *buffer,size_t size) {snprintf(buffer,size,"9999-12-31T23:59:59Z");}
int main(void) {
 Mqtt m={0};assert(!pthread_mutex_init(&m.mutex,NULL));
 memset(m.gateway,'G',32);memset(m.device,'D',32);memset(m.run,'a',32);
 m.source.quality=Q_OFFLINE;m.source.status=S_HUMIDITY_CRC_ERROR;
 m.source.seq=m.source.value_ms=m.source.status_ms=UINT32_MAX;
 StorageStats s={.committed=UINT64_MAX,.failures=UINT64_MAX};
 StoredRecord d={.rx_id=UINT64_MAX,.session=UINT64_MAX,.elapsed_s=DBL_MAX};
 snprintf(d.utc,sizeof(d.utc),"9999-12-31T23:59:59Z");memset(&d.diagnostic,255,sizeof(d.diagnostic));
 mqtt_state(&m,1,0,DBL_MAX,UINT64_MAX,1,&s,1,&d,UINT64_MAX,DBL_MAX);
 assert(m.slots[1].have&&!m.stats.format_errors);
 size_t length=strlen(m.slots[1].json);assert(length<MQTT_JSON_CAPACITY);
 assert(strstr(m.slots[1].json,"\"latched\":4294967295"));
 puts(m.slots[1].json);
 /* Truncation itself must be accounted, never queued as valid JSON. */
 char buffer[MQTT_JSON_CAPACITY]={0};size_t used=MQTT_JSON_CAPACITY-1;
 assert(append(buffer,&used,"XX")<0);offer(&m,1,buffer,used);assert(m.stats.format_errors==1);
 pthread_mutex_destroy(&m.mutex);return 0;
}
