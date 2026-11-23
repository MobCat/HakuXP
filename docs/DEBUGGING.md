# HakuXP debug harness and XBDM

The first debugging target is the Android emulator with PC-side tools.
The Windows executable built by `test.bat xbdm` exercises the debug service;
it does not emulate the Xbox CPU or GPU. A Windows emulator port is separate
work. The transport and protocol have no FEX, Vulkan or Android UI dependency.

To test a game on the device with these tools (launching it from the PC,
screenshots, Mark log, save states, headless `xboxrun` and writing up the
result), follow [TESTING-GAMES.md](TESTING-GAMES.md).

## Start a PC log

The local `scripts/buildenv.local.bat` selects the existing client:

```bat
set "HX_XBDLOG=F:\Projects\C++\DinoRec\xbdtools\xbdlog.exe"
```

Build the APK with `build.bat`, then install it on the selected Android
device. USB debugging must be enabled and the PC authorized. HakuXP installs
under its own application ID (`zip.mobcat.hakuxp.debug` for debug builds,
`zip.mobcat.hakuxp` for release), next to hakuX; see [BUILDING.md](BUILDING.md)
for package migration details.

From Command Prompt in the project root:

```bat
debug.bat
```

Over Wi-Fi, after `wireless.bat pair` and `wireless.bat use` (see
BUILDING.md, "Wireless debugging"), `debug.bat` finds the remembered device
on whichever connection it has, USB first; the ADB forward works the same.

With multiple devices or a timed capture:

```bat
debug.bat --serial DEVICE_SERIAL --duration 120
```

Open HakuXP after the script prints the log paths. The app-wide monitor starts
with the launcher, before loading a game, FEX or Vulkan. An already open app
picks up the temporary PC settings within about one second. No game restart
is needed. The separate Android capture starts immediately on the PC and can
record startup failures even if the app never opens its XBDM listener.
Use `--text` for plain text; the default is NDJSON suitable for scripts and
AI tools. `debug.bat --help` lists overrides. Use
`--package zip.mobcat.hakuxp` for the release APK; the default is the debug
package `zip.mobcat.hakuxp.debug`. Install the APK before starting capture, and
turn on Settings > HakuXP > Debug > Debugging (see "Runtime controls").

In **Settings > HakuXP > Debug > XBDM live logging**:

- Enable app-wide XBDM to keep the monitor available across app launches.
- Check the listening address, port, connected log clients and any error.
- Send a test log without launching a game. Both launcher and service messages
  should appear on the PC.
- Change the device port or restart logging without restarting a game.
- Enable LAN or original Xbox callback connections only when needed. Defaults
  are loopback, port 1731 and callbacks off; the protocol has no authentication.

The settings apply immediately and are stored by the debug service. A PC
session temporarily overrides them; the menu identifies that state and locks
the saved settings until the session ends. Test and restart remain available.

The script:

1. Selects one authorized device (or `--serial` / `ANDROID_SERIAL`).
2. Finds the installed app UID and starts a separate PC-side Android capture
   filtered to that UID, covering launcher, game and debug process PID changes.
3. Creates an ADB forward from PC TCP 731 to device TCP 1731. An existing
   forward is rejected rather than replaced.
4. Saves the three monitor properties and enables a loopback-only service
   with callback connections disabled.
5. Generates an explicit localhost INI and starts xbdlog on the PC, keeping
   its stdin open for the capture. No reference project's default.ini is used.
6. On Ctrl+C, duration expiry or setup failure, closes both loggers, restores
   the previous properties and removes the forward it created.

Files remain in `build/logs/xbdm/`: a timestamped `.ndjson` (or `.log`),
an app-filtered `.android.log`, the exact `.ini`, and `.json` session metadata.
Both PC captures flush each app record as it arrives. xbdlog flushes each received
event to its PC-side file. An emulator crash cannot erase those received
events. In-flight data and the emulator's queue tail can still be lost.
The bounded replay history can repeat recent messages after reconnection.

Restoring properties returns the running app to its saved settings, normally
within one second. If those settings are off, its listener stops. If the PC logger is forcibly killed or USB
disconnects during cleanup, the printed cleanup error identifies any
property/forward that could not be restored. Record the previous values
before a manually managed session.

Direct IPv4 mode skips all ADB and property changes:

```bat
debug.bat --host 127.0.0.1 --port 1731
```

## Implemented protocol

This is a logging subset compatible with the local xbdlog tools, not a
complete Microsoft debug monitor. It does not load an XDK monitor binary
or expose a guest xbdm.dll. The greeting and version identify HakuXP.

