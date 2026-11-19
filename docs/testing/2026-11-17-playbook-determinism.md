# Playbook replays that don't repeat: Soulcalibur II

**Title:** Soulcalibur II (USA), `4E4D0003` `NM00301A`, certificate
version 2049.
**Image:** `Soulcalibur II (USA).iso` on the Odin2's SD card (not hashed on
the device). **XBE:** MD5 `8ECABC50DE1E14D10AC73141424D5761`, read from
`W:\Microsoft\Xbox\NTSC-U\Soulcalibur II (USA)\NM00301A.iso` (the device's
copy unchecked).
**Device:** AYN Odin2 (QCS8550, Adreno 740), Android 13, Turnip (the app's
custom driver).
**APKs (debug), installed on the Odin2:** `8acd6732...` 13:25 (input sync),
`4665227c...` 13:37 (and the deterministic clock), `20d0036d...` 14:07
(and the call tracer); in the evening (each before the runs named):
`f9630d95...` (packaged the old core: `build.bat apk` doesn't rebuild it;
run stopped), `6fffd18d...` (the vblank in a spin; `spin-01`),
`316a6d7d...` (the 100 ms in the guest's time; `spin-02`, `spin-03`),
`7b5847ba...` (`NC_TRACE_MEM`; `mem-01`, `mem-02`, `rand-01`,
`rand-02`), `0db37ade...` (the GPU in lockstep; `ls-01`, `ls-02`),
`d2b1080e...` (a save under the lockstep; `ls-03`, `ls-04`); then
`2ff36075...` (the APU in lockstep: hung, see 9), `a005ebf3...` (`apu-01`,
`apu-03`), `81a1eb07...` (the APU held from the load; `apu-04`, `apu-05`),
`b201eca2...` (`NC_TRACE_MEMDUMP`; `apu-06` to `apu-09`), `324de838...`
(the controller's frames all on the vblanks; `usb-01`, `usb-02`, and the
owner's recording `in-01`, `in-02`).
**Playbooks:** the owner's `4E4D0003_20261116-230239` (recorded 2026-11-16
23:02 from a state at the start of Arcade stage 1, Talim against Taki, 833
input changes to frame 3425), and `4E4D0003_idle-state` (written for this:
the same state, nothing pressed, checks to frame 1800, saves at 300-1200).
Both in `playbooks/4E4D0003_NM00301A/`.

## Before

The owner's recording, replayed once on 2026-11-16 (run `20261116-230350`):
all 5 checkpoints differ from the recording (difference 21-39; 8 or less
counts as the same). At frame 600 both show the fight clock at 0'02"70
but Taki lying differently; by frame 1200 the replay's round is over
("PERFECT") while the recording's goes on.

## Diagnosis

1. **The game itself, with no input** (`idle-01`, `idle-02`, the build of
   2026-11-17 morning): the pictures are the same to frame 900 ("BATTLE 1
   FIGHT!!", difference 0.0) and differ at 1200 (25.4) and 1800 (19.4): Taki
   does something else. So the drift isn't the input alone.
2. **The input's timing:** a change reached the title through the USB
   controller's own 1 ms frames on a host thread, its frame number the GPU
   decoder's swap count (behind the title's CPU by a varying amount): a
   press could land a frame early or late.
3. **The clocks:** the title reads RDTSC through XAPI's
   QueryPerformanceCounter (0x103A75, about 50 callers) and the kernel's
   KeQuerySystemTime, both the host's clock.
4. **Where the game parts ways** (after 2 and 3 were fixed; `NC_TRACE_EIP`):
   the title draws random numbers from its own Mersenne Twister (state at
   0x372DF8, index at 0x193C60, seeded once from the C runtime's rand(),
   state at 0x3706F0; drawn through 0x1BFD0, 110 callers), all on its main
   thread t8. Two no-input runs drew the same numbers from the same callers
   for 351 vblanks (5.9 s); from vblank 352 the caller at 0x60C77 draws on
   some frames in one run and others in the other. Its draw is gated on
   float compares of an object's fields and a squared distance between two
   positions (0x12BC0): positions had already drifted, without a random
   number being involved.
5. **Why positions drift:** QueryPerformanceCounter traced per vblank. The
   main thread's calls from 0x343DE (a time-stamped event, about 20 a
   frame) split differently across vblanks from the start (vblank 3: 32 vs
   48). The vblank interrupt comes from the host's timer, so when the game
   is still busy at a vblank, the interrupt and the clock's next step land
   at another point of its frame, and what it reads mid-frame differs.
