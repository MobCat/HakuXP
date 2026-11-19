# Halo's attract mode stutters: the vblank ran at 61.5 Hz

**Title:** Halo: Combat Evolved (USA) (Rev 2), `4D530004` `MS00409A`
(certificate version 9).
**Image:** `Halo - Combat Evolved (USA) (Rev 2).iso` on the Odin2's SD card,
3,625,713,664 bytes.
**XBE:** `default.xbe`, MD5 `7F6CC2900594C9E46C927D2D304B43DA` as read from
this image on 2026-11-15 ([record](2026-11-15-halo-visibility-test.md));
not reread today.
**Device:** AYN Odin2 (QCS8550, Adreno 740), Android 13, 60.00 Hz panel.
**GPU driver:** the app's custom package, Turnip T30 (Mesa
26.3.0-T30-1.4.359, `vulkan.purple.so`) installed. The run's own
`vk: driver` line had rotated out of logcat: unchecked for this run.
**APK:** debug `ef33904e1bd84aebe35667ece3650bf7a90e7509c32983993147435727b905cd`
(installed 08:53:53).
**Evidence:** `build/logs/halo-stutter/` (`2026-11-17_0905_logcat-all.txt`,
the device's logcat from 08:58:25 on; `2026-11-17_0907_dumpsys-surfaceflinger.txt`;
`2026-11-17_0909_gfxinfo-native-framestats.txt`), screenshots
`build/screenshots/2026-11-17_res-delta-early.png`, `_res-delta-5m.png`,
`_halo-stutter-0905.png`.

## Before

Started from the PC at 08:53:53 and left on its main menu (the owner).

| Time | Where | Overlay |
| --- | --- | --- |
| 08:54:44 | main menu | `stutters 29 \| hangs 1 \| dropped 0`, FPS 30 |
| 08:59:44 | XDemos (attract mode) | `stutters 311`, last 49 ms, `FPS: 29 \| 34.2ms avg / 48.8ms last`, `Target: 30 fps (60 Hz)` |
| 09:04:53 | main menu again | `stutters 586`, last one 1m03s ago, FPS 30 |

From the kept logcat (08:58:25-09:05:01), 322 stutter lines, all in the
demos:

- Every one is a frame shown 48.0-49.1 ms after the one before (mostly
  48.7-49.0), with 0 dropped. Three vblanks of 59.94 Hz would be 50.05 ms:
  48.8 ms is three of 16.27 ms.
- Reasons: `late swap: 0 ms before its vblank, render + GPU 2 ms` and
  `guest flipped 15 ms after its vblank, ready -1 ms before`. The frame
  became ready (swap, slot written, GPU done) within a millisecond after its
  second vblank, so it waited for the third.
- One stutter every 19 frames (median; mostly 14 to 26 apart).
- The frame rate counted every 5 s (`hakuX-hle-fps`): 30.7-30.8 fps on the
  main menu, 29.9-30.0 in the demos. A console at 59.94 Hz flipping every
  second vblank can't go above 29.97.
- No compiles, no disc reads, no GPU stalls in the explained frames; the
  render thread and GPU took 1-2 ms a frame.

## Diagnosis

**Not a movie, not the CPU, not the GPU: the emulated vblank ran 2.6% fast,
locked to a vsync that wasn't the panel's.**

- XDemos is Halo playing a recorded game in its own engine (the 5-minute
  screenshot shows the HUD on Assault on the Control Room), not a Bink
  movie.
- The NV2A's vblank (`native/dev/nv2a.c`) locks to the panel's vsync, as
  the app's Choreographer reports it, when a whole number of the panel's
  periods comes within 5% of a frame. Its period estimate, in the
  `nv2a: the presenter picks frames up ...` lines, wandered from 16.05 to
  16.80 ms; 26 of the 55 lines say 16.26-16.29 ms (61.5 Hz). That passed
  the 5% test (2.5% off), so the vblank ran at about 61.5 Hz.
