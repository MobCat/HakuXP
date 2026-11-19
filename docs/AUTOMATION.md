# Testing 1100 titles: automation, wireless debugging and remote control

Status: proposal, 2026-11-15. Built so far: what "What already exists"
lists, and wireless ADB (`wireless.bat`, see docs/BUILDING.md). The order
of work is at the end. For testing one game by hand today, see
[TESTING-GAMES.md](TESTING-GAMES.md).

**Changed 2026-11-16 (the owner's rule): games don't run headless on the
device any more.** A run nobody can see can't be told from the device being
free. Every game runs on screen in the app; while the PC drives it, the game
shows **\*Automated Testing In Progress\*** in red at the bottom left; and a
run is repeated the same way with a playbook. See
[Playbooks](#playbooks-repeatable-runs-on-screen) below. Lane 1 as first
planned (headless `xboxrun`) stays below for its history.

## What we need

- Work through the ROM set (about 1100 titles) in groups of 10 to 30 that
  share a problem: run each group unattended, sort each title into an
  outcome, and keep the evidence: logs, frames, the exit reason, keyed by
  the exact disc revision.
- Drive a single title from the PC to investigate it: launch it, press
  buttons, take screenshots, save and load states, read memory, watch logs.
  Over Wi-Fi, without a cable.
- Re-run the set after emulator changes and see what got better or worse.

## What already exists

Most of the batch machinery is upstream's, built for its own device runs.

| Piece | Where | What it gives us |
| --- | --- | --- |
| `xboxrun` | `native/run/xboxrun.c` | Boots an image headless on the device (no app, window or sound), with the HLE GPU as the app runs it |
| Exit codes | `xboxrun.c` header | 0 ended, 1 boot failed, 2 usage/disc, 3 host fault, 5 timeout, 6 hang, 7 guest stopped (bugcheck, unhandled exception; its `stop:` line says why), 8 state load failed, 9 GPU lost, 10 title left (dashboard, reboot, other XBE) |
| `capture=N,...` `frames=N` `hang=S` `NC_TIMEOUT` | `xboxrun.c`, `run/capture.c` | Frames written as PNG by number, a run that ends at frame N, a no-new-frame detector, a wall-clock limit |
| `NC_PAD_SCRIPT` | `native/dev/xid.c` | Scripted input for player 1: `"60f:start:2,120f:a:2"` presses by frame number, the same each run; `@file` for longer scripts (up to 64 steps) |
| `census.sh`, `imports.py` | `tools/native/` | Boots every `.iso` in a folder to its first kernel calls and tabulates unimplemented kernel imports |
| Live knobs | `debug.hakux.*` properties | Savestates (`debug.hakux.state save:N`), GPU timing, quirks, memory scans; `tools/native/knobs.py --md` lists them all |
| Frame readback | `hle_disp_read_front` (`hw/xbox/hle/vk/display.h`) | The presented image, as `xboxrun`'s capture uses it |
| Pad input | `nc_pad_set`, `nc_sbc_set` (`native/dev/usb.h`) | The entry points for every controller state the core sees: a gamepad layout's (Duke, S, wheel) and a Steel Battalion controller's |
| XBDM log service | `native/debug/`, `debug.bat` | Logs from every app process to xbdlog on the PC, over ADB |
| Your xbdtools | `F:\Projects\C\xbdtools` | Real-XBDM clients: xbdpoke, xbdpwd, xbdshot, xbdmem, xbdfsm, xbdlog; `port=` in their ini |

## What changes in the harness

Today the XBDM service only carries logs. It lives in the app's `:debug`
process, reads logcat, and has no path into the game. The rethink:

1. **Every run goes through the app, on screen** (since 2026-11-16; the
   first plan ran batches headless with `xboxrun`, which now refuses on
   Android). A PC orchestrator starts each title in the app with a
   playbook over ADB and reads its results back.
2. **XBDM becomes the control plane for interactive work.** It answers the
   real verbs your xbdtools already send, so they work against HakuXP
   unchanged, plus a `pad!` family that real hardware can't offer.
   `docs/xbdinput-design.md` in xbdtools found that xbdm can't press buttons
   in a retail title on either console. In an emulator we own the
   controller, so we can.
3. **Two processes, one endpoint.** The XBDM server stays in `:debug`, so it
   survives a game crash and can launch the next title. Commands that touch
   the game go to the core over a local channel, and the core runs them at
   defined points (the AGENTS.md rule).

## Lane 1 as first planned: batch triage with xboxrun (off since 2026-11-16)

Off on devices: `xboxrun` exits 11 on Android, and `xboxrun.sh`,
`sweep.sh`, `census.sh` and `frontier-wait.sh` say so and stop. The steps
below still describe what a batch run has to collect and classify; it now
runs through the app with playbooks (next section). The sweeps' records
stay as they were.

A PC orchestrator (`tools/auto/`, Python), per device:

1. **Manifest.** One row per image: title ID and certificate version (the
   key mods and settings already use), region, XBE hash, image hash, path.
   A host tool reads them from the image; `native/core/xdvdfs.c` and
   `hw/xbox/hle/core/xbe.c` already build on the host for the tests.
2. **Queue.** Resumable: a title with a result for this build is skipped.
   One queue per device serial, so several devices split the set.
3. **Run.** `xboxrun <image> hang=20 capture=300,1200,3600 frames=3600`
   with `NC_TIMEOUT` and a default pad script that presses Start and A
   through intros. Per-title scripts override it (`tools/auto/scripts/<title
   ID>.txt`). The custom GPU driver goes in with `vkdriver=`, unpacked on
   the device (TODO.md, GPU drivers), and each result records which driver
   ran. Never while the app is running (`xboxrun.sh` checks).
4. **Collect.** stderr, the PNGs, the exit code, the `stop:` line, the
   unimplemented-import line, run time.
5. **Classify.** From the exit code and log lines: boot failure, host fault,
   guest stop, hang, title left (Midtown Madness 3's return to the
   dashboard should land here), GPU lost, ran to the end. Then from the frames: black, frozen (the
   same picture at every capture), changing. Sound isn't covered: `xboxrun`
   has none.
6. **Report.** `results.jsonl` plus a folder per title; a summary table;
   and a diff of two runs, for regressions. `summary.bat`
   (`tools/native/runlog.py`) already gathers the per-launch facts from an
   app capture (`--json` for scripts); the batch runner can reuse its
   parsing for `xboxrun`'s output.

**Runs are groups, not the whole set.** A run is 10 to 30 titles picked
because they share a problem (the same failure, engine or middleware).
Which groups come first is decided once the method and its records work.
At about 90 seconds a title, a group of 30 takes some 45 minutes.

**The images live on the device.** They are copied by hand to the Odin's
internal storage or SD card (or downloaded there), and run from there:
local storage is faster and steadier than any Wi-Fi link. The orchestrator
takes a device folder and a list of titles; it never copies images. No
network disc is planned.

**First sweep, 2026-11-16.** Steps 3 to 6 in their simplest form:
`tools/native/sweep.sh` on the device (a list of images, each on an
empty disk, one pad script), `tools/native/sweep.py` on the PC (a table,
runs grouped by how they ended), `tools/native/xbehash.py` for step 1's
XBE hash, read from the image on the device. 22 titles in 35 minutes;
four core fixes came out of it
([record](testing/2026-11-16-sweep-1.md)). What it taught: check the
driver line in every run (`vkdriver=` had been ignored, so it ran on
Qualcomm's driver), follow relaunches (`NC_FOLLOW`), and a frame only
counts once compared with the app.

**First manual run, 2026-11-15 (Halo).** What a script will have to do
([test record](testing/2026-11-15-halo-visibility-test.md)):

- `xboxrun` is pushed stripped (`llvm-strip`, 7 MB instead of 70) to
  `/data/local/tmp/native` with `xboxrun.sh`. From Git Bash, set
  `MSYS_NO_PATHCONV=1` or `adb push` turns `/data/...` into a Windows path.
- The app's custom driver and its emulated hard disk are readable from the
  debug app with `run-as zip.mobcat.hakuxp.debug` (`files/gpu_driver/`,
  `files/native/hdd/`). Copy a title's `Partition1/UDATA/<id>`,
  `TDATA/<id>` and its cache partition into the runner's `hdd=` folder,
  and `chmod -R a+rwX` them as the app user so `xboxrun` (the shell user)
  can write them. Restore the copy before each run: titles write their
  saves and markers.
- A title's first boot on an empty disk can spend the whole run on its own
  setup (Halo builds a 770 MB map cache, then starts its attract mode).
  Runs that test a game need its disk state, not just presses.
- `capture=` frames came back fully black at Halo's main menu, which the
  app shows (whether the capture or the headless picture is at fault is
  not known yet). Its level came back very dark: brighten a frame before
  calling it black.

## Playbooks: repeatable runs on screen

**How to record, write, play and read them: [PLAYBOOKS.md](PLAYBOOKS.md).**
This section is the design and its status.

Built 2026-11-16, tested on the PC only (below). Every game runs on screen
in the app now; the emulation already stopped whenever the game wasn't in
front (`onPause`), and a playbook's frames stop with it. While the PC
drives the device, the game shows **\*Automated Testing In Progress\*** in
red at the bottom left, over the controls: leave the device alone then.
The banner is a window view: the device's screen (`screenshot.bat`,
scrcpy) shows it, the frames the guide and the checkpoints keep don't.

A **playbook** is a recorded controller: what the title read from each
port, frame by frame, from a known start (the title's start, or a savestate
saved for it). Played back, it is the only input (the controllers and the
on-screen controls are ignored), so the same presses land at the same
frames every run. **Checkpoints** keep a small picture signature (16x12
cells of brightness) of chosen frames; a replay compares its own with the
recording's and says which match (difference 0 to 255; 8 or less counts as
the same). That is how a fix is told from a lucky run: replay the playbook
that showed the bug, before and after the change, and see the checkpoints
match up to the bug's frame and the bug's frame change.

### Recording

- **On the device:** guide > Debug > Playbooks > *Record from here (saves a
  state)*, or *Record from the game's start*. Then play. From here saves
  the game as the playbook's starting state and starts the core again into
  it, so the recording begins exactly as each playback will. **\*Recording
  Playbook\*** shows in red meanwhile. Mark log adds a named checkpoint;
  there is one every 600 frames anyway. *Stop recording* in the same menu
  ends it (so do closing, restarting or relaunching the game).
- **From the PC, for the owner to play:** `playbook.bat record --here` or
  `playbook.bat record` (the running game, from here or from its start),
  `playbook.bat record --uri URI` (a game from its start).
- **Files**, in the app's external files folder
  (`/sdcard/Android/data/zip.mobcat.hakuxp.debug/files/playbooks`, which
  adb reads and writes without run-as): `<TITLEID>_<date-time>.txt`; its
  own states in `<name>/states/` (each `<state ID>.hxs` with its `.info`,
  its picture `.jpg`, and `blobs/`, the cache files the states use); the
  recording's checkpoint pictures in `<name>/rec/`; each playback's run in
  `<name>/<date-time>/`, with the states its save lines made.

### Savestates in a playbook

Added 2026-11-16, at the owner's request.

- **State IDs.** Every state the app saves now has an ID,
  `<title ID>_<XMID>_<date-time>` (`4D530004_MS00409A_20261116-210400`),
  and a `.info` file beside it (ASCII `key=value`: `id`, `title`, `xmid`,
  `saved`, `by` (guide, playbook or recording), and as it applies `slot`,
  `label`, `frame`, `part`, `playbook`, `game`, `app`). The state's own
  header already held its title ID and save time; the `.info` adds the
  XMID and the ID a playbook names it by. The guide's save slots get one
  too (`slotN.info`). `playbook.bat states` lists the IDs on the device.
- **`F save [LABEL]`** saves the game at frame F into the run's folder as
  a new state ID, so each replay keeps its own state at, say, the bug's
  frame (`playbook.bat pull` brings it to the PC). It needs push-buffer
  mode, as the guide's Save state does.
- **`F load ID`** loads that state after frame F, and the playbook goes on
  with the lines after it, its frames counting from the load: a load
  starts the core again, so it splits the playbook into parts, each with
  its own frames. The ID is looked for in the playbook's states, then in
  the game's save slots. Checkpoints of a later part are `part:frame` in
  `checks.txt` (`1:600`), its pictures `s1_f600.png`; `result.txt` has
  `part=` and, between parts, `result=load` and `load=`.
- **Recording** keeps the guide's Save state as a save line (`F save
  slot3`), and the guide's Load state (or `debug.hakux.state load:N`) as a
  load line: the state is copied into the playbook's states with the cache
  files it uses (`nc_cache_export`), the part ends with `F load ID`, and
  the recording goes on in the next part from that state.
- **`start state`** takes an ID too. "Record from here" now saves the
  starting state into the playbook's states under its ID; a state beside
  the playbook (the first layout) still works.
- **Push and pull** carry a playbook's `states/` folder, so a state goes
  with the cache files it needs; before, `push` sent the `.hxs` alone.

### Playing

- `playbook.bat play NAME --wait` checks that the device is awake and
  unlocked (if not, it says so and stops: ask the owner), sets
  `debug.hakux.automation` (the banner), closes a running game, and starts
  the playbook's game (its `image` line, or `--uri`) with it. The run goes
  in `<name>/<date-time>/`: `checks.txt` (a line per checkpoint, its
  difference and verdict), `f<frame>.png` per checkpoint (scaled to 960
  wide at most), `result.txt` (rewritten every 2 s: mode, result, frame,
  events, checks, differs, worst). `--wait` follows `result.txt` to the end,
  then closes the game and turns the banner off (`--keep` leaves the game
  up). Exit 0: every checkpoint as recorded; 1: some not; 2: it ended
  another way (stopped, the game stopped, a relaunch, a timeout); 3: the
  device asleep or locked.
- On the device: guide > Debug > Playbooks > *Play a playbook* lists this
  game's, newest first.
- `playbook.bat pull NAME` copies it and its runs to `build/playbooks/`
  (`--state` adds the state); `playbook.bat compare
  build/playbooks/NAME/rec build/playbooks/NAME/<run> ...` lists the
  checkpoints side by side. `playbook.bat push FILE` sends a playbook (and
  the state it starts from) to the device.

### The format

ASCII lines, `#` comments. The grammar is at the top of
`native/dev/playbook.h`. A recorded one, shortened:

```
hakuxp-playbook 1
title 4D530004
xmid MS00409A
start state 4D530004_20261116-210400.hxs
pads 1
image content://com.android.externalstorage.documents/tree/...
recorded 20261116-210400 AYN Odin2, HakuXP 1.3.0
120 p1 start
123 p1 -
300 p1 a lx=-32768
301 p1 lx=-31000
600 check auto sig=5A5B...
1210 check menu sig=...
1800 end
```

With saves and a load (two parts; the second's frames count from the
load):

```
hakuxp-playbook 1
title 4D530004
xmid MS00409A
start state 4D530004_MS00409A_20261116-210400
300 p1 a for 3
900 save menu
1200 load 4D530004_MS00409A_20261116-213000
120 p1 start for 2
600 check level sig=...
700 save boss
1800 end
```

`F pN INPUT...` is player N's whole controller from frame F on (`-`:
nothing pressed). Inputs use `NC_PAD_SCRIPT`'s names: `a b x y black white
lt rt` (fully pressed, or `a=128`), `up down left right start back ls rs`,
`lx= ly= rx= ry=`. Hand-written playbooks work too (the agent can't play
the game): `300 p1 start for 2` presses Start for two frames. A check
without `sig=` keeps its picture without comparing it.

### What is the same every run, and what isn't

- **The same:** the input at each frame, where frames are the title's
  swaps (the frames it drew) since the core started or the state loaded;
  the start (a state, or the title's start); the players plugged in
  (`pads`); a press shorter than a frame still reaches the title for one
  read.
- **Not the same:** the emulator isn't deterministic. Guest threads, the
  GPU and the disc run against the host's clock, so a title that times its
  logic by vblank or the clock rather than by frames can drift. The hard
  disk (saves, caches) isn't part of a state, and the settings (render
  scale, GPU driver) aren't part of a playbook. The checkpoints show a
  drift: the first one that stops matching is where the run went another
  way. A playbook whose checkpoints don't match between two replays on the
  same build isn't a test yet.
- **A relaunch** into another XBE ends playback and recording (result
  `relaunch`): a playbook covers one XBE's run.
- **A save** pauses the game about a frame while it is written, in the
  recording and every replay alike; a load is a new start (a new core),
  exact as far as the state is.

### Controls

| What | Values | Who sets it |
| --- | --- | --- |
| Launch extras (`LauncherActivity` passes them to the game) | `hakuxp.automated` (bool), `hakuxp.playbook NAME`, `hakuxp.record NAME`, `hakuxp.run NAME/RUN` | `playbook.py` |
| `debug.hakux.automation` | anything: the banner on; `0` or empty: off | `playbook.py` |
| `debug.hakux.playbook` | `play:NAME`, `record:start`, `record:here`, `stop`, `mark:NAME`, `exit`, each with `@nonce` | `playbook.py` |
| Launch extra between parts | `hakuxp.segment` (int): the part after a load | the app |
| The core's options | `playbook=PATH`, `record=PATH`, `playrun=DIR`, `playseg=N`, `playid=TITLEID_XMID` (native/app/playbook_jni.c) | the app |
| Log lines | `hakuX-native-playbook`: start, marks, each checkpoint's verdict, the end | the core |

### Tested so far

On the PC, 2026-11-16: the playbook module's host test (parsing, playback
including sub-frame presses and catching up, recording and writing back,
signatures), the Kotlin header, folder and banner logic in JVM tests,
`playbook.py` against a simulated adb, and a clean build of the core and the
debug APK. On the Odin2 the same evening
([record](testing/2026-11-16-playbooks-first-device-run.md)): the banner,
`playbook.py` (status, list, push, play, record, stop, end, the refusal
while asleep and locked), a recording from Halo's start, and two replays
matching its three checkpoints (difference 0, on menus). Savestate lines
the same evening ([record](testing/2026-11-16-playbook-savestates-device-run.md)):
a recording with a save and a load, three replays across its two parts
matching every checkpoint, the runs' own states with their `.info`, and
"Record from here" (a playbook from a state) with its replay. **Not
yet:** the guide's own Playbooks, Save and Load menus (their code paths
were reached from the PC), Mark log's checkpoint, a load by a save slot's
ID, gameplay, a relaunch.

## The ROM set

`W:\Microsoft\Xbox`, read-only: nothing writes there, the tools included.
Clean XISO rips, one folder per game (`<region>\<name>\<name>.iso`, with
`default.xbe`, `default.tbn` and often an empty `[TITLEID] <name>.iso.txt`
whose name carries the title ID). As of 2026-11-15:

| Folder | Images | Size |
| --- | ---: | ---: |
| NTSC-U | 717 | 1532 GB |
| PAL | 962 | 2353 GB |
| NTSC-J | 270 | 608 GB |
| MULTI | 200 | 519 GB |
| OXM (magazine demo discs) | 78 | 303 GB |
| Prototypes, tools, homebrew, other | 44 | |

Images average 2.1 to 2.6 GB, the largest 7.4 GB. Only 385 of the 717
NTSC-U folders have a `[TITLEID]` file, so the manifest reads each image's
own XBE for its title ID and version.

## Lane 2: remote control of the app over XBDM

| Verb | xbdtools client | HakuXP behaviour | Runs in |
| --- | --- | --- | --- |
| `dmversion`, `dbgname`, `systime` | xbdpoke | Identity and clock | `:debug` |
| `xbeinfo running`, `modules` | xbdpoke | The running title: XBE name, title ID, image | `:debug`, from the core's status |
| `magicboot title="..." [debug]`, `reboot [warm\|cold]` | xbdpwd | Launch an image from the library (by path under the ROM folder, or by title ID); return to the library or restart the title | `:debug` (starts the game activity) |
| `screenshot` | xbdshot | `203` and the original Xbox header (linear, D3DFMT format) at the game's own resolution, from `hle_disp_read_front` | core |
| `getmem`, `setmem` | xbdmem | Guest virtual memory, read and written at a sync point | core |
| `pad!state port=N buttons=0x.. lt= rt= lx= ly= rx= ry=`, `pad!release port=N`, `pad!script "..."` | xbdinput (planned) | Takes the port over, as XSim's `XSimAcquireControl` does on a 360; `pad!script` uses the `NC_PAD_SCRIPT` grammar | core, through `nc_pad_set` |
| `hakux!state save=N` / `load=N` | none yet | The existing savestate request | core |
| `hakux!status` | none yet | Today's counters plus FPS, frames, draws, title, uptime, the last stop reason | both |
| `notify`, `notifyat`, `debugstr` | xbdlog | As today | `:debug` |
| `threads`, `getcontext`, `stop`, `go`, `continue`, `break` | xbdmem | Later: needs the debugger backend already in TODO.md | core |

`pad!state` uses the shape xbdinput's design picked for a cooperating title
on hardware (route A), so one xbdinput serves both.

Not planned for now: `drivelist`, `dirlist`, `getfile`, `sendfile` (xbdfsm)
on the emulated hard disk. Useful later for save management.

**The local channel.** While a game runs, the core listens on an abstract
Unix socket and accepts only the app's own UID (`SO_PEERCRED`). `:debug`
connects when a game starts and forwards core commands as framed
requests, binary replies included. Pad state and screenshots don't need
the guest stopped. Memory and savestates run at the core's existing
synchronization points.

## Wireless

1. **Android's Wireless debugging** (Android 11 and up; the Odin2 runs 13),
   built: `wireless.bat pair` once, `wireless.bat use`, and then
   `build.bat install`, `debug.bat`, `adb forward` for XBDM and the batch
   runs reach the device over Wi-Fi. adb's mDNS discovery reconnects a
   paired device by itself, and the remembered device is found by its
   hardware serial on whichever transport it has, USB first. Settings >
   Debug > Wireless debugging shows the device's address and opens
   Developer options. Steps: docs/BUILDING.md, "Wireless debugging".
2. **Direct XBDM over Wi-Fi, no ADB.** The LAN option already exists (bind
   0.0.0.0, port 1731; Android apps can't bind 731). xbdtools reach it with
   `port = 1731` in their ini. XBDM has no authentication, and with control
   verbs a client could launch titles and write memory, so LAN mode needs an
   allowlist: control verbs only for PCs approved on the device (asked on
   first connect, kept in settings), logs only for everyone else. Settings >
   Debug shows the device's address and port.
3. **Watching the screen:** scrcpy over wireless ADB for a live view,
   xbdshot for captures and GIFs.

## Order of work

1. Wireless ADB: built; to check on the Odin (pair, discovery, install).
2. The manifest tool, reading the images in place without writing to
   `W:\Microsoft\Xbox`.
3. Playbooks checked on the device (built 2026-11-16), then the batch
   orchestrator over them, in the app and on screen, for a group of 10 to
   30 titles already on the device; how results are recorded comes with it.
4. The core's local channel, then `screenshot`, `pad!`, `hakux!state`,
   `xbeinfo` and `magicboot`, each checked with the matching xbdtool.
5. The LAN allowlist.
6. `getmem` and `setmem`, then the debugger backend (`stop`, `go`,
   `threads`, `getcontext`).

## Open questions

- Which groups to run first: decided once runs and their records work.
- How many devices can run batches.
- Whether xbdtools may change: a HakuXP profile in xbdset, and xbdinput
  built against `pad!`.
