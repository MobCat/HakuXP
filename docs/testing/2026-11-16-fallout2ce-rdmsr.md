# Fallout 2 CE (homebrew) stops at its first RDMSR

**Title:** Fallout 2 CE, an Xbox port of fallout2-ce (homebrew, not a
retail disc), title ID `FFFF0002`, XMID as logged `FFFF00200`; its
certificate fields are homebrew values.
**Image:** `Fallout 2 CE.iso` on the Odin2's SD card, 583,860,224 bytes.
**XBE:** `default.xbe`, 2,527,232 bytes, MD5
`95521B7A1A72C7CC98E87A10FF2AA876` (read from the Odin2's image,
`tools/native/xbehash.py --adb`).
**Device:** AYN Odin2 (QCS8550, Adreno 740), Android 13.
**GPU driver:** see each run. **APK:** none; headless `xboxrun` only.

## Before

Headless sweep, 2026-11-16 (`build/logs/xboxrun/2026-11-16-sweep1/11.out`):
14 kernel calls in, `stop: system op 13 at 001a5306 (0f 32 89)`, exit 7.
System op 13 is RDMSR (`native/cpu/cpu.h`), which the kernel's system-op
handler didn't take.

## Diagnosis

The code at 0x1A52FA (read from the image) is
`mov dword [esp+28h], 2Ah; mov ecx, [esp+28h]; rdmsr`, then
`shr eax, 22; and eax, 2Fh` and a switch through a table at 0x240550 that
maps those bits to the clock multiplier times 10 (index 4 is 55, 0 is 50,
1 is 30 ...; values it lacks give 0). That is MSR 0x2A, EBL_CR_POWERON,
decoded as Intel documents the Pentium III's, the same table Linux's
speedstep-lib uses: the code works out the CPU's clock.

## Fix

`native/kernel/msr.c` (new, plain C, host test `test_msr`) and
`msr_op` in `native/kernel/kernel.c`, for RDMSR and WRMSR:

- 0x2A reads 0x01040000: multiplier bits 0100 (5.5) and bus bits 01
  (133 MHz), the Xbox's 733 MHz on a 133 MHz bus, in Linux's decode. Not
  read from a console.
- 0x10 (the TSC) reads what the guest's RDTSC would (`nc_cpu_guest_tsc`,
  new in the CPU interface: 733,333,333 Hz, as the translated RDTSC is
  scaled).
- 0x17 and 0x8B read 0; a write to them is accepted.
- Anything else reads 0 and drops writes, logged once per register
  (`cpu: RDMSR MSR 0000002a at 001a5306` is logged for the known ones
  too, once each). On the Xbox's CPU a register it lacks raises #GP;
  which ones the Xbox has was not checked.

## After

`fallout-new` (runner `a51fc134...`, Turnip PurpleVK 26.3.0-devel, empty
disk, the sweep's pad script, 90 s): `cpu: RDMSR MSR 0000002a at
001a5306`, then its main menu (Intro, New Game, Load Game, Options,
Credits, Exit) at frame 300; ran to the limit (exit 5). It runs at a
steady 8.6 fps: every frame 116.7 ms apart (7 vblanks), 739 stutters,
mostly "guest idle", the vCPU half halted. Logs and frame:
`build/logs/xboxrun/2026-11-16-fixes/fallout-new*`. That build ran with
MSR 0x10 still reading host counter ticks; the scaling came after it
(Fallout's code read only 0x2A in this run).

## Not tested

- Why 8.6 fps: frames exactly 7 vblanks apart with the CPU mostly idle
  point at its waits (KeDelayExecutionThread calls of 1 ms?), not its
  drawing. Not looked into.
- Its unimplemented imports `NtOpenDirectoryObject` and
  `NtQueryDirectoryObject` (not called in this run, as far as the log
  shows).
- In the app; past the main menu.
