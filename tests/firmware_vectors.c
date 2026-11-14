/* Build against the preserved, unmodified main-firmware reference.
 * Exercises the real DataService -> SnapshotConsumer -> TextFormatter path.
 * This is a host harness, not a flashed image or a hardware fault injection.
 */
#include "app_data_service.h"
#include "app_snapshot_consumer.h"
#include "app_snapshot_text_formatter.h"
#include <assert.h>
#include <stdio.h>
static App_SnapshotConsumer_t consumer;
static void emit(void) {
 App_SnapshotEvent_t event; uint8_t ready;
 assert(App_SnapshotConsumer_Process(&consumer,&event,&ready)==APP_DATA_RESULT_OK);
 assert(ready);
 App_UplinkMessage_t msg={0}; msg.reason=APP_UPLINK_REASON_EVENT;
 msg.flags=event.flags; msg.skipped_count=event.skipped_count; msg.snapshot=event.snapshot;
 char text[512]; uint32_t len;
 assert(App_SnapshotTextFormatter_Format(&msg,text,sizeof(text),&len)==APP_UPLINK_TEXT_RESULT_OK);
 assert(fwrite(text,1,len,stdout)==len);
}
static void start(void) { App_DataService_Init(); App_SnapshotConsumer_Init(&consumer); emit(); }
int main(void) {
 for(App_DataStatus_t s=APP_DATA_STATUS_INVALID_ARGUMENT;s<=APP_DATA_STATUS_HUMIDITY_CRC_ERROR;++s) {
  start();
  assert(App_DataService_UpdateSourceQuality(APP_DATA_SOURCE_SHT30,APP_DATA_QUALITY_NOT_AVAILABLE,s,215)==APP_DATA_RESULT_OK);
  emit();
 }
 start();
 assert(App_DataService_UpdateSourceQuality(APP_DATA_SOURCE_SHT30,APP_DATA_QUALITY_OFFLINE,APP_DATA_STATUS_NACK,3000)==APP_DATA_RESULT_OK);
 emit();
 App_DataValueUpdate_t values[2]={
  {APP_DATA_SOURCE_SHT30,APP_DATA_POINT_TEMPERATURE,25000,-3,APP_DATA_UNIT_DEGREE_CELSIUS},
  {APP_DATA_SOURCE_SHT30,APP_DATA_POINT_HUMIDITY,50000,-3,APP_DATA_UNIT_PERCENT_RH}};
 assert(App_DataService_PublishValues(values,2,4000)==APP_DATA_RESULT_OK); emit();
 assert(App_DataService_UpdateSourceQuality(APP_DATA_SOURCE_SHT30,APP_DATA_QUALITY_STALE,APP_DATA_STATUS_TIMEOUT,5000)==APP_DATA_RESULT_OK); emit();
 assert(App_DataService_UpdateSourceQuality(APP_DATA_SOURCE_SHT30,APP_DATA_QUALITY_OFFLINE,APP_DATA_STATUS_TEMPERATURE_CRC_ERROR,7000)==APP_DATA_RESULT_OK); emit();
 assert(App_DataService_PublishValues(values,2,8000)==APP_DATA_RESULT_OK); emit();
 return 0;
}
