#ifndef BSP_SYSTEM_DIAGNOSTICS_H
#define BSP_SYSTEM_DIAGNOSTICS_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define BSP_SYSTEM_DIAGNOSTICS_FAULT_RECORD_VERSION       1UL
#define BSP_SYSTEM_DIAGNOSTICS_FAULT_RECORD_COMMIT        0x4641554CUL
#define BSP_SYSTEM_DIAGNOSTICS_FAULT_RECORD_INVALID       0UL

#define BSP_SYSTEM_DIAGNOSTICS_RAW_RESET_BOR              (1UL << 25U)
#define BSP_SYSTEM_DIAGNOSTICS_RAW_RESET_PIN              (1UL << 26U)
#define BSP_SYSTEM_DIAGNOSTICS_RAW_RESET_POR              (1UL << 27U)
#define BSP_SYSTEM_DIAGNOSTICS_RAW_RESET_SOFTWARE         (1UL << 28U)
#define BSP_SYSTEM_DIAGNOSTICS_RAW_RESET_IWDG             (1UL << 29U)
#define BSP_SYSTEM_DIAGNOSTICS_RAW_RESET_WWDG             (1UL << 30U)
#define BSP_SYSTEM_DIAGNOSTICS_RAW_RESET_LOW_POWER        (1UL << 31U)

#define BSP_SYSTEM_DIAGNOSTICS_RESET_REASON_BOR           (1UL << 0U)
#define BSP_SYSTEM_DIAGNOSTICS_RESET_REASON_PIN           (1UL << 1U)
#define BSP_SYSTEM_DIAGNOSTICS_RESET_REASON_POR           (1UL << 2U)
#define BSP_SYSTEM_DIAGNOSTICS_RESET_REASON_SOFTWARE      (1UL << 3U)
#define BSP_SYSTEM_DIAGNOSTICS_RESET_REASON_IWDG          (1UL << 4U)
#define BSP_SYSTEM_DIAGNOSTICS_RESET_REASON_WWDG          (1UL << 5U)
#define BSP_SYSTEM_DIAGNOSTICS_RESET_REASON_LOW_POWER     (1UL << 6U)

#define BSP_SYSTEM_DIAGNOSTICS_FRAME_REJECT_NONE          0UL
#define BSP_SYSTEM_DIAGNOSTICS_FRAME_REJECT_STACK_ERROR   (1UL << 0U)
#define BSP_SYSTEM_DIAGNOSTICS_FRAME_REJECT_MISALIGNED    (1UL << 1U)
#define BSP_SYSTEM_DIAGNOSTICS_FRAME_REJECT_OUT_OF_RANGE  (1UL << 2U)

#define BSP_SYSTEM_DIAGNOSTICS_CFSR_STACK_ERROR_MASK      0x00003838UL
#define BSP_SYSTEM_DIAGNOSTICS_EXC_RETURN_BASIC_FRAME     (1UL << 4U)

typedef enum
{
  BSP_SYSTEM_FAULT_TYPE_NONE = 0,
  BSP_SYSTEM_FAULT_TYPE_HARDFAULT = 1,
  BSP_SYSTEM_FAULT_TYPE_MEMMANAGE = 2,
  BSP_SYSTEM_FAULT_TYPE_BUSFAULT = 3,
  BSP_SYSTEM_FAULT_TYPE_USAGEFAULT = 4
} BSP_SystemFaultType_t;

typedef enum
{
  BSP_SYSTEM_FAULT_RECORD_NO_COMMITTED_RECORD = 0,
  BSP_SYSTEM_FAULT_RECORD_VALID = 1,
  BSP_SYSTEM_FAULT_RECORD_INVALID_VERSION = 2,
  BSP_SYSTEM_FAULT_RECORD_INVALID_SIZE = 3,
  BSP_SYSTEM_FAULT_RECORD_INVALID_CHECKSUM = 4,
  BSP_SYSTEM_FAULT_RECORD_INVALID_CONTENT = 5
} BSP_SystemFaultRecordStatus_t;

typedef struct
{
  uint32_t raw_rcc_csr;
  uint32_t reasons;
  uint32_t captured;
} BSP_SystemResetReport_t;

typedef struct
{
  uint32_t commit;
  uint32_t checksum;
  uint32_t version;
  uint32_t size;
  uint32_t fault_type;
  uint32_t exc_return;
  uint32_t original_sp;
  uint32_t core_frame_sp;
  uint32_t frame_valid;
  uint32_t extended_frame;
  uint32_t frame_reject_reasons;
  uint32_t r0;
  uint32_t r1;
  uint32_t r2;
  uint32_t r3;
  uint32_t r12;
  uint32_t lr;
  uint32_t pc;
  uint32_t xpsr;
  uint32_t cfsr;
  uint32_t hfsr;
  uint32_t dfsr;
  uint32_t afsr;
  uint32_t mmfar;
  uint32_t bfar;
  uint32_t shcsr;
  uint32_t icsr;
} BSP_SystemFaultRecord_t;

typedef struct
{
  BSP_SystemResetReport_t reset;
  BSP_SystemFaultRecordStatus_t previous_fault_status;
  BSP_SystemFaultRecord_t previous_fault;
} BSP_SystemBootReport_t;

uint32_t BSP_SystemDiagnostics_DecodeResetReasons(uint32_t raw_rcc_csr);
uint32_t BSP_SystemDiagnostics_CalculateFaultChecksum(
  const BSP_SystemFaultRecord_t *record);
BSP_SystemFaultRecordStatus_t BSP_SystemDiagnostics_ValidateFaultRecord(
  const BSP_SystemFaultRecord_t *record);
BSP_SystemFaultRecordStatus_t BSP_SystemDiagnostics_ConsumeFaultRecord(
  BSP_SystemFaultRecord_t *retained_record,
  BSP_SystemFaultRecord_t *boot_copy);
uint32_t BSP_SystemDiagnostics_EvaluateFrame(uint32_t original_sp,
                                             uint32_t exc_return,
                                             uint32_t cfsr,
                                             uint32_t *core_frame_sp);

void BSP_SystemDiagnostics_CaptureBootReport(BSP_SystemBootReport_t *report);
void BSP_SystemDiagnostics_EnableConfigurableFaults(void);
void BSP_SystemDiagnostics_CaptureFault(BSP_SystemFaultType_t fault_type,
                                        uint32_t exc_return,
                                        const uint32_t *stack_pointer);

#ifdef __cplusplus
}
#endif

#endif
