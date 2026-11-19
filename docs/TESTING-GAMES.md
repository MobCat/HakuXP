# Testing a game on the device

How to run a title on the test device, watch it from the PC, collect
evidence and write the result down. It brings together what DEBUGGING.md
(the XBDM log service), AUTOMATION.md (the batch plan) and BUILDING.md
(the build history) describe, as the steps actually used on 2026-11-15
and 2026-11-16 (Halo on the Odin2).

There are two lanes:

- **The app**, for what a player sees: the guide, settings, sound, the real
  controller. Logs come to the PC through `debug.bat`.
- **Playbooks**, for repeatable runs (since 2026-11-16): the app on screen,
  a recorded controller played back the same way every run, checkpoints
  that say whether it went as recorded. The headless `xboxrun` lane that
  did this before is off on devices (below).

## Rules

- `W:\Microsoft\Xbox` (the ROM set) is read-only. Nothing writes there, the
  tools included. Images to test are copied to the device by the owner.
- Do not unlock the device or get past its lock screen. If it is asleep or
  locked (below), ask the owner to wake it and wait.
- A game runs on screen, in the app, or not at all: never headless in the
  background (the owner's rule, 2026-11-16; `xboxrun` and the device
  scripts refuse). Drive the device with `playbook.bat`, which shows the red
  **\*Automated Testing In Progress\*** banner on the game, and run
  `playbook.bat end` when done so the banner goes.
- Do not contact physical consoles named in reference projects'
  `default.ini` files. `debug.bat` writes its own localhost ini.
- Title IDs are uppercase (`4D530004`). Track a run by title ID and XMID
  (`MS00409A`), plus the image file.
- An XBE or image hash is one you read from the image itself, or
  "unchecked". Never copy one from a database.
- Record what was actually tested. A host test does not show that a game
  runs on Android.
- Do not commit or push unless the owner asks.

## The tools

| Tool | What it does | Where its output goes |
| --- | --- | --- |
| `install.bat` (`install.bat build`) | Installs the last debug APK on the test device, over Wi-Fi or USB (builds it first) | The device |
| `debug.bat` | The app's logs on the PC while it runs (XBDM and a separate app-UID logcat); pulls Mark log pictures | `build/logs/xbdm/<date>_<time>.*` |
| `summary.bat` | One summary per game launch in the newest capture: image, IDs, device, GPU driver, how far boot got, how it ended, imports, frame rate, EC ranges. `--json` for scripts | stdout |
| `screenshot.bat [NAME]` | The device's screen as a PNG | `build/screenshots/` |
| The guide's Mark log | A `hakuX-native-mark` line with a note and, if ticked, a picture of the game | The log; the picture in `build/screenshots/<date>_<time>_<EC>_<note>.png` while `debug.bat` runs |
| `debug.hakux.state` | Save or load a state from the PC: `setprop debug.hakux.state save:1` (or `load:1`) | The app's save states |
| `playbook.bat` | Playbooks: `play NAME --wait` on screen with the automated testing banner, `record`, `stop`, `mark`, `end`, `list`, `pull`, `push`, `compare` (lane 2) | The device's `playbooks/`; `build/playbooks/` when pulled |
| `tools/native/xboxrun.sh`, `sweep.sh`, `census.sh` | Off since 2026-11-16: they say so and exit 11 (headless runs on the device) | |
| `tools/native/sweep.py DIR [--md]` | The table of a sweep run before 2026-11-16, its runs grouped by how they ended | stdout |
| `tools/native/xbehash.py [--adb ADB] IMAGE...` | The MD5 of each image's `default.xbe`, read from the image (on the device: `dd` and `md5sum` there) | stdout |
| `tools/native/knobs.py --md` | Every `NC_*` environment variable and `debug.hakux.*` property, from the source | stdout |
| `tools/native/compare-debug-logs.py` | Checks a capture against a log the device exported | stdout |

The adb these use is `HX_ADB` from `scripts/buildenv.bat`, on this machine
`E:\android-kit\sdk\platform-tools\adb.exe`. The device is `ANDROID_SERIAL`,
else the one `wireless.bat use` remembers (`scripts/device.local.bat`,
not versioned), else the only one connected.

## Is the device ready?

From Git Bash, set `MSYS_NO_PATHCONV=1` on every adb call that names a
device path, or `/data/...` becomes a Windows path.

