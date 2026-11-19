# TransWorld Snowboarding: square shadows, and stutters in its replays

**Title:** TransWorld Snowboarding (USA), `49470004` `IG00403A`.
**Image:** `TransWorld Snowboarding (USA).iso` on the Odin2's SD card,
3,947,692,032 bytes.
**XBE:** `default.xbe`, MD5 unchecked.
**Device:** AYN Odin2 (QCS8550, Adreno 740), Android 13,
`Odin2_V1.0.0.380_20250815_111346_user`.
**GPU driver:** PurpleVK 26.3.0-devel (git-62ac221a33), Turnip, the app's
custom package (`vk: driver "PurpleVK public driver" (id 18)`).
**Playbook:** `49470004_20261117-193509`, recorded by the owner on
2026-11-17 19:35 from a state (the Bavaria slopestyle course at night), 8515
frames. Its replays do not follow the recording (every checkpoint differs
from the first, at frame 600): a separate bug, not looked at here. Good
enough for this one: the rider is on lit snow for most of it.
**Evidence:** `build/logs/tws-shadow/` (`shadow2`, `fixed1`, `dbg1`,
`dbg2`, `kt1`: each run's logcat and screenshots; `shadow2` also the frame
dumps), `build/logs/playruns/` (the stutter fix's runs), the runs'
checkpoint pictures in `build/playbooks/49470004_20261117-193509/`.

## Seen

The owner, 2026-11-17: at night under the stadium lights the rider should
cast a shadow from each light (several soft shadows around the feet, like
players' shadows on a floodlit pitch). Instead each light drew a dark
square on the snow around the rider, the squares overlapping, with nothing
of the rider's shape in them.

## How the game draws them

From a replay on the APK installed 2026-11-17 19:13 (SHA-256 `324de838...`),
with `debug.hakux.psframe`, `rtdump` and `pixhist` armed at frame 1165
(run `shadow2`):

- Three shadow maps a frame, one per light: 128x128 **F24S8** depth
  targets (surface format `0x2f`, linear, pitch 512; ids 473, 483, 484 in
  that run). The rider is drawn into each from its light, depth only.
- Then the snow under the rider is drawn again three times (n = 38, 180, 54
  vertices), each with stage 0 the light's map as a shadow buffer
  (PROJECT3D, `SHADOWFUNC` LESSEQUAL), `r0 = v0 * t0.a`, blended
  `DESTCOLOR x ZERO`: the frame darkens where the compare says the snow is
  behind the rider. The receivers' texture coordinate is `oT0 = v0 * c100..c103`.
- Not stencil, not a colour texture: hardware shadow buffers, in register
  combiner draws (`shadow draw: stages=1 fmt=3 func=3 modes=00002`).

## Why: two cuts of the depth range

A float depth target (CONTROL0 bit 12) holds the depth itself: D3D's range
is 0 to 1e30 for F24 and 0 to 511.9375 for F16. The Vulkan image holds it
over that maximum (`pbdraw.c` `zfloat_scale`), and the compare multiplies
the stored value back up (`psh.c` `shadow_sample`, `k_max`).

1. **F24 read at F16's range.** `shadow_fmt` (`vk/draw.c`) gave F24 and
   F16 one code, 3, and `k_max[3]` is 511.9375. The rider's stored depths
   (about 3e28, 0.03 of 1e30) came out near 15 and the cleared texels at
   511.9375.
2. **r clamped to 1e7.** The receiver's r / q goes through `nvproj`
   (`ff.h`), which keeps texture coordinates within +-1e7 (a guard for
   sampling), then to the format's maximum.

Before: r was cut to 1e7, then to 511.9375, so `depth <= r` held for
every texel, the cleared ones included: each map's whole square in shadow.
With (1) fixed alone (APK `a46f1441...`), the stored depths were right
(3e28, cleared 1e30) but r was still 1e7: the compare failed for every
texel, the rider's included. No squares, and no shadow at all. Debug view
17 (`debug.hakux.psdbg=17`: red stored depth, green r, blue the result)
showed it in run `dbg1`: each map's square red, the rider's outline dark
inside it, blue nowhere (`dbg_f1005.png`, `dbg_f1236.png`).

## Fix

- `hw/xbox/hle/vk/draw.c` `shadow_fmt`: F24 has its own code, 0 (the one no
  shadow stage used); D24 1, D16 2, F16 3 as before, so no other title's
  shader key changes. -1 now means not a depth surface.
- `hw/xbox/hle/shader/psh.c` `shadow_sample`: code 0 compares at 1e30, with
  r its own r / q, not `nvproj`'s clamped one. D24, D16 and F16 generate the
  same GLSL as before (the SPIR-V cache is keyed by the GLSL text, so they
  keep their cached shaders).
- `tests/hle/test_psh.c`: F24's range and unclamped r, F16's range and the
  D24 text unchanged. `test.bat`: 26 of 26 pass.

Left as it was: D24's r is also cut at 1e7 while its range goes to
16,777,215, so a D24 receiver in the far 40% of its range compares against
1e7. That could hide some shadows in NGB, PGR2 or Fable (D24 shadow
buffers); changing it changes their pictures, so it waits for a test on
those titles (TODO.md).

## On the device

Each run replayed `49470004_20261117-193509` on screen with the automated
testing banner, from its state, and ended with `playbook.bat end`. Runs
drift from the recording and from each other, so frames are compared by
what they show, not pixel for pixel.

| Run | APK (SHA-256) | What it showed |
| --- | --- | --- |
| `20261117-193832` (the owner's replay) and `shadow2` | `324de838...`, before | Dark squares around the rider, one per light (`shadow2/rt_1410_215_640x480_f44_b4.png`, `screen.png`) |
| `fixed1`, to frame 3057 | `a46f1441...`, F24 range only | No squares, no rider shadow either |
| `dbg1`, debug view 17 at 1005-1543 | `a46f1441...` | Each map's square red (lit), the rider's outline dark, blue (shadow) nowhere |
| `dbg2`, debug view 17 at 977-1459, normal view at 1566-1895 | `8a9167e1...`, both fixes | Blue in the rider's shape only, two lights' shadows at once (`dbg_f1064.png`, `dbg_f1190.png`); in the normal view soft rider-shaped shadows beside him, no squares (`normal_f1697.png`, `normal_f1895.png`) |

Under the board, where it touches the snow, the debug view shows the
compare failing (no contact shadow): the game's own bias, read as lit; not
compared with a console.

Not checked: a console or a reference capture side by side; other courses
and day levels; other titles with F24 shadow buffers (none known yet).

## The ~120 ms stutters: in playbook runs only (fixed)

**Seen:** the replays above stuttered every second or so, frames shown
~117 ms apart (`guest flipped ~100 ms after its vblank, ready ~24 ms
before`: the frame drawn on time, the game's flip ~100 ms late), many right
after the game opened a trick sound from its cache partition
(`Score_Point_1.wav`, `Score_Saving_Sound.wav`). The owner's own
screenshots (no automated-testing or recording banner, so normal play, as
far as they show) have the stutter count unchanged for the 14 s of riding
between them. Normal play was not captured here.

**Why:** a kernel-call trace from the first `Score_Point_1.wav` open (run
`kt1`, `NC_TRACE_KCALL_AT=Score_Point_1`) shows the loader thread read the
file, then call `KeQuerySystemTime` from 0x1E160C over and over (510 calls
before the log dropped lines): a wait for a moment by the clock. In a
playbook the clock moves only with the vblanks the guest takes
(`detclock.h`), and the vblank is held for the title's own point
(`detevents.h`): every thread waiting, or a spin of one unchanging state.
A thread polling the clock is neither (each read is a kernel call and a new
value), so the vblank came only from the 100 ms fallback: each forced
vblank's report lists the last system ops as one thread's `k128`
(KeQuerySystemTime) over and over (t152, t8). The runs had 23-37 of these
~100 ms late flips each and fewer than 100 forced vblanks (none logged its
100th). In normal play the clock is the host's and the wait just ends.

**Fix:** `native/kernel/detevents.c`: a thread's 64th clock read in a row
(KeQueryInterruptTime, KeQueryPerformanceCounter or KeQuerySystemTime, no
other system op between; `detevents_calc.h`), with a vblank due and the
decoder quiescent, gets the vblank: its line is served on the way back
from that read, so the next read sees the new floor. Counted in the
guest's reads, not host time: the same point every
run, where the fallback's was wherever 100 ms ran out. Logged at the first
and every 1000th (`events: vblank given to tN polling the clock`). Only
while a playbook plays or records: normal play, and the input path, are
unchanged. Host test `test_detevents`; `test.bat`: 27 of 27 pass. APK
`646a5e639e46129725d999a08c17974686357c13b08ae8bf8f99bef15ee260be`.

**On the device** (the playbook to its end, twice, `playbook.bat play
--wait`; logcat per run in `build/logs/playruns/49470004_20261117-193509/`):

| Run | APK | Frames | fps (5 s counts) | Stutters | >= 80 ms | ~100 ms late flips | Forced vblanks |
| --- | --- | --- | --- | --- | --- | --- | --- |
| `fixed1` (before) | `a46f1441...` | to 3102 | mean 52.3, min 46.7 | 184 | 67 | 37 | under 100 |
| `dbg2` (before) | `8a9167e1...` | to 1991 | mean 50.9, min 44.6 | 137 | 54 | 33 | under 100 |
| `poll-01` | `646a5e63...` | 8516 (all) | mean 59.6, min 57.7 | 346 | 2 | 0 | 0 |
| `poll-02` | `646a5e63...` | 8516 (all) | mean 59.4, min 57.8 | 548 | 1 | 0 | 0 |

The clock-poll vblank fired from the start of each run (`vblank given to
t152 polling the clock (k128 ...)`, t152 being a thread the old forced
reports named). Left in `poll-01`: one 175 ms frame at frame 258 (the load
into the run) and one 80 ms late flip at 1030. The other stutters (25-40
ms, `UI missed 1 front frame`, at a similar rate before and after: one in
24 frames in `fixed1`, one in 31 and one in 18 in `poll-01` and `poll-02`,
by the stutter monitor's frame count) are the display side, a separate
lead in TODO.md.

The two runs still differ from each other from the first checkpoint
(mean pixel difference 35.2 at frame 600, 15.6-105.9 after), as two runs
before the fix did (`fixed1` and `dbg1`: 31.4 at 600): TWS's replays drift
for another reason, not looked at.

**Soulcalibur II, for regressions** (`4E4D0003_idle-state`, the
determinism record's no-input playbook, twice, same APK; pictures in
`build/logs/playruns/4E4D0003_idle-state/`): both runs ended at frame 1802,
their 7 checkpoints the same as each other (mean pixel difference 0.0,
1.0 at frame 1200), 0 forced vblanks, and the new rule never fired (no
`polling the clock` line; its first use always logs). Against the
determinism record's `usb-01` (2026-11-17, APK `324de838...`): the same to
frame 900, 0.8 at 1200, 29.4 at 1800, where today's fight clock reads
0'14"46 and that run's 0'14"43, two game frames apart. The rule did
nothing in this game, so that comes from elsewhere: the record's
"not done" list (disc completions, kernel timers on the host's clock) or
today's renderer build; not looked at. (A replay's stopped pull left
`build/playbooks/4E4D0003_idle-state/` with a partial copy of the device's
5.1 GB folder for this playbook, the library copy untouched.)

None of today's captures show the determinism's startup lines (`lockstep
on`, `events: the vblank when the title waits`), on yesterday's APK as on
today's: they come before `logcat -T 1` attaches.

## Also seen, not fixed

- **Replays drift from the recording** from the first checkpoint (frame
  600), the owner's "did not record correctly". Not looked at here.
- **Debug view colours**: a red (1, 0, 0) output shows as (234, 51, 35) in
  `adb screencap` (why was not looked at); read the channels as high or
  low only.