| Command | Behavior |
| --- | --- |
| `dmversion`, `dbgname` | HakuXP monitor identity |
| `help` | 202 multiline command list, terminated by a dot |
| `notify [reconnectport=N]` | 205 response; this socket becomes the notification stream |
| `notifyat port=N` | Original Xbox callback connection, when explicitly enabled |
| `notifyat port=N drop` | Remove the requesting peer's callback registration |
| `hakux!status` | HakuXP extension: logging capability, record/drop counters, history capacity |
| `bye` | Acknowledge and close the command connection |

Notifications are CRLF-delimited `debugstr` records, with quoted strings,
doubled embedded quotes, a thread ID and `lf` on each completed logical line.
Long lines are split into fragments below xbdlog's input limit. Control and
non-ASCII bytes are escaped as ASCII `\xNN`. Guest `DbgPrint` retains its
emulated thread ID; host diagnostics use thread zero.

The original Xbox `notifyat` dialect uses a reverse callback socket and an
initial `hello`. The same-socket `notify` dialect works through a single ADB
forward. With callbacks disabled, `notifyat` fails explicitly; both tested
xbdlog clients then fall back to `notify`. `reconnectport` is accepted for
client compatibility; the server does not initiate later reconnections.
The client must reconnect and subscribe after a dropped connection.

Unknown commands return 407. Memory reads/writes, register access, guest
pause/resume/step, breakpoints, modules, files, screenshots and exception
notifications are not implemented. Do not treat a successful connection
as support for xbdpoke or a general debugger. `hakux!status` reports
`guestcontrol=0` explicitly.

## Log coverage and bounds

The Android app hosts one monitor in its non-exported `:debug` bound service.
Both launcher and `:native` processes bind from `Application.onCreate`, before
UI/core initialization. The service loads only `libhakuxdebug.so`; it does not
depend on SDL, FEX, Vulkan or a successful game launch. Its logcat reader runs
with the app UID and reads that UID's messages from all app processes.

Covered: launcher errors, process/activity starts, uncaught Java exceptions,
SDL and direct Android logs, shared `nc_log`, HLE logs and guest `DbgPrint`.
The Android sink marks guest thread IDs for the collector to restore in XBDM.
Host messages use thread zero and include Android timestamp, PID and TID.
On enabling/restarting capture, logcat replays up to 300 recent available
lines. This is best effort, not a complete startup archive.

The PC `.android.log` is independent of the app's service and native library;
start `debug.bat` before opening the app to capture early failures. It filters
by app UID before writing, rather than a single PID, and flushes each line.
It uses the UID output format available on Android 9, without requiring the
newer `logcat --uid` option. Other apps' records are not saved.

A game process crash does not directly kill the service while the launcher
remains bound. Android may destroy it when the last bound app process exits,
on force-stop, or under resource pressure. Data already received on the PC
survives. Logcat buffer overflow, device/USB failure and queue tails can still
lose records. Native raw stderr writes and system-owned crash/tombstone
records are not guaranteed in either app-UID stream; full tombstone capture
remains separate work. Existing local log export is preserved.

Producers copy into a fixed queue without waiting, allocation or socket I/O.
The worker owns all sockets and replay history. A busy/full queue drops
records and reports the count. This does not make the existing formatted
logging function or stderr sink async-signal-safe or nonblocking.

- Producer queue: 128 records; tag 63 bytes and text 1023 bytes, plus NUL.
  Longer input is truncated, consistent with the existing native log buffer.
- Replay history: last 128 collected records, shared by up to eight sockets.
- Each client has a bounded output buffer. Slow readers can lose history;
  a loss record is emitted when possible. Stalled output is disconnected.
- Idle command sockets expire after 10 seconds. Callback connection attempts
  expire after two seconds; stalled output after five seconds.
- Orderly shutdown attempts a final flush; crash logging does not depend on
  that flush succeeding. Debug transport state is not part of a savestate.

## Runtime controls

**Debugging** (Settings > HakuXP > Debug, the first row) is the one switch
over all of it (`android/.../debug/DebugMode.kt`). Debug builds start with it
on, release builds off. Off:

- No debug service: nothing binds `XbdmService`, so it doesn't start (or
  ends when the switch goes off; Android may keep the emptied `:debug`
  process cached until it needs the memory), no XBDM listener opens
  (whatever the saved settings or a PC's `debug.hakux.xbdm*` properties
  say) and the screen isn't kept on for Wireless debugging. A game started
  while it was on keeps the service bound, but the service reads the switch
  every second and closes the listener.
- The rest of the Debug tab stays in view, grayed out: XBDM, Wireless
  debugging, the overlays, the hitch monitor, console timing and Export logs.
- A game starts without the FPS overlay, the EC stamp or the hitch monitor,
  with console timing on, no PC save or load requests, and no playbooks: one
  the launch asks for is refused (the banner says "Debugging is off") and
  `debug.hakux.playbook` commands are dropped, but for `stop` and `exit`, so
  `playbook.bat end` still closes a game the PC started. The guide has no
  Debug tab.

