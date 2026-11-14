#include "app_diagnostic.h"
#include "app_uplink_service.h"
#include "bsp_uart.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#ifdef _WIN32
#include <io.h>
#include <fcntl.h>
#endif
static const uint8_t *active;
static char sent[512];
static unsigned starts;
static BSP_UART_Status_t next_status;
static BSP_UART_TxEvent_t event;
BSP_UART_Status_t BSP_UART1_Init(void) { return BSP_UART_STATUS_OK; }
BSP_UART_Status_t BSP_UART1_WriteAsync(const uint8_t *p, uint16_t n) {
  assert(!active); ++starts;
  if (next_status != BSP_UART_STATUS_OK) return next_status;
  active=p; assert(n<sizeof(sent)); memcpy(sent,p,n); sent[n]=0;
  return BSP_UART_STATUS_OK;
}
BSP_UART_TxEvent_t BSP_UART1_TakeTxEvent(void) {
  BSP_UART_TxEvent_t result=event; event.type=BSP_UART_TX_EVENT_NONE;
  if(result.type!=BSP_UART_TX_EVENT_NONE) { assert(active);assert(!strcmp((const char *)active,sent));active=NULL; }
  return result;
}
static void complete(uint32_t t, BSP_UART_TxEventType_t type) {
  event.type=type;event.timestamp_ms=t;App_UplinkService_Process(t);
}
static BSP_SystemFaultRecord_t fault(void) {
  BSP_SystemFaultRecord_t f={0};
  f.commit=BSP_SYSTEM_DIAGNOSTICS_FAULT_RECORD_COMMIT;
  f.version=1;f.size=sizeof(f);f.fault_type=4;f.frame_valid=1;
  f.core_frame_sp=0x20001000U;f.pc=UINT32_MAX;f.cfsr=UINT32_MAX;f.hfsr=UINT32_MAX;
  f.checksum=BSP_SystemDiagnostics_CalculateFaultChecksum(&f);return f;
}
int main(int argc, char **argv) {
  BSP_SystemBootReport_t boot={0};App_SystemMonitorReport_t monitor={0};
  App_WatchdogGateReport_t gate={0};App_Diagnostic_t d;char text[512];uint32_t n;
  unsigned i;int vectors=argc==2&&!strcmp(argv[1],"--vectors");
#ifdef _WIN32
  if(vectors) (void)_setmode(_fileno(stdout),_O_BINARY);
#endif
  boot.reset.captured=1;boot.reset.raw_rcc_csr=UINT32_MAX;
  boot.reset.reasons=BSP_SystemDiagnostics_DecodeResetReasons(boot.reset.raw_rcc_csr);
  assert(boot.reset.reasons==127U);
  monitor.monitor_valid=1;monitor.health_state=APP_SYSTEM_HEALTH_UNHEALTHY;
  monitor.active_reasons=monitor.observed_reasons=255U;
  gate.initialized=1;gate.decision=APP_WATCHDOG_GATE_DECISION_LATCHED;gate.latched_reasons=0x70090U;
  for(i=0;i<8;++i) {
    BSP_SystemFaultRecord_t retained=fault();
    if(i==0) retained.commit=0;
    if(i==2) retained.version=2;
    if(i==3) retained.size=0;
    if(i==4) retained.checksum=0;
    if(i==5) { retained.fault_type=9;retained.checksum=BSP_SystemDiagnostics_CalculateFaultChecksum(&retained); }
    if(i==6) { retained.frame_valid=0;retained.frame_reject_reasons=7;
      retained.core_frame_sp=0;retained.checksum=BSP_SystemDiagnostics_CalculateFaultChecksum(&retained); }
    boot.previous_fault_status=BSP_SystemDiagnostics_ConsumeFaultRecord(&retained,&boot.previous_fault);
    assert(retained.commit==0);
    App_Diagnostic_Map(&d,&boot,&monitor,&gate,i==7?0:UINT32_MAX-i);
    if(i!=1&&i!=6&&i!=7) assert(d.pc==0&&d.fault_type==0);
    if(i==6) assert(d.frame_valid==0&&d.pc==0&&d.frame_reject==7&&d.cfsr==UINT32_MAX);
    assert(App_Diagnostic_Format(&d,text,sizeof(text),&n));assert(n<=511&&strlen(text)==n);
    if(vectors) fwrite(text,1,n,stdout);
  }
  for(i=0;i<4;++i) {
    memset(&boot,0,sizeof(boot));memset(&monitor,0,sizeof(monitor));memset(&gate,0,sizeof(gate));
    monitor.monitor_valid=i!=0;monitor.health_state=(App_SystemHealthState_t)i;
    gate.initialized=i!=0;gate.decision=(App_WatchdogGateDecision_t)i;
    App_Diagnostic_Map(&d,&boot,&monitor,&gate,i*5000U);
    assert(App_Diagnostic_Format(&d,text,sizeof(text),&n));
    if(vectors) fwrite(text,1,n,stdout);
  }
  /* Bound even the longest textual uint32 for every member, including invalid enums. */
  memset(&d,255,sizeof(d));assert(App_Diagnostic_Format(&d,text,sizeof(text),&n));assert(n<=511);
  assert(!App_Diagnostic_Format(&d,text,n,&n)&&n==0&&text[0]==0);
  if(vectors) return 0;
  assert(App_UplinkService_Init()==APP_UPLINK_SERVICE_RESULT_OK);
#if APP_DIAGNOSTICS_ENABLED
  {
    App_SnapshotEvent_t e={0};unsigned before;
    App_DataService_Init();assert(App_DataService_GetSnapshot(&e.snapshot)==APP_DATA_RESULT_OK);
    e.flags=APP_SNAPSHOT_EVENT_NONE; /* valid zero-initialized snapshot, EVENT needs a flag */
    e.flags=(App_SnapshotEventFlags_t)1;
    App_Diagnostic_Map(&d,&boot,&monitor,&gate,5000);
    assert(!App_UplinkService_DiagnosticDue(4999));assert(App_UplinkService_DiagnosticDue(5000));
    App_UplinkService_SubmitDiagnostic(&d);App_UplinkService_SubmitDiagnostic(&d);
    assert(g_app_uplink_service_report.diagnostic_replaced==1);
    assert(App_UplinkService_SubmitEvent(&e)==APP_UPLINK_SERVICE_RESULT_OK);
    App_UplinkService_Process(5000);assert(!strncmp(sent,"v=1;",4));
    before=starts;App_UplinkService_SubmitDiagnostic(&d);App_UplinkService_Process(5001);
    assert(starts==before);complete(5002,BSP_UART_TX_EVENT_COMPLETE);
    assert(!strncmp(sent,"d=1;",4));assert(g_app_uplink_service_report.completed_count==1);
    App_UplinkService_SubmitEvent(&e);App_UplinkService_SubmitDiagnostic(&d);
    before=starts;App_UplinkService_Process(5003);assert(starts==before);
    complete(5004,BSP_UART_TX_EVENT_COMPLETE);assert(!strncmp(sent,"v=1;",4));
    assert(g_app_uplink_service_report.last_completed_at_ms==5002);
    assert(g_app_uplink_service_report.diagnostic_completed==1);
    complete(5005,BSP_UART_TX_EVENT_COMPLETE);
    assert(!active);App_UplinkService_Process(9999);assert(!active);
    next_status=BSP_UART_STATUS_BUSY;App_UplinkService_Process(10002);
    assert(g_app_uplink_service_report.diagnostic_busy==1);
    before=starts;App_UplinkService_Process(10003);assert(starts==before);
    /* Telemetry always wins, including diagnostic retry intervals. */
    App_UplinkService_SubmitEvent(&e);next_status=BSP_UART_STATUS_OK;
    App_UplinkService_Process(15002);assert(!strncmp(sent,"v=1;",4));
    complete(15003,BSP_UART_TX_EVENT_COMPLETE);assert(!strncmp(sent,"d=1;",4));
    complete(15004,BSP_UART_TX_EVENT_ERROR);assert(g_app_uplink_service_report.diagnostic_errors==1);
    assert(g_app_uplink_service_report.tx_error_count==0);
    assert(g_app_uplink_service_report.last_completed_at_ms==15003);
    assert(App_UplinkService_DiagnosticDue(UINT32_MAX-10));
    assert(!App_UplinkService_DiagnosticDue(10));assert(App_UplinkService_DiagnosticDue(5000));
    App_UplinkService_Init();App_UplinkService_SubmitDiagnostic(&d);next_status=BSP_UART_STATUS_ERROR;
    App_UplinkService_Process(5000);assert(g_app_uplink_service_report.diagnostic_errors==1);
    /* No telemetry has ever completed: diagnostics still cannot set its health clock. */
    App_UplinkService_Init();next_status=BSP_UART_STATUS_OK;
    App_UplinkService_SubmitDiagnostic(&d);App_UplinkService_Process(5000);
    complete(5001,BSP_UART_TX_EVENT_COMPLETE);
    assert(!g_app_uplink_service_report.has_successful_transmission);
    assert(!g_app_uplink_service_report.completed_count&&!g_app_uplink_service_report.last_completed_at_ms);
    {
      App_SystemTimingSample_t timing={0};App_SystemHealthInputs_t inputs={0};
      App_SystemMonitorReport_t report;
      App_SystemMonitor_Init(16000000,0,0);App_SystemMonitor_Process(&timing,&inputs,7000);
      App_SystemMonitor_GetReport(&report);assert(report.active_reasons&APP_SYSTEM_HEALTH_REASON_UPLINK_STALLED);
    }
  }
#else
  App_UplinkService_Process(5000);App_UplinkService_Process(UINT32_MAX);assert(starts==0);
  (void)complete; /* helper remains referenced for strict host builds */
#endif
  puts("stage6 formatter/mapping/arbitration checks passed");return 0;
}
