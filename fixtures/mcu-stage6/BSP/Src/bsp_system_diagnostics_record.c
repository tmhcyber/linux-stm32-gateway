#include "bsp_system_diagnostics.h"

#include <stddef.h>
#include <string.h>

#define BSP_SYSTEM_DIAGNOSTICS_FNV_OFFSET_BASIS  2166136261UL
#define BSP_SYSTEM_DIAGNOSTICS_FNV_PRIME         16777619UL

#define BSP_SYSTEM_DIAGNOSTICS_MAIN_SRAM_BASE    0x20000000UL
#define BSP_SYSTEM_DIAGNOSTICS_MAIN_SRAM_SIZE    0x00020000UL
#define BSP_SYSTEM_DIAGNOSTICS_CCM_SRAM_BASE     0x10000000UL
#define BSP_SYSTEM_DIAGNOSTICS_CCM_SRAM_SIZE     0x00010000UL
#define BSP_SYSTEM_DIAGNOSTICS_BASIC_FRAME_SIZE  32UL
#define BSP_SYSTEM_DIAGNOSTICS_FP_FRAME_SIZE     104UL
#define BSP_SYSTEM_DIAGNOSTICS_FP_CORE_OFFSET    72UL

static uint8_t BSP_SystemDiagnostics_RangeIsInside(uint32_t start,
                                                   uint32_t length,
                                                   uint32_t region_base,
                                                   uint32_t region_size)
{
  uint32_t offset;

  if ((start < region_base) || (length > region_size))
  {
    return 0U;
  }

  offset = start - region_base;
  return (uint8_t)(offset <= (region_size - length));
}

static uint8_t BSP_SystemDiagnostics_FrameRangeIsInsideRam(uint32_t start,
                                                           uint32_t length)
{
  return (uint8_t)(
    (BSP_SystemDiagnostics_RangeIsInside(
       start,
       length,
       BSP_SYSTEM_DIAGNOSTICS_MAIN_SRAM_BASE,
       BSP_SYSTEM_DIAGNOSTICS_MAIN_SRAM_SIZE) != 0U) ||
    (BSP_SystemDiagnostics_RangeIsInside(
       start,
       length,
       BSP_SYSTEM_DIAGNOSTICS_CCM_SRAM_BASE,
       BSP_SYSTEM_DIAGNOSTICS_CCM_SRAM_SIZE) != 0U));
}

uint32_t BSP_SystemDiagnostics_DecodeResetReasons(uint32_t raw_rcc_csr)
{
  uint32_t reasons = 0U;

  if ((raw_rcc_csr & BSP_SYSTEM_DIAGNOSTICS_RAW_RESET_BOR) != 0U)
  {
    reasons |= BSP_SYSTEM_DIAGNOSTICS_RESET_REASON_BOR;
  }
  if ((raw_rcc_csr & BSP_SYSTEM_DIAGNOSTICS_RAW_RESET_PIN) != 0U)
  {
    reasons |= BSP_SYSTEM_DIAGNOSTICS_RESET_REASON_PIN;
  }
  if ((raw_rcc_csr & BSP_SYSTEM_DIAGNOSTICS_RAW_RESET_POR) != 0U)
  {
    reasons |= BSP_SYSTEM_DIAGNOSTICS_RESET_REASON_POR;
  }
  if ((raw_rcc_csr & BSP_SYSTEM_DIAGNOSTICS_RAW_RESET_SOFTWARE) != 0U)
  {
    reasons |= BSP_SYSTEM_DIAGNOSTICS_RESET_REASON_SOFTWARE;
  }
  if ((raw_rcc_csr & BSP_SYSTEM_DIAGNOSTICS_RAW_RESET_IWDG) != 0U)
  {
    reasons |= BSP_SYSTEM_DIAGNOSTICS_RESET_REASON_IWDG;
  }
  if ((raw_rcc_csr & BSP_SYSTEM_DIAGNOSTICS_RAW_RESET_WWDG) != 0U)
  {
    reasons |= BSP_SYSTEM_DIAGNOSTICS_RESET_REASON_WWDG;
  }
  if ((raw_rcc_csr & BSP_SYSTEM_DIAGNOSTICS_RAW_RESET_LOW_POWER) != 0U)
  {
    reasons |= BSP_SYSTEM_DIAGNOSTICS_RESET_REASON_LOW_POWER;
  }

  return reasons;
}

uint32_t BSP_SystemDiagnostics_CalculateFaultChecksum(
  const BSP_SystemFaultRecord_t *record)
{
  const uint32_t *word;
  size_t word_count;
  size_t index;
  uint32_t checksum;

  if (record == NULL)
  {
    return 0U;
  }

  word = &record->version;
  word_count = (sizeof(*record) - offsetof(BSP_SystemFaultRecord_t, version)) /
               sizeof(uint32_t);
  checksum = BSP_SYSTEM_DIAGNOSTICS_FNV_OFFSET_BASIS;
  for (index = 0U; index < word_count; ++index)
  {
    checksum ^= word[index];
    checksum *= BSP_SYSTEM_DIAGNOSTICS_FNV_PRIME;
  }

  return checksum;
}