The individual settings are kept for when it is on again. The switch is a
one-line file (`files/debug_mode`) so that all three processes read the
same value; a game reads it as it starts. It does not cover the core's own
`debug.hakux.*` developer properties (`env`, `ncdump`, `fex_pc` and the
rest), which need ADB to set.

The app monitor is disabled by default. Its persisted controls live in
Settings > HakuXP > Debug. Nonempty Android properties override those controls and
are polled by the debug service. The app accepts port 1 (meaning 1731) or
1024..65535, and bind addresses 127.0.0.1 or 0.0.0.0. Invalid overrides stop
the listener and report an error in the menu.

The standalone `xboxrun` CLI still reads environment variables/properties at
startup; it hosts its own monitor. The app game process no longer starts a
second listener. The CLI bind address can be any numeric local IPv4 address.

| Environment | Android property | Default / meaning |
| --- | --- | --- |
| `NC_XBDM` | `debug.hakux.xbdm` | Empty/0 disabled; 1 selects 1731; otherwise a port |
| `NC_XBDM_BIND` | `debug.hakux.xbdm_bind` | `127.0.0.1` |
| `NC_XBDM_CALLBACKS` | `debug.hakux.xbdm_callbacks` | 0; set 1 to allow `notifyat` |

The protocol has no authentication. Keep the default loopback/ADB route
for device work. Explicit callback mode only calls back to the command
peer's IP, at the requested port.

## Module boundaries

These rules are also recorded in [../AGENTS.md](../AGENTS.md).

| Module | Responsibility |
| --- | --- |
| `native/debug/log_queue.*` | Fixed storage, non-waiting publication and drop accounting |
| `native/debug/xbdm_protocol.*` | Command parsing and notification encoding, without sockets |
| `native/debug/socket.*` | Windows/POSIX socket primitives |
| `native/debug/xbdm.*` | Client lifecycle, bounded delivery, callback and replay handling |
| `native/debug/debug.c` | Standalone CLI monitor startup settings |
| `native/debug/android_jni.c` | Small Android transport and property bridge |
| `android/.../debug/XbdmService.kt` | App-wide service, settings ownership and IPC |
| `android/.../debug/XbdmMonitor.kt` | Testable transport/capture lifecycle |
| `android/.../debug/AppLogCapture.kt`, `LogcatRecord.kt` | App-UID capture and guest attribution |
| `android/.../debug/XbdmRuntime.kt` | Process-local service client and UI status |
| `native/core/log.c` | Shared native log fan-out and guest thread attribution |
| `tools/native/xbdm-log.py` | PC logger, device selection, ADB setup and cleanup |
| `tools/native/android_log.py` | PC-side app UID capture, independent of XBDM |

Keep guest CPU state, instruction translation, kernel/devices, rendering,
platform integration and debugger transport behind explicit interfaces.
Use the existing `native/cpu/cpu.h` boundary; do not spread FEX structs into
debug handlers. Split large files by responsibility as they change.
Source modules must be replaceable without requiring runtime plugin loading.

Next, introduce a small debugger backend interface for guest snapshots and
execution control. Requests that inspect mutable state or change memory/CPU
state must run at defined emulator synchronization points. Transport threads
must not reach directly into the JIT, scheduler or Vulkan objects. Add new
command families in separate modules instead of growing one large switch.

## Validation

```bat
test.bat xbdm
test.bat xbdm client
test.bat jvm
```

The first command builds the Windows transport harness with MinGW-w64 GCC,
then runs protocol/queue, socket transport and launcher cleanup tests. The
second also runs the configured unmodified xbdlog client in both dialects,
checks PC log survival after abrupt server exit, and tests the PC launcher
against the real server/client. Every host test uses loopback and temporary
files. Rollback tests simulate ADB; they do not establish device permissions.

`build.bat core` also produces `build/native/core/xbdm_smoke`, an Android
ARM64 executable using the same service and log entry points, without FEX,
Vulkan or a ROM. For a device smoke test, substitute the selected SDK's adb
and select a device with `-s` if needed:

```bat
adb shell mkdir -p /data/local/tmp/hakuxp-xbdm
adb push build\native\core\xbdm_smoke /data/local/tmp/hakuxp-xbdm/xbdm_smoke
adb shell chmod 755 /data/local/tmp/hakuxp-xbdm/xbdm_smoke
```

For the standalone smoke executable, keep the app monitor off and prepare a
manual forward. Direct mode avoids app-package/property setup:

```bat
adb forward --no-rebind tcp:1731 tcp:1731
debug.bat --host 127.0.0.1 --port 1731 --duration 20
```

