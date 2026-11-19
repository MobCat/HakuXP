# First app-wide XBDM device capture

Inputs, unchanged in `build/logs/xbdm/`:

- `2026-11-15_12-23-51.178810.android.log`
- `2026-11-15_12-23-51.178810.ndjson`
- `hakuX-logs-2026-11-15_12-27-10.zip`

Device-reported model: Odin2, Android 13/API 33, QCS8550, Adreno 740.
The owner believes this may be an original Odin; physical model identification
is not established by these software fields. Package under test:
`com.rfandango.haku_x.debug`, version 1.3.0 (9).

## Log coverage

Comparison uses each message's device timestamp, PID, TID, level, tag and text,
and preserves duplicate counts. The PC receive clock is several seconds
behind the device clock, so it is not used to align the streams.

Device capture window: `11-15 12:24:03.839` to `11-15 12:25:37.436`.

| Source in console export | Records in window | Missing from PC Android | Missing from XBDM |
| --- | ---: | ---: | ---: |
| Current launcher app.log | 71 | 0 | 0 |
| Midtown Madness 3, first launch | 173 | 0 | 0 |
| Midtown Madness 3, second launch | 172 | 0 | 0 |
| RalliSport Challenge 2, MS05701W | 1260 | 0 | 0 |
| Total | 1676 | 0 | 0 |

The PC Android capture has 1690 records. Its additional 14 records are 11
debug-service startup records and three process self-termination messages.
The per-process console capture did not retain those termination messages.
All 1690 PC records have matching content in XBDM. XBDM additionally has its
monitor-start message and two client connection status events.

A separate metadata issue affects 14 XBDM records: the original logcat parser
split colon-containing tags, such as `_x.debug:native`, at their first colon.
The suffix remained in the message, so text was retained but tag attribution
was wrong. The comparison identifies this exact transformation separately;
it does not silently count it as an exact metadata match. The parser now
uses logcat's colon-space delimiter and has a regression test using an actual
captured process tag.

The parser fix passes all 38 JVM/UI tests and the debug APK builds. The
renderer, core libraries and game settings were not changed by this fix.
It is a logging metadata correction, not a claimed black-screen fix.

The export also contains 47 records from an older app session and eight
launcher records after the PC capture ended. They are outside the comparison
window. Session metadata and Android exit summaries are not logcat records;
this test does not establish capture of all native stderr or tombstone data.

Reproduce the comparison from the repository root:

```bat
python tools/native/compare-debug-logs.py build/logs/xbdm/hakuX-logs-2026-11-15_12-27-10.zip build/logs/xbdm/2026-11-15_12-23-51.178810.android.log build/logs/xbdm/2026-11-15_12-23-51.178810.ndjson --output build/audit/2026-11-15-first-device-run/log-coverage.json
```

## Game results

Midtown Madness 3 (USA), title `4D53002A`, exits at stage S2 after 22 kernel
calls on both launches. Immediately before returning to firmware with routine
2, it issues repeated SCSI opcode `AD` requests; the emulator logs zero-filled
responses. This sequence is a lead for the existing boot failure, not proof
that the response is the sole cause. The import inventory lists nine
unimplemented kernel functions, but that list alone does not prove they were
called. The native activity then closes the game itself.

The owner identified `MS05701W.iso`, title `4D530039`, as RalliSport Challenge
2 (World) (En,Ja,Fr,De,Es,It). It previously loaded; this run shows a black game
image with FPS/debug counters still visible.

RalliSport's game process continues running for about 64 seconds:

- Two startup pauses recover after about 4.54 and 2.99 seconds.
- At the last statistics record, the emulator reports 3326 written display
  frames, 3325 presented frames, 21693 draws, and zero push-buffer faults.
- The graphics backend reports no dropped state/target/layout/pipeline draws
  in the final interval and no Vulkan device-loss error.
- Presentation queue activity continues. These counters establish ongoing
  work; they do not prove the rendered image contains visible pixels.
- The final process exit is SIGKILL, with an app-side `Process: Sending signal`
  record. There is no recorded fatal exception preceding it.

Configuration in this run: system Qualcomm Vulkan driver, render scale 1x,
automatic aspect, FXAA off, bilinear upscale, asynchronous shaders and
prebuild enabled, console language 1, video flags `00080000`, AV pack 0.

The black-screen cause remains unproven. No renderer workaround was applied
on the basis of these logs. Possible failure stages still include the source
image, final presentation pass, and Android surface composition.

## Live follow-up

ADB briefly connected and confirmed that both the original
`com.rfandango.haku_x` and the fork are installed. The fork's saved preferences
had no XBDM preference file, and the three temporary XBDM properties were
empty. The device was at its launcher, with no native game process running.

USB disconnected before the controlled launch and XBDM toggle commands.
Those commands failed with device-not-found; no live comparison or renderer
change was performed. A screenshot of the initial device launcher and the
comparison report are in `build/audit/2026-11-15-first-device-run/`.

Next checks with a stable connection:

1. Capture the same title with XBDM off, retaining PC-side Android logs.
2. Repeat with XBDM on, keeping game, graphics settings and build fixed.
3. Compare against the installed original hakuX if both fork runs are black.
4. Inspect an Android screenshot and the existing render-target dump together
   to distinguish a black source frame from a presentation problem.
5. Record relevant debug properties before changing any, and restore them
   after each experiment. Do not clear game saves or shader caches merely to
   hide the reproduction.

## Follow-up at 14:07: RalliSport Challenge 2 on Turnip

Same title and device, a build with the Wireless debugging status
(APK SHA-256 `5980389a...6f13f91a`), installed and captured over Wi-Fi:
`debug.bat` ran on the remembered device's wireless serial
(`adb-c3e8b774-IKUygG._adb-tls-connect._tcp`). Capture:
`build/logs/xbdm/2026-11-15_14-07-08.079455.*`.

- GPU driver: the owner's custom Turnip package ("Turnip Adreno Driver
  T30", Mesa `26.3.0-T30-1.4.359`): `Vulkan driver vulkan.purple.so ...
  loaded`. The first run above used the system Qualcomm driver.
- The game reaches its main menu and draws it correctly: Android
  screenshot `build/audit/2026-11-15-rsc2-turnip/main-menu.png` (Career
  tab, FPS overlay 60).
- 60 fps held for 3 minutes (5-second windows: 51.5 first, then 59.4 to
  60.0). About 30 stutters of 33 ms, mostly "vCPU starved" (the guest's CPU
  thread got 10 to 15 ms of host CPU in a 29 to 33 ms frame); one 0.6 s hang
  at start, guest CPU-bound. No device loss, push-buffer fault or guest
  stop. Only routine Android warnings.
- The app logged `HakuXP-ADB: Wireless debugging on, port 39307` as it
  started: the new status poll works on the device.
- The import line still lists nine kernel functions not implemented here;
  the game reached its menu regardless, so none of them blocked boot.

So the black picture in the first run belongs to the system driver, not to
the title: with Turnip it renders. Whether the system driver's black frame
is worth a quirk is a separate, lower question.