- The vsync wasn't the panel's. `dumpsys SurfaceFlinger` with the screen
  on (`powerMode=On`, 60.00 Hz, `VSYNC period: 16666666 ns`) showed
  `mScreenAcquired=0` and `app: state=SyntheticVSync ... synthetic`. In that
  state, SurfaceFlinger's app vsync thread sends a vsync each time a 16 ms
  timer runs out, stamped with the time it woke, the way it does while the
  screen is off. The game process's own UI frames (`dumpsys gfxinfo
  ...:native framestats`) have vsync times 502.9, 508.4 and 497.6 ms apart:
  not whole multiples of 16.667 ms. Why SurfaceFlinger was in that state is
  the device's business (it has two displays, the second one inactive);
  what matters here is that we believed it.
- What that does to Halo: on the menu it flips every second vblank, which
  came to 30.75 fps (the 30.7-30.8 counts). In the demos the game keeps its
  own 30 Hz clock (the 29.9-30.0 counts), a frame every 33.33 ms against a
  32.53 ms two-vblank slot. Each frame falls 0.8 ms further behind until one
  misses its slot and waits a third vblank (48.8 ms). Thirty frames a second
  need 1.5 of those (30 x 32.53 + 1.5 x 16.27 = 1000 ms), one every 20
  frames, as the log has it.
- **The stutter monitor was right.** Frames really reached the screen 48.8
  ms apart; it wasn't misjudging a movie's cadence. Its reasons describe each
  frame correctly but not the cause, and the overlay's `Target: 30 fps (60
  Hz)` shows the video mode's rate, not the 61.5 Hz the vblank ran at.
- This is also why every title paced by vblanks ran 2.6% fast on the Odin2
  while SurfaceFlinger was in that state.

## Fix

- `native/dev/vsynctrack.c` (new, pure): the panel's period, plus how far
  each vsync time falls from the period's grid (smoothed). A real panel's
  times come from SurfaceFlinger's model of the hardware vsync and sit on
  the grid; a timer's do not. The times count as regular below 50 us, and
  as irregular again above 100 us.
- `native/dev/nv2a.c`: the vblank locks to the panel only while its times
  are regular **and** a whole number of periods is within **0.5%** of a
  frame (was 5%). 60 Hz and 120 Hz panels still lock for the 59.94 Hz modes,
  50 and 100 Hz for 50 Hz ones. Otherwise the vblank runs free at the mode's
  rate. Each change is logged (`nv2a: the vblank follows the panel: ...` /
  `nv2a: the vblank runs free at 59.94 Hz: the panel's vsync times are X ms
  off their grid ... (Android's synthetic vsync?)` / `... makes no frame`),
  and the presenter line now shows the jitter (`period P ms, J ms off its
  grid, irregular`).
- Host test `test_vsynctrack`: a real 60 Hz panel with late callbacks, a
  pause and +-20 us wobble (locks); 120 Hz (2 per frame); the synthetic
  vsync, 16 ms plus 50-600 us (doesn't lock) and a steady 16.3 ms timer
  (regular, but not a frame: doesn't lock); a real panel, then synthetic,
  then real again (releases, then locks again); the 0.5% edges. `test.bat`:
  24 of 24 pass.
- Build: debug APK
  `e74808dabd1dd80fbf82fc5b4eccf58958fcba73c9fecb87df9520c85ca4b86f`
  (`build.bat`, 2026-11-17 09:13).

- Then a log-only change (09:30): the vblank's first decision at boot is
  made before the vsync is measured, and logged `runs free ... 1.000 ms
  off their grid ... (Android's synthetic vsync?)` from the tracker's start
  value. Now it says nothing until 64 intervals are in
  (`nc_vsync_measured`). Built as APK
  `983fd9d863e880e2f410e7fd9bc9d95bef801519362d6f41d288dca1cea34e1c`, not
  installed. 24 of 24 host tests pass.

## After

APK `e74808da` installed at 09:19:02 with the owner's OK. The Odin2 then
slept on its timeout (09:19:07) and woke, and **SurfaceFlinger was back on
the panel's vsync**: `mScreenAcquired=1`, `app: state=VSync`
(`build/logs/halo-stutter/2026-11-17_0921_dumpsys-surfaceflinger.txt`). So
this run shows the fix's lock to a real panel, not its refusal of a
synthetic one; that half rests on the old log and the host test.

Halo from boot with no presses, on screen with the automation banner:
playbook `playbooks/4D530004_MS00409A/4D530004_attract-idle.txt` (run
`20261117-091953`, ended at frame 12601, 6 checkpoints, pictures in
`build/playbooks/4D530004_attract-idle/20261117-091953/`: the menu, a
single-player demo, two split-screen demos, the menu, the snow level).
Capture `build/logs/xbdm/2026-11-17_09-19-26.520680.*`. Driver line:
`vk: driver "PurpleVK public driver" (id 18) PurpleVK 26.3.0-devel`.

- `nv2a: the vblank follows the panel: 1 vsync of 16.665 ms a frame` 0.8 s
  after the start. The panel's times sat 0.002-0.003 ms off their grid for
  the rest of the run (the synthetic vsync's period had wandered 16.05-16.80
  ms).
- The menu now runs at 30.0 fps (was 30.7-30.8): the vblank is 60 Hz.
- The late frames are 50 ms (three vblanks of 16.67 ms), not 48.8.
- In the demos: 88 three-vblank frames in 199 s (0.44 a second), then 31
  in about 107 s (0.29 a second), against a steady 1.5 a second before.
- Boot: `HANG #1` 0.53 s at frame 3, as usual (not the 20 s one).

**What is left, and why.** The remaining late frames come in bursts of 3
to 17, one burst every 14.3-15.5 s (about 456 frames). A burst every 456
frames is the frames' pace and the vblank's differing by 0.11%: that is
Halo's 29.97 fps (a 59.94 Hz console's every other vblank) against this
panel's 60.006 Hz (16.665 ms, as measured). Each time Halo's frame times
drift across a vblank, their jitter of a millisecond or so puts some
frames before it and some after for half a second. Halo paces its frames
for a 59.94 Hz display, and only a vblank at 59.94 Hz keeps in step with it;
locked to the panel, ours runs 0.11% fast. Not fixed here (TODO.md).

## Not tested

- The fix refusing a synthetic vsync on the device (the `runs free` line
  after measuring): SurfaceFlinger had left that state before the run.
  Shown by the old log's numbers and `test_vsynctrack` only.
- The log-only change was seen later in APK `945f8a6d` (09:36, BUILDING.md
  "The playbook banner's ETA"): Halo's boot logged only `nv2a: the vblank
  follows the panel: 1 vsync of 16.665 ms a frame`.
- What in Halo keeps 29.97 Hz (the count at 0x1F8C80, TODO.md "Spin-waits
  read as CPU-bound stutters", and which interrupt advances it).
- Halo's demos with the vblank free at 59.94 Hz beside the 60 Hz panel.
- Other devices (the RP5): that the vblank still locks there.
- Why SurfaceFlinger went synthetic with the screen on; a sleep and wake
  cleared it this time.
- Halo in play (2026-11-15: 20 stutters in 285 s, probably with real vsync
  then; not checked).
- 50 Hz titles, 90/120/144 Hz panels: host test only.
- **The 20 s hang at Halo's boot** (about 08:42, the second lead): that
  boot's native lines had rotated out of logcat by 09:02, so it couldn't be
  read. It needs `debug.bat` running from the boot (TODO.md).
