/*
 * hakuX native core -- RDMSR and WRMSR (msr.h).
 *
 * The Xbox's CPU is a Pentium III (family 6, model 8: kernel.c's CPUID) at
 * 733 MHz on a 133 MHz front-side bus, a ratio of 5.5. The values below
 * follow Intel's P6 encodings for that CPU; they were not read from a
 * console.
 */
#include "native/kernel/msr.h"

/* EBL_CR_POWERON: the power-on configuration. Bits 27 and 25:22 hold the
 * core/bus clock ratio, 0100b for 5.5 (the decode Fallout 2 CE's code and
 * Linux's speedstep-lib share); bits 19:18 the bus, 01b for 133 MHz in
 * Linux's decode. Everything else 0. */
#define EBL_RATIO_5_5   (0x4u << 22)
#define EBL_BUS_133     (0x1u << 18)

/* Registers that read as 0 on purpose: the platform ID, the microcode
 * update signature. */
static bool known_zero(uint32_t msr)
{
    return msr == 0x17u || msr == 0x8Bu;
}

bool nc_msr_read(uint32_t msr, uint64_t tsc, uint64_t *value)
{
    switch (msr) {
    case NC_MSR_TSC:
        *value = tsc;
        return true;
    case NC_MSR_EBL_CR_POWERON:
        *value = EBL_RATIO_5_5 | EBL_BUS_133;
        return true;
    default:
        *value = 0;
        return known_zero(msr);
    }
}

bool nc_msr_write(uint32_t msr, uint64_t value)
{
    (void)value;
    return known_zero(msr);
}
