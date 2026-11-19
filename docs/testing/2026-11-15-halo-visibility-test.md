# Halo: Combat Evolved closes the app loading a saved game

**Title:** Halo: Combat Evolved (USA) (Rev 2), `4D530004` `MS00409A`
(certificate version 9).
**Image:** `Halo - Combat Evolved (USA) (Rev 2).iso` on the Odin2's SD card,
3,625,713,664 bytes, the same size as the owner's `MS00409A.iso`.
**XBE:** `default.xbe`, 2,535,424 bytes, MD5
`7F6CC2900594C9E46C927D2D304B43DA`, read from the XISO on the PC and from
the same sectors of the Odin2's image (both match).
**Device:** AYN Odin2 (QCS8550, Adreno 740), Android 13. **GPU driver:**
in the app, PurpleVK 26.3.0-devel (git-62ac221a33), Turnip, the app's
custom package. **Headless, corrected 2026-11-16:** Qualcomm's system
driver (`vk: driver "Qualcomm Technologies Inc. Adreno Vulkan Driver" (id
8)` in both `run.out` files). The custom driver was copied to the runner
and passed as `vkdriver=<path>`, which `xboxrun` ignored without a word
(it wanted `DIR|LIB|HOOKS`; fixed 2026-11-16, see TESTING-GAMES.md). The
very dark headless frames below may be the system driver's, not the
capture's: not rerun yet.

## Before

**In the app** (the owner, 16:59, capture
`build/logs/xbdm/2026-11-15_16-59-08.483395.android.log`): Halo boots with
its map cache already on the emulated hard disk and reaches the main menu
at about 28 fps. One A press resumes the solo campaign (`last_solo.txt`,
`savegame.bin`); 2.3 s later the whole app closes:

```
[005CBB49] mm: contiguous allocation of 4 KiB failed (#1; free 1048 KiB: 19a6000+1048K | 1 runs, largest 1048 KiB)
[005DDB21] hakuX-native-frontier: host fault: the title doesn't handle it at guest 0000000c, eip 0018110f
Fatal signal 11 (SIGSEGV), code 2 (SEGV_ACCERR) in tid 9700 (SDLThread)
```

**Headless** (`xboxrun`, pre-fix core, the app's Halo save and map cache
copied to the runner's disk, `NC_PAD_SCRIPT` A every 45 frames from frame
300): the same failure and the same fault, exit 3 (host fault), 17.6 s
after start. Logs in `build/logs/xboxrun/2026-11-15-halo/before/`.

Without the save, headless Halo spends its run building the map cache, then
starts XDemos (its attract mode). Without presses it never loads a game.

## Diagnosis

The fault is in Halo's D3D (the `D3D` section, 0x180E40):

- 0x181080, `BeginVisibilityTest(index)`: the page of results for
  `index >> 8` is made on first use with
  `MmAllocateContiguousMemoryEx(0x1000, 0, 0xFFFFFF, 0, PAGE_READWRITE)`,
  a page below 16 MiB. If that fails it returns without one.
- 0x1810F0, `GetVisibilityTestResult(index, ...)`: reads
  `[page + (index & 0xFF) * 16 + 0xC]` without checking the page: NULL +
  0xC.

Halo lays out its memory at fixed physical addresses at start (its
allocator at 0x1565A, called from 0x32E40): 0x345000 bytes at 0x61000, then
22 MiB at 0x3A6000 (its tag space, ending at 0x19A6000), then 22 MiB and 4
MiB anywhere. Every request bounded below 16 MiB lands under 0x61000. There
the emulated kernel held 0x0-0x60000: the first 64 KiB (never handed out),
a 256 KiB kernel area at 0x10000 of which its layout uses 0x13000, and the
kernel pool's first 64 KiB at 0x50000. One page was left, 0x60000: the first
visibility page took it, the second failed.

Upstream hakuX has the same layout (its commit `c2d3d3864c` put the pool at
0x50000 for Halo's sake), so this is not a HakuXP regression. Upstream's
v1.3.0 was not run here.

## Fix

`NC_KERNEL_SIZE` 0x40000 to 0x20000 (`native/kernel/kernel.h`): the kernel
area is 0x10000-0x30000, the pool's first range 0x30000-0x40000, and 33
pages (0x40000-0x61000) are left below Halo's block. Static asserts in
`kernel.c` keep the layout inside the area, the pool's base a multiple of
its size (a savestate's pool is found by rounding its process object's
address down) and at least 32 pages under 0x61000. Savestates restore page
ownership from their own record, so a state saved by an older build keeps
its layout; that path was not run.

A failed contiguous request now logs who asked and for what range
(`native/kernel/mm.c`, `request_text`), e.g. `of 4 KiB from 001810b3 in
00000000-00ffffff (0 KiB free there) failed`.

## After

Headless, fixed core, same disk contents (restored from the app's copy),
same pad script: no failed allocation; the saved game loads 17.5 s in
(`last_solo.txt` written) and runs to the 90 s limit (exit 5, timeout) at
30 fps (28.3 in the 5 s holding the load), 6 stutters in the whole run.
Frame 1500 is in the level, an interior with lit windows, but very dark:
it has to be brightened 8 times to see. Logs and frames in
`build/logs/xboxrun/2026-11-15-halo/after/`.

| Run | Core | Result |
| --- | --- | --- |
| App, owner | HakuXP before the fix | App closes 2.3 s after resuming the save |
| Headless | HakuXP before the fix | Host fault, exit 3, 17.6 s |
| Headless | HakuXP with the fix | Runs in the level to the 90 s limit, 30 fps |
| App, owner | HakuXP with the fix | Save loads, played 4.5 minutes, 29.8 fps average |

**In the app with the fix** (the owner, 18:35-18:40, debug APK
`18c430dd`, capture `build/logs/xbdm/2026-11-15_18-35-41.574818.android.log`):
the save loads and plays; ended by Exit to library after 285 s. Frame rate
26.3 min, 29.8 average, 30.0 max (50 samples); 20 stutters, 1 hang (0.51 s
at frame 2, at boot) and 1 dropped frame. The stutters: eight 50 ms frames
in the first seconds (a swap just after the vblank), the save's load (498
and 214 ms), and three 67 ms frames in play that the overlay calls `guest
CPU-bound: title code at 000BB0DF`. That address is Halo's spin-wait for
its frame tick (a count an interrupt advances), so those are late ticks,
not CPU load. The light sources render wrong (an overbright yellow shaft
in the cryo bay); the level isn't dark in the app.

The owner's point: before the fix, a fault in the title closed the whole
app (to the home screen), not just the game. That is still the case for
any other title fault (TODO.md, Debugging aids).

## Not tested
- Upstream hakuX v1.3.0 (`com.rfandango.haku_x`, installed) on the same
  save: expected to close the same way, not run.
- Why headless frames of the level come back so dark when the app's
  picture isn't (the capture path, or the headless picture).
- A savestate saved by a build before the fix.
- Other titles: the change gives every title 180 KiB more low memory.
  Nothing else was rerun.
