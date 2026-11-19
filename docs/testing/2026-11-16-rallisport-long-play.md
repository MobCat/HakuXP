# RalliSport Challenge 2: a 93-minute play in the app

**Title:** RalliSport Challenge 2 (World), `4D530039` `MS05701W`.
**Image:** `MS05701W.iso` on the Odin2's SD card, 6,656,884,736 bytes.
**XBE:** `default.xbe`, MD5 unchecked.
**Device:** AYN Odin2 (QCS8550, Adreno 740), Android 13,
`Odin2_V1.0.0.380_20250815_111346_user`.
**GPU driver:** PurpleVK 26.3.0-devel (git-62ac221a33), Turnip, the app's
custom package (`vk: driver "PurpleVK public driver" (id 18)`).
**APK:** not recorded in the capture.
**Capture:** `build/logs/xbdm/2026-11-16_13-31-35.470815.*` (debug.bat),
11 Mark log pictures in `build/screenshots/2026-11-16_13-33-44_*` to
`2026-11-16_15-01-28_*`.

The owner played rally stages from 13:31 to 15:04 and marked what looked
wrong. Three core processes (summary.bat):

| pid | Start | Length | ECs | How |
| --- | --- | --- | --- | --- |
| 24053 | 13:31:46 | 50.6 min | `00000000..034D29D7` | from the library; a state saved at `034CEE9D` |
| 6365 | 14:22:31 | 5.5 s | `00000000..00005082` | from the library, closed for the state's load |
| 6494 | 14:22:37 | 42.1 min | `00005083..02771006` | loaded slot 1 |

Each ended with Exit to library or a relaunch (SIG 9 from the activity).
No title fault, no host fault, no guest stop. Frame rate 58.5 and 59.7
average, 61.5 and 61.8 at best.

## 1. The car turns white after six stages (diagnosed)

**Seen:** the player's car body blown out to plain white, its livery gone
(marks `028079EF` "Bad car shader i thought we fixed", `0280DEB2`,
`02CDC810`, `034CEE8A`, then in the next process `0255CDEF` and
`025994FE`). Once it starts it stays, stage after stage. Loading the
state saved with it (`00009E14`: "loading a save state reset the bad
sharder") brings the car back.

**Why:** the car's reflection is a cube map the game renders into six
128x128 face targets, 0x10000 apart, at a new address for each stage.
`cube_of_targets` (`hw/xbox/hle/pb/pbdraw.c`) turns those targets into a
cube and logs `cube N: 128x128 at X (face stride 10000) from its face
targets`. In both long processes that line comes exactly six times and
never again:

| Process | Cubes made | Next table-full / first mark |
| --- | --- | --- |
| 24053 | 13:39, 13:43, 13:50, 13:53, 13:58, 14:01:42 | stage at ~14:04 has none; white car on screen by 14:07:04 |
| 6494 | 14:22:37 (the load), 14:26, 14:30, 14:34, 14:39, 14:45 | stages at ~14:51 and 14:58 have none; white car on screen by 14:58:56 |

The later stages still render all six faces: the save's object list
(`savestate: surface N ... kept`) has 128x128 targets at
`02560100 + f * 0x10000`, f = 0..5. So `cube_of_targets` found its faces
and failed at its other condition:

```c
if (!found || g_pd.nobjs >= MAX_OBJS) {
    return 0;
}
```

`g_pd.nobjs` only grows (a full table reuses slots through `evict_slot`),
so once the object table (`MAX_OBJS` 128) has filled, no cube is ever made
again. `texture()` then falls back to face 0's 2D target, which is the
white-car case the function's own comment describes for Forza. And
`evict_slot` never frees a cube or its face views (`o->cube ||
o->d.parent`): this title leaves one cube plus 12 views (linear and
swizzled targets on each face) pinned per stage, so the table fills
faster with each stage. A new process starts with an empty table, which
is why the state load fixed it.

Not proven on the device: `nobjs` isn't logged, and no build with a fix
has run. The six-and-stop pattern in both processes and the face targets
present in the save are the evidence.

**Fix (in code, not yet on a device):**

- `hw/xbox/hle/pb/pbslot.c` (new, pure) chooses what a full table frees:
  the object used longest ago, a cube and its views as one unit, used
  when any of them was last bound or the cube last sampled, and kept
  while any of them is in use.
- `pbdraw.c`: `evict_slot` describes the table to it. A cube sampled this
  epoch stays (the draw being built may hold its id), and so does the
  colour target a bind has while it makes its depth target (`keep`).
  `new_slot` frees what was chosen; a cube goes with its views
  (`cube_free`, views first, the table closed up, `rt_obj`/`ds_obj`
  looked up again, logged as `cube N at X and its K face views freed`).
  `cube_of_targets` takes its slot through `new_slot`, never one of its
  own faces' (`keep_pa`, `keep_len`), and logs when it still can't.
  `target_part` looks its source up again after making an object.
- Host test `tests/hle/test_pbslot.c`: the unit rules, and a model of
  this title's stages (5 targets and a 12-face cube each, at new
  addresses) in a 128-slot table. Under the old rule the 8th stage's cube
  finds the table full; under the new one 40 stages each get theirs.
- `test.bat`: 21 of 21 pass. `build.bat core`: builds, no warnings.

The timing first looked like a 32-bit microsecond overflow (2^31 us is
35 min 47 s, and both breaks came about 36 minutes into a process). The
run-1 white car was already on screen 30 s before that point, and the
cube log explains both runs, so the timer idea is dropped.

## 2. "The world imploded" (lead, not diagnosed)

