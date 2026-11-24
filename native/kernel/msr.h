/*
 * hakuX native core -- RDMSR and WRMSR. Titles run in ring 0 on the Xbox,
 * and some code reads a model-specific register: Fallout 2 CE (homebrew)
 * reads EBL_CR_POWERON for the CPU's clock multiplier. Plain C, no kernel
 * state: kernel.c moves the values in and out of the guest's registers.
 */
#ifndef NATIVE_KERNEL_MSR_H
#define NATIVE_KERNEL_MSR_H

#include <stdbool.h>
#include <stdint.h>

#define NC_MSR_TSC             0x10u
#define NC_MSR_EBL_CR_POWERON  0x2Au

/* The value RDMSR reads (tsc: the guest's RDTSC count now). False for a
 * register HakuXP doesn't model: *value is 0 then and the guest goes on
 * (the CPU raises #GP for a register it doesn't have; which ones the
 * Xbox's has wasn't checked). */
bool nc_msr_read(uint32_t msr, uint64_t tsc, uint64_t *value);
/* WRMSR: nothing is kept (MTRRs and the like have no effect here). False
 * for a register HakuXP doesn't model. */
bool nc_msr_write(uint32_t msr, uint64_t value);

#endif