BSP_SystemFaultRecordStatus_t BSP_SystemDiagnostics_ValidateFaultRecord(
  const BSP_SystemFaultRecord_t *record)
{
  if ((record == NULL) ||
      (record->commit != BSP_SYSTEM_DIAGNOSTICS_FAULT_RECORD_COMMIT))
  {
    return BSP_SYSTEM_FAULT_RECORD_NO_COMMITTED_RECORD;
  }

  if (record->version != BSP_SYSTEM_DIAGNOSTICS_FAULT_RECORD_VERSION)
  {
    return BSP_SYSTEM_FAULT_RECORD_INVALID_VERSION;
  }

  if (record->size != (uint32_t)sizeof(*record))
  {
    return BSP_SYSTEM_FAULT_RECORD_INVALID_SIZE;
  }

  if (record->checksum !=
      BSP_SystemDiagnostics_CalculateFaultChecksum(record))
  {
    return BSP_SYSTEM_FAULT_RECORD_INVALID_CHECKSUM;
  }

  if ((record->fault_type < (uint32_t)BSP_SYSTEM_FAULT_TYPE_HARDFAULT) ||
      (record->fault_type > (uint32_t)BSP_SYSTEM_FAULT_TYPE_USAGEFAULT) ||
      (record->frame_valid > 1U) ||
      (record->extended_frame > 1U))
  {
    return BSP_SYSTEM_FAULT_RECORD_INVALID_CONTENT;
  }

  if (((record->frame_valid != 0U) &&
       ((record->frame_reject_reasons !=
         BSP_SYSTEM_DIAGNOSTICS_FRAME_REJECT_NONE) ||
        (record->core_frame_sp == 0U))) ||
      ((record->frame_valid == 0U) && (record->core_frame_sp != 0U)))
  {
    return BSP_SYSTEM_FAULT_RECORD_INVALID_CONTENT;
  }

  return BSP_SYSTEM_FAULT_RECORD_VALID;
}

BSP_SystemFaultRecordStatus_t BSP_SystemDiagnostics_ConsumeFaultRecord(
  BSP_SystemFaultRecord_t *retained_record,
  BSP_SystemFaultRecord_t *boot_copy)
{
  BSP_SystemFaultRecordStatus_t status;

  if ((retained_record == NULL) || (boot_copy == NULL))
  {
    return BSP_SYSTEM_FAULT_RECORD_INVALID_CONTENT;
  }

  (void)memset(boot_copy, 0, sizeof(*boot_copy));
  status = BSP_SystemDiagnostics_ValidateFaultRecord(retained_record);
  if (status == BSP_SYSTEM_FAULT_RECORD_VALID)
  {
    *boot_copy = *retained_record;
  }

  retained_record->commit = BSP_SYSTEM_DIAGNOSTICS_FAULT_RECORD_INVALID;
  return status;
}

uint32_t BSP_SystemDiagnostics_EvaluateFrame(uint32_t original_sp,
                                             uint32_t exc_return,
                                             uint32_t cfsr,
                                             uint32_t *core_frame_sp)
{
  uint32_t reject_reasons = BSP_SYSTEM_DIAGNOSTICS_FRAME_REJECT_NONE;
  uint32_t frame_size;
  uint32_t core_offset;

  if (core_frame_sp != NULL)
  {
    *core_frame_sp = 0U;
  }

  if ((original_sp & 0x3U) != 0U)
  {
    reject_reasons |= BSP_SYSTEM_DIAGNOSTICS_FRAME_REJECT_MISALIGNED;
  }

  if ((cfsr & BSP_SYSTEM_DIAGNOSTICS_CFSR_STACK_ERROR_MASK) != 0U)
  {
    reject_reasons |= BSP_SYSTEM_DIAGNOSTICS_FRAME_REJECT_STACK_ERROR;
  }

  if ((exc_return & BSP_SYSTEM_DIAGNOSTICS_EXC_RETURN_BASIC_FRAME) != 0U)
  {
    frame_size = BSP_SYSTEM_DIAGNOSTICS_BASIC_FRAME_SIZE;
    core_offset = 0U;
  }
  else
  {
    frame_size = BSP_SYSTEM_DIAGNOSTICS_FP_FRAME_SIZE;
    core_offset = BSP_SYSTEM_DIAGNOSTICS_FP_CORE_OFFSET;
  }

  if (BSP_SystemDiagnostics_FrameRangeIsInsideRam(original_sp,
                                                   frame_size) == 0U)
  {
    reject_reasons |= BSP_SYSTEM_DIAGNOSTICS_FRAME_REJECT_OUT_OF_RANGE;
  }

  if ((reject_reasons == BSP_SYSTEM_DIAGNOSTICS_FRAME_REJECT_NONE) &&
      (core_frame_sp != NULL))
  {
    *core_frame_sp = original_sp + core_offset;
  }

  return reject_reasons;
}