6. **The vblank at the game's own point made it run at 8 fps** (the owner:
   "its running at 8 fps now"). The first version gave the vblank only
   when every guest thread waited. Soulcalibur II never waits: its D3D
   spins at 0x152850 reading the GPU's progress, while the GPU (the
   decoder) waits in FLIP_STALL for that very vblank. Every vblank came
   from the 100 ms fallback. Fix: a vblank is also given when two
   interrupt checks in a row (the clock's, ~1 ms apart) find the same
   thread at the same EIP with the same registers and flags and no system
   op between them: any iteration of such a spin is the same guest state.
   A save also forced one (the 100 ms counted host time while the game was
   held); now it counts the guest's time.
7. **Its random state parts from the start** (`NC_TRACE_MEM`, page hashes
   of 0x14E000-0x654000 each vblank, `tools/native/memtrace.py`): two
   runs' C runtime rand state (0x3706F0) and Twister differ from vblank 6.
   The number of draws per caller stayed equal for 344 vblanks, so it
   showed up only when a value changed a decision (0x60C77). Who draws
   (`NC_TRACE_EIP=145a40`, rand): 0x1C100 stirs the random streams (rand
   and four more generators, 0-3 draws each), called from the main loop's
   frame wait at 0x29F90: `while (pending(0x38FAEC) > 1) stir();`. A
   frame is marked done by a D3D callback (InsertCallback 0x1524D0, the
   callback at 0x32850) that runs when the GPU reaches it in the push
   buffer. So the game stirs for as long as our decoder thread takes to get
   there: host time (46 vs 70 stirs on vblank 6, 535 vs 1010 on 7).
8. **What's left: the sound** (after the lockstep below). Both pairs of
   runs part at vblank 1071 (17.8 s): one run draws a random number from
   0x141032 there and the other doesn't. That's the game's sound engine
   setting up a voice (pan tables, then `rand() % range` for its pitch).
   The audio hardware runs on the host's clock, so when a voice ends, and
   what the engine starts next, moves between runs. The memory pages that
   differ from vblank 1 (0x608000, 0x636000, 0x63B000, 0x38F000) turned
   out to be the controller's (12).
9. **The APU in lockstep hung the game at its first frame** (the owner:
   "emulator has hung for longer then 1m"). The first version raised the
   APU's interrupt after each of the game's APU register writes, as the
   frame thread does at each frame. DirectSound halts the sound front end
   (FECTL) around its own changes, and the device model raises the trap
   interrupt for a halted front end too: raised at the halting write, it
   came back at each of the handler's FECTL writes, at the APU's IRQL
   (21), for good; the vblank's interrupt (lower) never got in, and the
   GPU waited for its flip. Now only a write that trapped a method raises
   it at once.
10. **The runs began an audio frame apart:** two counters (0x38F4E4 by 256
    samples, 0x6087DC by 1) differed in the frame-300 saves. The APU's
    frame thread ran a frame or none between the state's load and the
    lockstep's start; the lockstep now begins before the load.
11. **`NC_TRACE_MEMDUMP=600`** (the traced range's bytes at vblank 600):
    2 words of 5 MB differed: 0x38F4E4 by 512 and 0x6087DC by 2. 0x6087DC
    is in XPP's data (the controller driver), next to its USB register
    reads at 0x1748B7; 0x38F4C0 is the game's input block: the
    controller's packet count.
12. **The controller's own clock:** in a playbook the USB frames run on the
    game's thread at each vblank, but the controller's own thread took
    over after 50 ms without one: before the first tick, and in each
    saved state's hold (250 ms), on the host's clock, so each save added
    a few frames. It no longer takes over in a playbook (the vblank comes
    within 100 ms of the game's time anyway), and HcFmRemaining reads a
    whole frame there.

## Fix (so far)

- **Input sync** (`native/dev/inputsync.c`, hooks in `native/kernel/intr.c`,
  `native/dev/ohci.c`, `hw/xbox/hle/pb/pbmode.c`, `native/dev/padplay.c`):
  while a playbook plays or records, the frame is the frames the title has
  pushed (the push-buffer scout, counted at every PUT), read on its own
  thread as it takes a vblank, and the USB controller's frames run there
  too (its own thread stands by throughout; it took over after 50 ms
  without a tick until 12 above). Each run writes `taken.txt`. Host test
  `test_inputsync`.
- **Deterministic clock** (`native/kernel/detclock.c`): RDTSC hooked where
  the title has it (FEX asks the core about each instruction it
  translates; no change to FEX, whose project asks for no AI-written code)
  and answered from a counter; KeQuerySystemTime, KeQueryInterruptTime and
  KeQueryPerformanceCounter from the same time; a vblank sets the floor
  (start + vblanks x period); each guest thread reads its own sequence
  (+256 cycles, +100 ns a read). Starts from a savestate's exact time and
  TSC, or a boot's 0 and 2026-01-01. Host test `test_detclock`.
- **The vblank at the game's own point** (`native/kernel/detevents.c`,
  hooks in `native/kernel/ps.c`, `intr.c`, `kernel.c`; the gate in
  `native/dev/nv2a.c`): while a playbook plays or records, the host's
  vblank only counts; the guest gets it when every thread waits and the
  decoder is quiescent, or in a spin as in 6 above; one however many came
  meanwhile; after 100 ms of the guest's time anyway (counted, logged).
- **The GPU in lockstep** (`hw/xbox/hle/pb/pbmode.c` `hle_pb_settle`,
  called in `native/dev/nv2a.c`): the guest's writes that let the decoder
  go on (a PUT, PGRAPH's FIFO, its interrupt cleared, a flip) wait until
  the decoder has done what they allow and stopped, so its traps and D3D's
  callbacks reach the game at the same instruction every run. A savestate
  under the lockstep pauses the guest first and parks the decoder from the
  guest's pause point (`native/state/save.c`): parked first, the game's
  next write waited 50 ms for it (3 a run) and went on unsynced.
- **The APU in lockstep** (`native/dev/apu/lockstep.c`, hooks in
  `native/dev/apu/apu.c`, `dsp/gp_ep.c`, `vp/vp.c`, called from
  `detevents.c` and `core/core.c`): its frame thread stands by and the
  game's thread runs the frames, 1500 a second of vblanks (25 at 60 Hz) at
  each vblank given; while DirectSound waits for the GP DSP (the mailbox
  spin) the frames it needs run there, counted against the next vblanks;
  XGSCNT counts the samples run; the GP's boost and the held-voice wait
  no longer read the host's clock there; a write that trapped a method
  raises its interrupt at once. Begins before the state's load. The sound
  may stutter when the game runs below its vblanks.
- **The controller on the vblanks only** (`native/dev/ohci.c`): 12 above.
- **Tools:** `NC_TRACE_EIP` (`native/kernel/calltrace.c`), `NC_TRACE_MEM`
  and `NC_TRACE_MEMDUMP` (`native/kernel/memtrace.c`, compared with
  `tools/native/memtrace.py`), `hxs.py --ram` and `--ramdiff`.

## After

| Runs (no input) | Build | Pictures | Notes |
| --- | --- | --- | --- |
| `idle-01` / `idle-02` | before both | same to 900, differ 1200 (25.4), 1800 (19.4) | |
| `sync-01` / `sync-02` (the owner's recording) | input sync | all 5 differ from each other (17.9-33.0) | `taken.txt`: all 833 changes at the same frame and vblank in both |
| `det-01` / `det-02` | + clock | same to 900, differ 1200 (18.0), 1800 (33.0) | clock started from the same TSC 174662066224878 both times; first RDTSC answered at 0x156A60 |
| `trace-01` / `trace-02` | + tracer | (same as above) | random draws the same for 351 vblanks, then 0x60C77 |
| `qpc-01` / `qpc-02` | + tracer | (same) | 0x343DE's calls split differently across vblanks from vblank 3 |
| `ev-01` | + vblank when idle | - | 8 fps: every vblank forced (100 ms) |
| `spin-01` | + vblank in a spin | - | 56-60 fps; forced only at the saves (a save's hold counted) |
| `spin-02` / `spin-03` | + guest time | same to 900, differ 1200 (28.6), 1800 (25.6) | 0 forced; random draws part at vblank 422 (0x60C77), as before |
| `mem-01` / `mem-02`, `rand-01` / `rand-02` | + `NC_TRACE_MEM` | (same as above) | memory parts from vblank 1; rand's callers (0x1C118, stir) differ from vblank 5 |
| `ls-01` / `ls-02` | + GPU lockstep | same at all 5 (0.0-7.9) | random draws the same to vblank 1070; 3 lockstep timeouts a run (the saves) |
| `ls-03` / `ls-04` | + save under the lockstep | same at all 5: 0.0, 0.0, 0.0, 0.2, 7.9 | 0 forced, 0 timeouts; saves 254-282 ms; random draws part at vblank 1071 (0x141032, the sound engine) |
| (`apu-hang`) | + APU lockstep | - | hung at frame 16 (9 above) |
| `apu-01` / `apu-03` | + trap-only interrupt | same at all 5: 0.0, 0.0, 0.0, 0.1, 7.2 | random draws the same for all 1916 vblanks traced; 16 pages ever differ, 1-9 at a time |
| `apu-04` / `apu-05` | + held from the load | 0.0 to 0.1, then 5.8 at 1800 | random draws all the same; 2 pages differ from vblank 1 |
| `apu-08` / `apu-09` | + `NC_TRACE_MEMDUMP` | 0.0 to 1200, 4.6 at 1800 | vblank 600: 2 words differ (the controller's count) |
| `usb-01` / `usb-02` | + the controller on the vblanks | **all 7 the same: 0.0** | vblank 600's 5 MB byte for byte the same; the traced range the same at every vblank of the playback but one (vblank 447, one page) |

With input, the owner's recording (`4E4D0003_20261116-230239`, 3425
frames), replayed twice with the last build (`in-01`, `in-02`): all 5
pictures the same as each other (0.0; both still differ from the
recording itself, made before all this), all 833 changes taken at the same
frame and vblank, random draws the same at every vblank, 60 fps
throughout, nothing forced. In memory, one page (0x62C000) differs from
vblank 278 on, and blocks of buffers (0x528000-0x542000) for 4-5 vblanks
at a time: likely the disc's reads, still completing on the host's clock.

Speed with all of it (`usb-01`, `usb-02`, fps per 5 s): 56, 60, 56, 46,
54, 60 and 56, 60, 57, 44, 53, 60; the dips are the eight saves between
frames 900 and 1200. The owner's recording (no saves): 60 throughout.

Run folders: `playbooks/4E4D0003_NM00301A/4E4D0003_idle-state/` (the
later ones with `mem.bin`, `mem.v600` and `logcat.txt`) and
`.../4E4D0003_20261116-230239/` (`in-01`, `in-02` with theirs).

## Not done

- The disc's completions still come on the host's clock (the buffers
  above; a game whose loading takes longer would get its frames shifted);
  so do the kernel's timers (KeSetTimer, DPCs) and thread preemption. Not
  seen to change Soulcalibur II's fight.
- Vblank 447 of `usb-01` / `usb-02`: one page (0x631000) differed for one
  vblank; not looked at.
- Only Soulcalibur II was run with all of this; other games' playbooks
  (Halo's menus) not yet.
- The kernel's tick counts in guest memory (KeTickCount and the rest)
  still follow the host's clock (Soulcalibur II doesn't import them).
- The hard disk at a playbook's start (saves, caches) is not pinned.
- The owner's recording was made before these changes: it can't match its
  own pictures; its replays now match each other. Record it again with
  this build so its own pictures are the reference.
- Repeat-check command (N runs, the first checkpoint that differs).
