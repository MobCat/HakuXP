# Dennou Taisen - DroneZ stops drawing after its nickname screen

**Title:** Dennou Taisen - DroneZ (Japan), `4D440003` `MD00303J`.
**Image:** `Dennou Taisen - DroneZ (Japan).iso` on the Odin2's SD card,
1,260,978,176 bytes.
**XBE:** `default.xbe`, 3,207,168 bytes, MD5
`4E191FA99F973C5C302CA7FDEAAA41F7` (read from the Odin2's image,
`tools/native/xbehash.py --adb`).
**Device:** AYN Odin2 (QCS8550, Adreno 740), Android 13.
**GPU driver:** see each run. **APK:** none; headless `xboxrun` only.

## Before

Headless sweep, 2026-11-16 (`build/logs/xboxrun/2026-11-16-sweep1/10.out`,
runner MD5 `9dcfd184...`, Qualcomm's system driver: `vkdriver=` was being
ignored then), empty disk, Start and A pressed alternately every 5 s from
15 s: 59.9 fps through the logos and the nickname keyboard, then after
frame 2401 (about 40 s) no frame again; `hang=30` ended it (exit 6).

At the hang every guest thread waits except two:

- t8 (priority 8, the title's main thread) loops at 0x98F05-0x98F5F:
  `ObReferenceObjectByHandle(8, ...)`, `NtYieldExecution()`,
  `ObfDereferenceObject(...)`, again and again: it polls a thread object
  for its end.
- t68 (priority 7) is ready and never runs.

## Diagnosis

`NtYieldExecution` called `nc_ps_yield`, which puts the thread behind the
ready threads of its own priority; the scheduler then picks the highest
priority ready thread, which is t8 again. On NT, `NtYieldExecution`
(SwitchToThread) gives the processor to any ready thread, a lower
priority's too, for up to one quantum (`KiFindReadyThread` from priority
1); Sleep(0), a zero delay, is the one that only gives way to equal or
higher priorities. The Xbox kernel is NT's; that it does the same was not
checked on a console.

## Fix

`nc_ps_yield_any` (`native/kernel/ps.c`), for `NtYieldExecution` only
(`native/kernel/ke.c`): the yielding thread sits out the next pick
(`NcThread.yield_any`, cleared at every pick), so a lower-priority ready
thread runs. The yielder is back at the pick after that: at the latest
when that thread's quantum ends (`nc_ps_slice_end` sees a higher-priority
ready thread). With no other thread ready nothing changes (the pass still
runs: it polls timers and waits). Zero-timeout waits and Sleep(0) keep the
old rule.

## After

| Run | Runner | Driver | Result |
| --- | --- | --- | --- |
| Sweep 1, run 10 | before the fix (`9dcfd184...`) | Qualcomm system | no frame after frame 2401, exit 6 |
| `dronez-old-turnip` | before the fix (`9dcfd184...`) | Turnip | no frame after frame 2403, exit 6: the hang isn't the driver's |
| `dronez-new` | with the fix (`a51fc134...`) | Turnip (PurpleVK 26.3.0-devel) | 150 s to the limit (exit 5), 59.9 fps; frames 1800 (nickname keyboard), 3600 (in a level, "Continue"), 5400 (its pause menu, from the scripted Start) |

Logs and frames: `build/logs/xboxrun/2026-11-16-fixes/dronez-new*`.

## Not tested

- In the app, with a controller; anything past the first level's start.
- How the Xbox's own kernel schedules after `NtYieldExecution` (NT 5's
  behaviour assumed).
- Other titles that poll with `NtYieldExecution`: none other is known;
  sweep 2 reruns the set with the fix.