```bash
adb shell dumpsys power | grep mWakefulness
```

```bash
adb shell dumpsys window | grep isKeyguardShowing
```

`mWakefulness=Awake` and `isKeyguardShowing=false` mean go ahead. Anything
else: ask the owner.

## Lane 1: in the app

1. **Build and install:** `install.bat build`, or `build.bat` and then
   `install.bat`. Note the APK's SHA-256 for the record.
2. **Start the capture first**, in its own terminal, so it sees the start:
   `debug.bat`. It runs until Ctrl+C (or `--duration N`), and then puts the
   app's log settings back and removes its ADB forward.
3. **Start the game from the PC.** Send a VIEW intent to the launcher
   with the image's document URI in a folder the app has already been
   given (Settings > General > Game folders):

   ```bash
   MSYS_NO_PATHCONV=1 adb shell am start -a android.intent.action.VIEW -d '<uri>' -n zip.mobcat.hakuxp.debug/com.rfandango.haku_x.LauncherActivity
   ```

   The URI is the folder's tree, then the file as a document under it,
   with `:` as `%3A`, `/` as `%2F` and spaces as `%20`. Halo on the
   Odin2's SD card:

   ```
   content://com.android.externalstorage.documents/tree/6262-3239%3AISOs%20and%20ROMs%2FXbox/document/6262-3239%3AISOs%20and%20ROMs%2FXbox%2FHalo%20-%20Combat%20Evolved%20(USA)%20(Rev%202).iso
   ```

4. **Drive it.** Keys sent with `adb shell input keyevent` reach the
   guide: `KEYCODE_BACK` opens it, `KEYCODE_BUTTON_L1` / `KEYCODE_BUTTON_R1`
   change tabs, `KEYCODE_DPAD_UP` / `DOWN` / `LEFT` / `RIGHT` move,
   `KEYCODE_BUTTON_A` chooses, `KEYCODE_BUTTON_B` goes back. Keys sent in
   a quick burst can be lost (one of five was on 2026-11-16): pause
   between them, and check with a screenshot. Whether adb keys reach the
   game as controller presses has not been checked. Until it is, the
   owner plays, or use a hand-written playbook (lane 2).
5. **Collect evidence while it runs:**
   - Screenshots with `screenshot.bat NAME`. Turn on the guide's Debug >
     EC in the corner (or the FPS overlay), and each frame shows the
     emulation count (EC). That is the hex number each native log line
     starts with, so you can find the picture's place in the log.
   - Mark log (guide > Debug) when something happens. The note and EC go
     in the log, and the picture comes to the PC.
   - Save states with `adb shell setprop debug.hakux.state save:N`, to
     come back to a point. Remove test states the title did not have
     before.
