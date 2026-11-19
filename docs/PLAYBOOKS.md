# Playbooks: writing and using them

A playbook is a controller recording for one game: which buttons, triggers
and sticks the game read, frame by frame, from a known starting point.
Played back, it presses the same things at the same frames every time, so
you can run a game the same way before and after a change and see whether
a bug is really fixed or just didn't happen this time.

This is the how-to. The design and what has been tested are in
[AUTOMATION.md](AUTOMATION.md) ("Playbooks"); the grammar's source of
truth is the comment at the top of `native/dev/playbook.h`.

Contents: [The rules](#the-rules) -
[Quick start](#quick-start) -
[On the device](#on-the-device) -
[From the PC](#from-the-pc) -
[Keeping playbooks organized](#keeping-playbooks-organized) -
[Writing one by hand](#writing-one-by-hand) -
[Format reference](#format-reference) -
[Savestates and state IDs](#savestates-and-state-ids) -
[Reading the results](#reading-the-results) -
[Checking a bug fix](#checking-a-bug-fix) -
[What stays the same, and what doesn't](#what-stays-the-same-and-what-doesnt) -
[Troubleshooting](#troubleshooting) -
[Reference tables](#reference-tables)

## The rules

- **Games run on screen, never in the background.** `xboxrun` and the old
  sweep scripts refuse on the device. A playbook runs in the app, on the
  device's screen.
- **The banner says who is driving.** While the PC drives the device, or a
  playbook plays, the game shows **\*Automated Testing In Progress\*** in
  red at the bottom left, with the playbook's progress under it, ending in
  the time left at the game's current pace (`ETA 6m20s`: 1200 frames to go
  at 30 fps read 40s; `--` while the game is paused or the first second is
  measured). Leave the device alone while it shows. While you record one,
  it says **\*Recording Playbook\***.
- **The device must be awake and unlocked.** `playbook.bat play` checks and
  stops if it isn't; nothing wakes or unlocks it for you.
- **Pausing is safe.** The game stops whenever it isn't in front (the
  guide open, another app, the screen off), and a playbook's frames stop
  with it: it goes on where it was.
- **`playbook.bat end`** when you're done driving: it closes the game and
  turns the banner off.
- **Debugging must be on** (Settings > HakuXP > Debug > Debugging; debug
  builds start with it on). Off, the app refuses playbooks, drops the PC's
  playbook commands but `stop` and `exit` (`playbook.bat end` still closes
  the game) and has no Playbooks row in the guide.

## Quick start

Record once on the device, replay from the PC as often as you like.

1. Start the game, play to just before the part you care about.
2. Open the guide (Back, or the guide button) > **Debug** > **Playbooks** >
   **Record from here**. The game saves a state, starts again into it,
   and the banner says **\*Recording Playbook\***.
3. Play through the bug. At the moment that matters, guide > Debug >
   **Mark log**, with a short note: that adds a named checkpoint.
4. Guide > Debug > **Playbooks** > **Stop recording**. A message says the
   playbook was saved (named `<title ID>_<date-time>`, e.g.
   `4D530004_20261116-213431`), and it is listed in the same panel: A on
   it plays it back on the device.
5. On the PC:

   ```bash
   playbook.bat list
   ```

   ```bash
   playbook.bat play 4D530004_20261116-213431 --wait
   ```

   The game starts again from the recording's state and plays it back. At
   the end the PC prints each checkpoint's verdict, closes the game and
   turns the banner off.
6. Look at the pictures:

   ```bash
   playbook.bat pull 4D530004_20261116-213431
   ```

   ```bash
   playbook.bat compare build/playbooks/4D530004_20261116-213431/rec build/playbooks/4D530004_20261116-213431/<run>
   ```

## On the device

Everything is in the guide's **Debug** tab, row **Playbooks**. Its panel
on the right works like Save state's: move into it with A (or right), up
and down to choose, A to do it, X to delete, B to go back. What it lists
depends on what is going on:

| Now | The panel lists |
| --- | --- |
| Nothing | **Record from here** (saves a state, then records from it), **Record from the game's start**, then this game's playbooks, newest first: when each was recorded, from a state or from the start, and its last run's result, with the starting state's picture. A plays one, X deletes it (with its runs and states, once you confirm) |
| Recording | **Stop recording**, with the frame and counts so far |
| Playing | **Stop playback**, with the frame |

While recording:

- **Mark log** (Debug tab) adds a checkpoint named after your note.
  There is one every 600 frames anyway.
- **Save state** (Game tab) adds a `save` line: each playback saves its own
  state at that frame too.
- **Load state** (Game tab) is kept: the state is copied into the playbook
  (with the cache files it needs), and the recording goes on from it in a
  new part. A playback loads the same state at the same frame.
- **Restart**, **Exit**, or the game stopping or launching another of its
  programs ends the recording (it is saved up to there).

"Record from here" needs savestates, which work only for games running in
push-buffer mode; otherwise use "Record from the game's start".

When a playback you started on the device ends, a message gives the
result: how many checkpoints and how many did not look as recorded.

## From the PC

`playbook.bat` (`tools/native/playbook.py`) talks to the test device over
adb, the same device the other tools use (`ANDROID_SERIAL`, else the one
`wireless.bat use` remembers, else the only one connected).

| Command | What it does |
| --- | --- |
| `playbook.bat status` | Awake? locked? a game running? the automation properties |
| `playbook.bat list` | The playbooks on the device: name, title ID, XMID, start, when recorded |
| `playbook.bat states` | The state IDs in the playbooks folder: each playbook's own states and those its runs saved |
| `playbook.bat play NAME [--wait] [--uri URI] [--run RUN] [--timeout S] [--keep]` | Plays NAME on screen with the banner. Closes a running game first. `--wait` follows it to the end, then closes the game and turns the banner off (`--keep`: leaves the game running). `--uri`: the disc image when the playbook has no `image` line. `--run`: the run folder's name (default: the date and time). `--timeout`: seconds before it gives up and stops it (default 1800) |
| `playbook.bat record [--here] [--uri URI] [--name NAME]` | Starts a recording for someone to play: the running game from its start, or `--here` from a state saved now, or with `--uri` a game from its start |
| `playbook.bat stop` | Stops playback or recording |
| `playbook.bat mark NAME` | Recording: a checkpoint now |
| `playbook.bat end` | Stops, closes the game, banner off |
| `playbook.bat pull [NAME...] [--to DIR]` | Copies playbooks and their folders (states, runs, pictures) to `build/playbooks/` |
| `playbook.bat push FILE...` | Sends playbooks (and their `states` folders) to the device |
| `playbook.bat compare RUN_DIR...` | Runs side by side, checkpoint by checkpoint |

`play` exit codes: **0** every checkpoint as recorded, **1** some not,
**2** it ended another way (stopped, the game stopped, a relaunch, a
timeout, nothing to play), **3** the device is asleep or locked, **4** adb
failed.

Notes:

- Run it from a Windows prompt or call `python tools/native/playbook.py`
  directly. Passing a quoted URI through `cmd.exe` from Git Bash can leave
  quotes in it; the tool strips them, but calling Python directly avoids
  the problem.
- The disc image is a content URI the app can open (a game in one of its
  game folders). A recording stores it on its `image` line, so `play`
  usually needs no `--uri`. TESTING-GAMES.md explains how to build one.

## Keeping playbooks organized

Keep reusable recordings and hand-written playbooks in the project's
[playbooks/](../playbooks/README.md) library, under `<TITLEID>_<XMID>/`.
The library README indexes the games, playbooks and their tested status.
Keep the original file name and its matching companion folder together:

```text
playbooks/584C0001_XL00100W/584C0001_20261116-215908.txt
playbooks/584C0001_XL00100W/584C0001_20261116-215908/rec/
```

Pull straight into that game's folder with `--to`:

```bat
playbook.bat pull 584C0001_20261116-215908 --to playbooks/584C0001_XL00100W
```

The ASCII playbooks and documentation can be versioned; their companion
folders (states, cache blobs, pictures and runs) stay local and are ignored
by Git. Preserve those folders when backing up or sharing a state-based
playbook. Temporary pulls and run evidence still go to `build/playbooks/`
by default. Push a library playbook by its full local path; play it by its
file name without `.txt`, as before.

## Writing one by hand

Any text editor will do; keep it ASCII. Start from a recording when you
can (pull it, edit it, push it back): it already has the right header and
real frame numbers. A hand-written one is useful when nobody can play,
for example to get past intros or to drive a menu.

### Finding the frames

Frames are the frames the game has drawn since it started (or since its
starting state loaded). A game running at 30 fps draws 30 a second, at 60
fps 60. To find the frame something happens at:

- Watch the banner during a playback or recording: it shows the frame.
- Record it once (even with nobody playing) and read the `check` lines and
  their pictures (`f600.png` is frame 600).
- Leave room: a menu that appears at frame 280 one run may appear at 285
  the next. Press a little after, and hold presses for 2 or 3 frames.

### Example: past the intros

Halo from its start to the profile screen: A twice, each held 3 frames,
then three checkpoints and the end.

```
hakuxp-playbook 1
title 4D530004
xmid MS00409A
start boot
pads 1
image content://com.android.externalstorage.documents/tree/6262-3239%3AISOs%20and%20ROMs%2FXbox/document/6262-3239%3AISOs%20and%20ROMs%2FXbox%2FHalo%20-%20Combat%20Evolved%20(USA)%20(Rev%202).iso
recorded hand-written: Halo past its intros
300 p1 a for 3
345 p1 a for 3
600 check profiles
1200 check f1200
1800 check f1800
2400 end
```

Then:

```bash
playbook.bat push build/playbooks/4D530004_handwritten-boot.txt
```

```bash
playbook.bat play 4D530004_handwritten-boot --wait
```

Hand-written checks have no `sig=`, so they keep a picture but compare
nothing (`nosig`). To get signatures, record the same thing once (see
below) and play the recording instead.

### Example: menus, sticks, triggers

```
# down twice, then A
900 p1 down for 2
930 p1 down for 2
960 p1 a for 3
# hold the left stick right for two seconds (30 fps), then let go
1200 p1 lx=32767 for 60
# accelerate halfway while steering left, from frame 1300 on
1300 p1 rt=128 lx=-20000
1500 p1 -
# two buttons at once
1600 p1 a lt for 4
```

### Example: two players

```
pads 3
100 p1 start for 3
100 p2 start for 3
```

`pads 3` plugs in controllers for players 1 and 2 (a bit per player: 1,
2, 4, 8). Without a `pads` line, the controllers connected to the device
decide.

### Getting real signatures without playing

To record what a hand-written sequence does (so later replays can be
compared), let the core press the buttons while a recording runs. From the
PC, with the game closed:

```bash
adb shell setprop debug.hakux.env "NC_PAD_SCRIPT=300f:a:3,345f:a:3"
```

```bash
python tools/native/playbook.py record --uri "<the game's content URI>" --name 4D530004_intro
```

Wait until the banner's frame is past the last press, then:

```bash
playbook.bat stop
```

```bash
adb shell setprop debug.hakux.env none
```

The recording now has the presses at their frames and checkpoints with
signatures. (`NC_PAD_SCRIPT`'s grammar is at the top of
`native/dev/xid.c`: `frame f : inputs : hold`, comma separated.) Set
`debug.hakux.automation` first if you are driving the device yourself, so
the banner says so:

```bash
adb shell setprop debug.hakux.automation me
```

## Format reference

A playbook is ASCII text, one item per line. `#` starts a comment;
blank lines are ignored. Words are separated by spaces.

### Header lines

| Line | Meaning |
| --- | --- |
| `hakuxp-playbook 1` | Must be the first line: the format and its version |
| `title 4D530004` | The game's title ID (hex). Playback refuses a game with another title ID |
| `xmid MS00409A` | The release (shown and kept; not checked) |
| `start boot` | From the game's start (the default) |
| `start state ID` | From a savestate: a state ID (`4D530004_MS00409A_20261116-213431`) or a file name with `.hxs` |
| `pads 1` | The players with a controller plugged in, as a hex bit mask (1 = player 1, 3 = players 1 and 2, F = all four) |
| `image content://...` | The disc image, for the PC to start the game with |
| `recorded ...` | Free text: when and where it was recorded |

Other words at the start of a line are ignored (room for notes), but a
line starting with anything else, a `-` for instance, is an error.

### Frame lines

Every other line starts with a frame number F (0 to 4294967295).

| Line | Meaning |
| --- | --- |
| `F pN INPUT...` | From frame F on, player N's (1-4) **whole** controller is exactly the inputs listed; anything not listed is released |
| `F pN -` | From frame F on, player N has nothing pressed |
| `F pN INPUT... for H` | Pressed from F for H frames, then **nothing** (the whole controller, not only those inputs) |
| `F check [NAME] [sig=HEX]` | A checkpoint: frame F's picture is saved, and compared with the recording's when there is a `sig=` (384 hex digits; recordings write them) |
| `F save [LABEL]` | Saves the game at frame F into the run's folder, as a new state ID; LABEL is one word kept in its `.info` |
| `F load ID` | After frame F, loads the state ID; the lines after this one are the next part |
| `F end` | Playback ends after frame F; the controllers work again |

Inputs:

| Inputs | Values |
| --- | --- |
| `a b x y black white` | Fully pressed; `a=128` for part of the way (0-255) |
| `lt rt` | Triggers, fully pressed; `lt=100` for part of the way (0-255) |
| `up down left right` | The d-pad |
| `start back` | |
| `ls rs` | Clicking the left or right stick |
| `lx= ly= rx= ry=` | Sticks, -32768 to 32767. `lx` positive is right, `ly` positive is **up** |

Rules worth knowing:

- Lines don't need to be in order within a part: they are sorted by frame,
  and lines at the same frame keep their order in the file.
- A line sets the whole controller. `100 p1 a` then `110 p1 b` releases A
  at 110. To hold both, write `110 p1 a b`.
- `for H` ends with *nothing pressed*. To hold a stick and tap a button,
  write the states out: `100 p1 lx=32767`, `110 p1 lx=32767 a`,
  `113 p1 lx=32767`.
- A press shorter than a frame in a recording still reaches the game for
  one read, so quick taps survive.
- Without an `end` line, playback never ends on its own: the last state is
  held. `playbook.bat play --wait` gives up after `--timeout` seconds and
  stops it.
- Each part can have one `end` or one `load`, not both.

## Savestates and state IDs

Every state the app saves now has an **ID**: `<title ID>_<XMID>_<date-time>`,
e.g. `4D530004_MS00409A_20261116-213431`. It is the state's file name (with
`.hxs`), and beside it is a `.info` text file:

```
id=4D530004_MS00409A_20261116-212538
title=4D530004
xmid=MS00409A
saved=2026-11-16 21:25:38
by=guide
slot=10
game=Halo - Combat Evolved (USA) (Rev 2).iso
app=HakuXP 1.3.0
```

`by` is `guide` (a save slot), `playbook` (a save line, with its `frame`,
`part` and `label`) or `recording` (a state kept with a playbook).

### Where states are

| States | Where |
| --- | --- |
| A playbook's own (its start, its loads) | `playbooks/<name>/states/<ID>.hxs`, with `.info`, a picture `.jpg`, and `blobs/` (the cache files they use) |
| What a run's `save` lines made | `playbooks/<name>/<run>/<ID>.hxs`, with `.info` and `blobs/` |
| The guide's save slots | The app's own storage: `files/native/states/<title ID>.<version>/slotN.hxs` and `slotN.info` (only slots saved since state IDs came in have an `.info`) |

A state is not one file: what the game's cache drives held is in the
`blobs/` folder beside it. Copy the folder, not just the `.hxs`.
`playbook.bat push` and `pull` do.

### Using states in a playbook

- `start state ID`: start from a state. It is looked for in the
  playbook's `states` folder, then beside the playbook, then among the
  game's save slots (by ID).
- `F load ID`: load a state mid-playbook. The core starts again in that
  state (a load always does), and the lines after the `load` line are the
  next **part**, whose frames count from 0 again.
- `F save [LABEL]`: save the game at frame F. Each run gets its own state
  in its run folder, so you can open it later or start another playbook
  from it.

An example with two parts:

```
hakuxp-playbook 1
title 4D530004
xmid MS00409A
start state 4D530004_MS00409A_20261116-210400
300 p1 a for 3
742 save before-load
1200 check menu
1329 load 4D530004_MS00409A_20261116-212538
# part 2: frames from the load
300 p1 a for 3
600 check difficulty
830 end
```

A state named by a save slot's ID works only on the device that has that
slot. To make a playbook that travels, keep its states in its own folder:
record the load (Load state while recording), or copy the state and its
`blobs` into `<name>/states/`.

Saving needs the game in push-buffer mode, like the guide's Save state.
A save pauses the game about a frame while it writes, in the recording
and in every replay alike.

## Reading the results

Each playback writes a run folder, `playbooks/<name>/<run>/` (the run is
the date and time, or `--run`); a recording's is `<name>/rec/`.

**`result.txt`**, rewritten every 2 seconds:

```
mode=play
playbook=/storage/emulated/0/Android/data/zip.mobcat.hakuxp.debug/files/playbooks/4D530004_rec-states.txt
result=ended
frame=831
last=830
events=4/4
checks=3
differs=0
worst=0
saves=1
part=1
```

`result` is `running` while it plays, then `ended` (its end line),
`stopped`, `game stopped` (a crash or the like), `game closed`,
`restarted`, `relaunch` (the game launched another program), or `load`
between parts. `differs` counts checkpoints that didn't look as recorded;
`worst` is the largest difference (0-255); `part` is 0 for the first part.

**`checks.txt`**, one line per checkpoint:

```
600 auto diff=0 same picture=f600.png
1200 menu diff=31 differs picture=f1200.png
1:600 auto diff=2 same picture=s1_f600.png
```

The first word is the frame (`1:600`: part 1's frame 600). `diff` is how
far the picture is from the recording's (the mean difference of 16x12
cells of brightness, 0-255); 8 or less counts as the same. `nosig` means
the playbook had no signature to compare with, `recorded` a recording's
own checkpoint.

**Pictures**: `f<frame>.png` (part 1 and on: `s<part>_f<frame>.png`),
at most 960 pixels wide. They are the game's own frames, without the
banner or the overlays.

**The log**: lines tagged `hakuX-native-playbook` (with `debug.bat`
running): the start, marks, each checkpoint's verdict, saves, loads, the
end.

## Checking a bug fix

1. **Record** up to just past the bug, starting from a state close to it
   ("Record from here"). Mark log right before the bug shows, and right
   after.
2. **Replay twice on the build that has the bug**:
   `playbook.bat play NAME --wait`, twice. Both should show the bug, and
   every checkpoint before it should be the same as recorded. If they
   aren't, the run drifts (see below): fix the playbook first (start
   closer, use a `load`, add slack) before trusting it.
3. **Change the emulator**, build, install.
4. **Replay again.** Checkpoints before the bug should still match; the
   ones at the bug should now differ (the bug is gone), and the pictures
   should show it right.
5. **Write it down**: the playbook, the runs and their verdicts go in the
   test record (TESTING-GAMES.md, "Writing it down"). Keep the playbook:
   it is the regression test for that bug.

## What stays the same, and what doesn't

The same every run: the input at each frame, the start (a state, or a
cold start), the players plugged in.

Since 2026-11-17, while a playbook plays or records (recordings made
before then were made without these, so they replay with these on but
won't match their own recording):

- **The input lands on the same frame of the game every run**
  (`native/dev/inputsync.h`). A playbook's frame is the frames the game
  has pushed, read on the game's own thread at each vblank it takes, and
  the controller's USB frames run there too, so a change reaches the
  game's next pad read every time. Each run's `taken.txt` lists every
  change as the game took it (`frame vblank change`): two runs' files are
  the same when the input went the same way (Soulcalibur II: all 833
  changes, both runs).
- **The game's clocks run on its own vblanks** (`native/kernel/detclock.h`):
  RDTSC (QueryPerformanceCounter) and the kernel's time start from the
  state's exact values (a boot: TSC 0 and the date 2026-01-01) and move
  on a frame per vblank, each guest thread's reads in their own sequence.
- **The vblank comes at the game's own point** (`native/kernel/detevents.h`):
  the host's vblank only counts; the game gets it when all its threads
  wait, or while it spins waiting on the GPU (the same state at two
  checks in a row). A game busy past a vblank gets one, late, not two. A
  game that does neither gets it after 100 ms of its own time anyway
  (logged as "vblank given while the title ran").
- **The GPU runs in lockstep with the game** (`hle_pb_lockstep` in
  `hw/xbox/hle/pb/pbmode.h`): each of the game's writes that lets the GPU
  go on waits until the GPU has done that work, so the GPU's interrupts
  and D3D's callbacks reach the game at the same instruction every run.
  Soulcalibur II stirs its random numbers while it waits for one.
- **The sound runs on the game's vblanks** (`native/dev/apu/lockstep.c`):
  the audio chip's frames run on the game's thread, 25 at each 60 Hz
  vblank it takes, not as the phone's speaker drains them, so a voice ends
  at the same point every run. The sound may stutter when the game runs
  slower than its vblanks (while states are saved).
- **The controller runs on the vblanks only**: its USB frames run at each
  vblank the game takes, also while the game is held for a saved state.

Soulcalibur II's fight with no input now plays the same every time: all
checkpoints the same (difference 0.0), its memory the same at every vblank
but one. Its recorded fight (833 presses, 57 s) replays the same each
time too. Not the same yet: the disc's reads, the kernel's timers and
thread switches still follow the host's clock (seen only as buffers that
differ for a few frames in Soulcalibur II;
[the record](testing/2026-11-17-playbook-determinism.md)).
All this runs only while a playbook plays or records; it costs Soulcalibur
II a few frames a second only while states are being saved.
Things outside the playbook also count: the emulated hard disk (game
saves, caches) is not in a state, and the settings (render scale, GPU
driver) are not in a playbook. The checkpoints show a drift: the first
one that stops matching is where the run went another way. Halo's menus
replayed with a difference of 0 every time.

Finding where two runs part ways: `taken.txt` for the input; for the
game, `debug.hakux.env=NC_TRACE_EIP=<address>` logs how often one guest
function ran each vblank, from which callers and threads
(`hakuX-native-trace`, `native/kernel/calltrace.h`);
`debug.hakux.env=NC_TRACE_MEM=<lo>-<hi>@<file>` writes a hash of each
4 KiB page of that guest range at every vblank (`native/kernel/memtrace.h`;
`NC_TRACE_MEMDUMP=<vblank>` also writes the range's bytes at that vblank;
the property holds 91 characters, so put other settings in
`debug.hakux.env2`), and
`python tools/native/memtrace.py A.bin B.bin` gives the first vblank and
pages where two runs differ (quote a value with `;` for adb:
`adb shell "setprop debug.hakux.env2 'A=1;B=2'"`); and save lines at the same frames in both
runs, then `python tools/native/hxs.py A.hxs B.hxs --ramdiff` (saves are
taken at a pause point after the frame, so small differences there are
expected). Set `debug.hakux.env none` when done: the traces slow the game.

A playbook covers one program of the game: when the game launches another
of its programs (some do between menus and levels), playback and
recording end there (`relaunch`).

## Troubleshooting

| What you see | Why, and what to do |
| --- | --- |
| `The device is asleep` / `locked` (exit 3) | Wake and unlock it; nothing does that for you |
| `No playbook NAME on the device` | `playbook.bat list`; push it with `playbook.bat push` |
| `names no disc image: give it with --uri` | The playbook has no `image` line: pass `--uri` |
| `Not a disc image's URI` | It must be `content://...` or `file://...`, with no spaces or quotes |
| "This game can't start: The game's file can't be opened" | The URI doesn't open: check it (TESTING-GAMES.md), and that the app was given the folder |
| The banner says `not started: line N: ...` | The playbook has an error on that line (the message says what). From the PC, `--wait` doesn't see this yet: it waits until `--timeout`. Look at the device, or `playbook.bat status` |
| `It was recorded with another game` | The `title` line doesn't match the disc |
| The banner says `Debugging is off` | Turn on Settings > HakuXP > Debug > Debugging, then play it again |
| `Its starting state ... is missing` | The `start state` ID isn't in the playbook's `states` folder, beside it, or in the save slots. Push the playbook with its folder |
| `The running game didn't close` | The game wasn't in front, so the command couldn't reach it: bring it to the front, or close it on the device |
| `save ...: savestates need push-buffer mode` (log) | This game doesn't support savestates; the run goes on without the state |
| Checkpoints differ in every replay, even before the bug | The run drifts: start from a state nearer the part you test, add slack to the presses, or use a `load` to reset mid-way |
| A run's `blobs/` came to the PC empty | A run saved before 2026-11-16 21:31 (its cache folder was private); later runs are fine |

## Reference tables

Files, in the app's external files folder
(`/sdcard/Android/data/zip.mobcat.hakuxp.debug/files/playbooks/`):

| Path | What |
| --- | --- |
| `<name>.txt` | The playbook |
| `<name>/states/` | Its own states (`<ID>.hxs`, `.info`, `.jpg`, `blobs/`) |
| `<name>/rec/` | The recording's checkpoints: `checks.txt`, `result.txt`, pictures |
| `<name>/<run>/` | One playback: `checks.txt`, `result.txt`, pictures, the states its save lines made |

Device properties and launch extras (what `playbook.py` uses; you can set
them with `adb shell setprop`):

| Name | Values |
| --- | --- |
| `debug.hakux.automation` | Anything: the banner says the device is being driven; `0`: off |
| `debug.hakux.playbook` | `play:NAME`, `record:start`, `record:here`, `stop`, `mark:NAME`, `exit`, each followed by `@` and something new (the tool adds it), so the same command can be sent twice |
| `debug.hakux.state` | `save:N`, `load:N`: the guide's Save and Load state (slots 1-10); while recording they become save and load lines |
| `debug.hakux.env` | `NC_PAD_SCRIPT=...`: scripted presses for the next game started (see above); `none` to clear |
| `debug.hakux.env2` | More `NAME=value;NAME=value` settings, for when `debug.hakux.env`'s 91 characters aren't enough; `none` to clear |
| Launch extras | `hakuxp.automated` (bool), `hakuxp.playbook NAME`, `hakuxp.record NAME`, `hakuxp.run NAME/RUN`, `hakuxp.segment N` |

Where the code is: `native/dev/playbook.c` (the format, playback,
recording), `native/dev/padplay.c` (the run: pad reads, checkpoints,
files), `native/dev/padstate.c` (saves and IDs), `native/app/playbook_jni.c`,
`android/.../playbook/` and `data/StateInfo.kt` (the app),
`tools/native/playbook.py` (the PC).