Mark `028635E9` (14:11:46, frame from the pause at ~14:10:50, race time
02:23.9): large dark triangles fanned out from the middle of the view
over the road. Nothing unusual in the log in the minute before it, and
the owner did not see it again. Large triangles fanning from the view's
middle look like vertices at or behind the camera (w near or below 0)
not clipped as the NV2A does; a near-plane object (a bridge, a bank)
would do it. Needs a repro: a save state just before, then
`debug.hakux.pbdraws=<n>` for the frame.

## 3. Stutters: the guest's vblank lock can't hold its phase (lead)

4,160 of the 4,255 stutter lines are `UI missed 1 front frame (pickups
~18 ms apart)`: a guest frame held for two refreshes (median 31.9 ms;
`front 32 ms after the last (expected 16.7)`) and the frame after it
never shown. They come about one every 60 guest frames (median), 0.7 a
second, and sometimes back to back (300 of 4,160 within 3 frames of the
last). Only one was "vCPU starved", the cause TODO.md had for this title
on 2026-11-15.

`nv2a.c` places the guest's vblank half a panel period from where the
presenter picks frames up. Measured over both long processes, the
pickup phase drifts +5.5 to +6.1 ms a second against the Choreographer's
vsync (period 16.28 ms, about 61.4 Hz), a full wrap every ~3 s, about two
stutters per wrap; the minutes with the least drift (+2 ms/s) have a
third of the stutters. The presenter is paced by its window's buffer
queue, so its pickups don't keep a fixed phase to the vsync times the
core locks to. Next: log raw pickup and vsync times for a few seconds to
see which clock is off, and check the Odin2's display modes for a 60 Hz
one (`dumpsys display`).

## 4. Saves (checked: they land; the `*` not measured)

The owner wasn't sure the game saved, and its save mark (`*`) is on
screen too briefly to see. From the capture's file opens (`hakuX-native-io`)
and the device's disk:

- The game saves to `UDATA\4d530039\D26DB9486574\user.profile` and
  `TDATA\4d530039\Highscore\Highscore.dat`: after each stage the profile
  twice and the high scores once, plus single profile saves at other
  points. 50 profile and 21 high-score writes in the two long processes.
- No save call failed. The kernel calls that did fail (`c0000035`, 684
  of them) are the game creating cache-partition folders that already
  exist; `c0000034`/`c000003a` are file probes across disc layers.
- On the Odin2 afterwards (15:30, `run-as`, read only): `user.profile`
  249,344 bytes, modified 15:04:27.952; `Highscore.dat` 324,408 bytes,
  15:04:08.048; the log's last two saves are at those same times.
  `SaveMeta.xbx` (40 bytes) is unchanged since 2026-11-15 14:08: the game
  only reads it. The session also started by reading the profile saved
  the day before. Whether the game shows the right progress from it is
  the owner's call.
- How long a save takes here: the end-of-stage save's opens span 94-170
  ms, a single profile save 1-3 ms. That is the game's own work: HakuXP's
  `NtWriteFile` is a host `pwrite` that takes no emulated time, as the
  console timing (`native/kernel/io.c`, `device_us`) covers reads only.
  If the `*` stays up only while the save runs, it gets 0-10 frames.
- On an Xbox a write to the hard disk costs seeks and rotation, tens of
  ms per access, so the `*` would likely stay longer there, but not
  necessarily long: the XDK's guidance asks for a "Saving %s." message on
  the hard disk only when a save takes more than five seconds (Reference
  UI, Load and Save Game, "Selecting the Slot"). Not compared with a
  console.

To measure the `*`: record the screen at a stage's end (scrcpy, 60 fps)
and count the frames it shows, next to footage from an Xbox.
`NC_TRACE_IO` logs each write with its size, but only for headless
`xboxrun` (an environment variable, no app property).

## 5. Smaller things

- **Loads:** each stage load holds the guest about 0.55 s (title code at
  `000A1237`/`000A124F`) and then about 1.24 s in a host call at
  `002BB620`/`002BB630`, every time. That matches the disc's console
  timing (`disc 125 us + 27600 kB/s`); probably as designed.
- **A pause counted as a hang:** HANG #3 (13:40:58, 53.6 s, "decoder
  waited for the flip") is the owner's 53.5 s pause. The hitch monitor
  should leave paused time out.
- **The debug service's binder warnings:** the `:debug` process logs
  `Process seems to be sending too many oneway calls` and then
  `FAILED BINDER TRANSACTION (parcel size = 472)` 22:04 and 25:05 after
  each long game start, in both processes. `XbdmService`'s `tick`
  sends a STATE message to every registered client every second,
  changed or not; the main process sits in the background all game and
  likely doesn't take them (Android's cached-app freezer: not confirmed,
  the capture has only the app's own logs). A failed send then drops
  that client. No effect on the game; send only on change.
- **The spoiler looks dark** (`004C184C`) and **LOD pop-in** (`00F53C00`):
  no reference picture to compare with yet; may be the game's own look.
- Nine unimplemented XAPI I/O imports, none seen called.

## Not tested

- The cube fix on a device: no APK was built or installed. To check:
  play RalliSport past seven stages and look for a `cube N: ... from its
  face targets` line every stage and `... face views freed` once the
  table is full; Forza and Sega GT 2002 (cubes used every frame) for
  regressions.
- The other leads: no change made for them.
- That `g_pd.nobjs` reached 128 (inferred, not logged).
- The car's look on an Xbox or in another emulator.
- Whether "the world imploded" repeats.
- Sound, and controller input timing.