6. **Stop and summarise:** Ctrl+C in `debug.bat`, then `summary.bat`. The
   raw files are in `build/logs/xbdm/`: `.ndjson` (XBDM), `.android.log`
   (the app's logcat), `.json` (the session) and `.ini`.

## Lane 2: playbooks

A recorded controller, played back the same way each run, on screen. The
how-to, with the format and hand-written examples, is
[PLAYBOOKS.md](PLAYBOOKS.md); the design is AUTOMATION.md, "Playbooks".
In short:

1. **Record** once: the owner plays, from guide > Debug > Playbooks >
   *Record from here* (a state saved first) or *from the game's start*;
   *Stop recording* when the bug has shown. Or from the PC:
   `playbook.bat record --here`. Mark log adds a named checkpoint (say,
   just before the bug).
2. **Replay** as often as needed, before and after a change:
   `playbook.bat play NAME --wait`. The device must be awake and unlocked;
   the game shows **\*Automated Testing In Progress\*** in red at the
   bottom left while it plays. It ends with each checkpoint's verdict.
3. **Look** at the pictures: `playbook.bat pull NAME`, then
   `playbook.bat compare build/playbooks/NAME/rec build/playbooks/NAME/<run>`.
   Replay twice on one build first: checkpoints that don't match then mean
   the run drifts (the emulator isn't deterministic), not a fix.
4. **Done:** `playbook.bat end` closes the game and turns the banner off.

A test record names the playbook, its runs and their verdicts.

## Before 2026-11-16: headless xboxrun (off)

Off on devices: `xboxrun` exits 11 on Android and `xboxrun.sh`, `sweep.sh`
and `census.sh` say so and stop. Kept for reading the records made with
it. The first run is written up in AUTOMATION.md ("First manual run") and
in [testing/2026-11-15-halo-visibility-test.md](testing/2026-11-15-halo-visibility-test.md).
In short, as it was:

1. **Push the runner.** Copy `build/native/core/xboxrun` to the scratch
   folder, strip it with the NDK's `llvm-strip` (7 MB instead of 70), and
   push it with `tools/native/xboxrun.sh` to `/data/local/tmp/native`.
2. **The GPU driver.** The app's custom driver is readable with `run-as`.
   Copy it and its `meta.json` next to the runner, and push adrenotools'
   hook libraries there too (`build/native/core/app-libs/*hook*.so`):

   ```bash
   MSYS_NO_PATHCONV=1 adb shell "run-as zip.mobcat.hakuxp.debug cat files/gpu_driver/vulkan.purple.so > /data/local/tmp/native/vulkan.purple.so"
   ```

   ```bash
   MSYS_NO_PATHCONV=1 adb shell "run-as zip.mobcat.hakuxp.debug cat files/gpu_driver/meta.json > /data/local/tmp/native/meta.json"
   ```

   Then `vkdriver=/data/local/tmp/native/vulkan.purple.so` (or the app's
   form, `vkdriver=DIR|LIB|HOOKS`). Check the run's
   `Vulkan driver vulkan.purple.so from ...: loaded` and
   `vk: driver "PurpleVK public driver" (id 18)` lines. **Before
   2026-11-16 a plain path was ignored without a word**: every headless
   run of 2026-11-15 and the first sweep ran on Qualcomm's system driver
   (`vk: driver "Qualcomm ..." (id 8)`), whatever their records say.

3. **The disk.** A title's first boot on an empty disk can spend the
   whole run on its own setup. Copy the app's disk (`files/native/hdd/`,
   with the title's `Partition1/UDATA/<id>`, `TDATA/<id>` and its cache
   partition) into `/data/local/tmp/native/hdd` with `run-as`. Then
   `chmod -R a+rwX` it as the app user so the shell user can write it.
   Restore the copy before each run, because titles write to it.
4. **Run.** The app must not be running (`xboxrun.sh` refuses):

   ```bash
   MSYS_NO_PATHCONV=1 adb shell "cd /data/local/tmp/native && NC_TIMEOUT=90 NC_PAD_SCRIPT='300f:a:3,345f:a:3' sh xboxrun.sh '/storage/6262-3239/ISOs and ROMs/Xbox/Halo - Combat Evolved (USA) (Rev 2).iso' vkdriver=/data/local/tmp/native/vulkan.purple.so hdd=/data/local/tmp/native/hdd hang=20 capture=600,1500 > run.out 2>&1; echo exit \$?"
   ```

   `NC_PAD_SCRIPT` presses buttons the same way each run:
   `ms:input[+input...]:hold_ms`, comma separated. `60f:a:2` counts frames
   instead (frame 60, held 2). `@path` reads the script from a file, up to
   64 steps. The grammar is at the top of `native/dev/xid.c`. `capture=`
   writes those frames as PNGs,
   `frames=N` ends the run at frame N, and `hang=S` stops it after S
   seconds without a new frame. The exit codes are listed in AUTOMATION.md
   (0 ended, 3 host fault, 5 timeout, 6 hang, 7 guest stopped, 10 title
   left, and others).
5. **Collect.** Pull `run.out` and the PNGs, plus a logcat of the run,
   into `build/logs/xboxrun/<date>-<title>/`. Headless frames can come out
   very dark: brighten one before you call it black.

### A sweep: many titles, first boot

To decide what to test next, boot many images the same way and group
them by how they end (first used 2026-11-16, 39 images). Push
`sweep.sh`, a list of image paths (one per line, LF endings) and a pad
script file next to the runner, then start it in the background; the app
must stay closed (it stops before the next image if the app is up):

```bash
MSYS_NO_PATHCONV=1 adb shell "cd /data/local/tmp/native && NC_PAD_SCRIPT=@/data/local/tmp/native/sweep-pad.txt nohup sh sweep.sh /data/local/tmp/native/sweep-list.txt /data/local/tmp/native/sweep1 vkdriver=/data/local/tmp/native/vulkan.purple.so hang=30 capture=300,900,1800 > /data/local/tmp/native/sweep1.log 2>&1 < /dev/null &"
```

Each image gets 90 s (`NC_TIMEOUT`) on an empty disk, deleted after it.
The 2026-11-16 pad script pressed Start, then A, alternately every 5 s
from 15 s (`15000:start:150,20000:a:150,...`), to get past title
screens. Pull `results.tsv`, the `N.out` files and `N/frames/` into
`build/logs/xboxrun/<date>-sweepN/`, then
`python tools/native/sweep.py <that folder> --md`. A first boot on an
empty disk is not a play test: exit 5 (timeout) only says nothing
stopped it for 90 s; look at the frames.

## Reading the logs

- Each native line starts with `[EC]`. A state load keeps the count
  going, and a new process in the same session starts one past the last
  one.
- `hakuX-native-frontier: guest fault: the title doesn't handle it: read of
  X at eip Y`, then `stop: page fault at X ...`: the title crashed; since
  2026-11-16 that stops the game only (the app says "The game stopped";
  `xboxrun` exits 7). `host fault` is now left for faults the core can't
  take as the guest's (outside translated code, inside a kernel call), and
  those still close the whole app (TODO.md, Debugging aids).
- `vk: driver "..." (id N)`: the driver that actually ran (18 Turnip, 8
  Qualcomm's). Check it in every headless run.
- `xboxrun`'s `stop:` line says why the guest stopped.
- `hakuX-native-playbook`: a playbook's start, its marks, each
  checkpoint (`check menu at frame 1210: NOT the same as recorded
  (difference 31)`) and its end (`playback of ...: ended at frame 1801, 3
  checkpoints, 1 not as recorded`).
- `mm: contiguous allocation ... failed` names the caller and range. It
  was Halo's fault.
- Unimplemented kernel imports and zero-filled SCSI commands are listed by
  `summary.bat`. `imports.py` reads them from a `census.sh` run.
- The overlay's `guest CPU-bound: title code at ADDR` can be the title's
  own spin-wait (Halo's frame tick at `000BB0DF`), not real CPU load.

## Writing it down

- **A test record:** `docs/testing/<date>-<title-or-topic>.md`, like the
  Halo record. Include:
  - title, title ID, XMID and certificate version
  - the image file and its size
  - the XBE MD5 (or "unchecked")
  - the device and Android version, the GPU driver
  - the APK SHA-256, and the capture files used
  - then Before, Diagnosis, Fix, After, and **Not tested**
- **A build entry:** a dated paragraph in `docs/BUILDING.md` when a build
  goes on the device. Include the change, the test counts, the APK
  SHA-256, and what was and wasn't checked there.
- **TODO.md:** update or add the item, with what is still open.
- **DEVICES.md:** add the game to its device's "Games tested" (or the
  device itself, if it is new), with its title ID, XMID and result.
- **Evidence** stays in `build/` (logs, screenshots, xboxrun runs). It is
  not versioned, so the record names the files.

## Where things stand (2026-11-16)

The current list is TODO.md, "Game compatibility leads". Run so far on
HakuXP, all on the Odin2 (Android 13, Adreno 740):

- Halo (`4D530004`) plays from its save, after the kernel-area fix.
  Still open: its light shaders, and upstream v1.3.0 for comparison.
- Midtown Madness 3 (`4D53002A`) boots since the security-sector fix.
  Its 36 fps average is 60 in menus, a locked 30 in play and slow load
  screens (TODO.md).
- RalliSport Challenge 2 (`4D530039`) works on Turnip. On the system
  Qualcomm driver the picture is black.
- Sweep 1 (2026-11-16): 22 more titles' first boots, headless, on the
  system driver by mistake ([record](testing/2026-11-16-sweep-1.md)).
  DroneZ and Fallout 2 CE fixed from it.
- A title's page fault stops only the game since 2026-11-16 (headless
  checked; the app's dialog not yet seen on the device).

- Sweep 2 on Turnip ([record](testing/2026-11-16-sweep-2.md)), all 39
  images: 38 run 90 s, 37 draw correctly. Leads: Xbox Music Mixer halts
  at boot, Re-Volt stays on its title, Halo (Japan) stays black.

Next: those three; groups that share a problem before the compatibility
rating (AUTOMATION.md).