Run this in another terminal, then remove the forward after the test:

```bat
adb shell /data/local/tmp/hakuxp-xbdm/xbdm_smoke 1731 --auto
adb forward --remove tcp:1731
```

The PC log should contain ten `device event` records, thread `0x00000042`.
This verifies device transport, not guest execution. For manual Windows
tests, `build/xbdm-tests/xbdm_host.exe 1731` accepts `log TEXT`, `guest TEXT`,
`flood`, `crash` and `quit` on stdin. Its compiler runtime DLLs must be on PATH
(the batch test runner sets this). A second argument `callbacks` enables
the original Xbox callback dialect.

Validated on 2026-11-15: all three XBDM CTest cases, both local xbdlog
executables in both dialects, abrupt server exit with surviving PC output,
real launcher capture, and rollback after setup/logger/cleanup failures.
The Android core, smoke executable and APK build successfully; all 15 HLE
host tests also pass. No authorized ADB device was visible during these
checks, so on-device transport and game execution remain pending.

App-wide update, 2026-11-15: all 37 JVM/UI tests pass, including config
overrides, capture/transport lifecycle failures, guest thread parsing and
the Debug tab controls. The three CTest cases and configured xbdlog checks
also pass after adding listener restart/subscriber tests and PC Android
capture rollback/UID filtering tests. The signed APK includes the separate
debug service and its small native library. No authorized device is visible,
so Android permissions, service survival and live game-failure capture still
need to be checked on hardware.

First device check after installing the rebuilt APK:

1. Run `debug.bat` on the PC before opening HakuXP.
2. Open Settings > HakuXP > Debug and confirm a listener and at least one log client.
3. Press Send test log. Check launcher and service messages in both PC logs.
4. Try a failing game launch and confirm the error arrives without relying
   on an exported device log. Confirm logging still works in the launcher.
5. Stop the PC capture. Confirm the app returns to its saved monitor settings.
6. Enable logging from the menu, close/reopen the app, and verify persistence.
   Test native/Java crashes and USB reconnects before large ROM test runs.

The [first device comparison](testing/2026-11-15-first-device-run.md) now
confirms that all 1676 console log records in its capture window reached
both PC streams. It also records a colon-tag metadata parsing fix and the
unresolved RalliSport Challenge 2 black screen. Use
`tools/native/compare-debug-logs.py --help` to repeat the comparison for later
console exports; a matching log set does not prove complete crash coverage.

## Local reference sources

Use source behavior and captured results to resolve differences between
original Xbox and Xbox 360 documentation.

- `F:\Projects\C\xbdtools`: `src/xbdlog.c`, `src/xbdm_proto.c`,
  `src/xbdm_proto.h`, and `docs/protocol-notes.md`. The latter distinguishes
  both notification dialects using the author's hardware observations.
  Source checkout inspected: `969bece`.
- `F:\Projects\C\xbdtools\XDK-RAG-Store`: original Xbox SDK reference index.
- `F:\Projects\C++\DinoRec`: `docs/architecture.md`, project notes,
  `xbdtools/xbdlog.exe`, `XDK360-RAG-Store` and `TheUnderground-RAG-Store`.
  Source checkout inspected: `3cebc21`.

The two `XDK360-RAG-Store/xdk360_index.db` files are byte-identical:
44,707,840 bytes, SHA-256
`8b79f5296b3a980587df6937e378f0efab08a99b46a301fc583568b8cbc8de49`.
DinoRec adds project/reverse-engineering context; its SDK index is not a
newer copy of the xbdtools SDK index. Check the underlying document and
console generation before importing a RAG answer into original Xbox code.
The reference repositories and their default console settings were not
modified or used to contact physical consoles.

## Emulation count

Since 2026-11-15 each native log line (in XBDM, logcat and stderr) starts
with the emulation count in hex, `[0001A2B3] ...`: the kernel's total of
emulated kernel calls, which only moves as the guest runs and survives
savestates. A guest DbgPrint line in logcat has it after the
`[guest-thread=...]` marker, which `LogcatRecord` still finds first. With
the FPS overlay on, each frame on screen shows the same count in its top
right corner, so a `screenshot.bat` picture (or one taken on the device)
can be placed among the log lines around it. Lines from the app's Java and
Kotlin code have no count.

## Mark log

The guide's Debug > Mark log writes one `hakuX-native-mark: [EC] note`
line, plus `| screenshot Pictures/HakuXP/<file>.png` when the picture is
ticked (`nativeMarkLog`; the picture is saved first). While `debug.bat`
runs, its log reader (`tools/native/marks.py`, host test `test_marks`)
pulls each mark's picture into `build/screenshots/` as
`<date>_<time>_<EC>_<note>.png`, in the background so the log never
waits on it.
