# Steel Battalion (USA): release build, controller emulation and catalog capture

**Title:** Steel Battalion (USA), `43430002` `CC00201A`, XDK 4627.
**Image:** `Steel Battalion (USA).iso` on the Odin2's SD card (`/storage/6262-3239/ISOs and ROMs/Xbox/Steel Battalion (USA).iso`), 1,810,235,392 bytes.
**XBE:** `default.xbe`, 6,635,520 bytes, MD5 `63F797B8CA211455B41F50841C657611` (read from the image on the device with `tools/native/xbehash.py`).
**Device:** AYN Odin2 (kalama, QTI QCS8550, Adreno 740, 14.9 GiB RAM), Android 13 (API 33), `Odin2_V1.0.0.380_20250815_111346_user`.
**GPU driver:** PurpleVK 26.3.0-devel (`git-62ac221a33`), Turnip, custom package (`vk: driver "PurpleVK public driver" (id 18)`).
**APK:** Signed release `build/release/HakuXP-0.1.0.apk` (`zip.mobcat.hakuxp` `0.1.0 (1)`, 7,705,688 bytes, signer `CN=HakuXP`), SHA-256 `8a06da140ab5f70c5552ab66ae8af62eb3fccb6628c0edf83f02ee17c7a7d7b4` (verified identical on the device after `wireless.bat install`).
**Git commit:** `528f49e85fb71a151ab22c67b74282f56af81738` plus uncommitted local working-tree changes (including `HakuXP-App` version logging in `AppDiagnostics.kt`, `hakuX-native-xbelibs` logging in `native/core/xbe_load.c`, and `tools/catalog/`).
**Capture:** `build/logs/xbdm/2026-11-20_11-41-07.011671.*` (`debug.bat --package zip.mobcat.hakuxp`), Mark log picture `build/screenshots/2026-11-20_11-47-35_000E7943_Stuck-Idk-the-controles.png`.
**Catalog / .hxpb:** Run `c061b8e5229cff38` ingested into `tools/catalog/web/hakuxp.db`; archive `tools/catalog/web/runs/c061b8e5229cff38.hxpb` (241,156 bytes, verified with `catalog.bat inspect`).

## What was tested

1. **Release build and wireless install:** Built `build.bat release`, installed on the awake, unlocked Odin2 over wireless ADB (`192.168.68.4:35213`), and verified the device's `base.apk` SHA-256 matched `build/release/HakuXP-0.1.0.apk`.
2. **Live app capture (`debug.bat --package zip.mobcat.hakuxp`):** Captured both app-UID logcat (`.android.log`) and live XBDM NDJSON (`.ndjson`), plus automatic Mark log screenshot pull.
3. **New telemetry lines confirmed in the capture:**
   - `HakuXP-App: Process starting: zip.mobcat.hakuxp:native pid=26501 version=0.1.0 (1)`
   - `hakuX-native-xbelibs: [00000000] title 43430002 XAPILIB:1.0.4627,D3D8:1.0.4627,XGRAPHC:1.0.4627,DSOUND:1.0.4627,DMUSIC:1.0.4627,XBOXKRNL:1.0.4627,LIBCMT:1.0.4627`
4. **Gameplay run (`summary.bat`):**
   - Single process (`pid 26501`), 636.8 s (`11:43:51` to `11:54:28`), EC `00000000..002914BC`, clean exit via Exit to library (`[002914BC]`).
   - Reached boot, menus, first training mission intro cutscene (~60 fps), and 3D VT cockpit gameplay (locked 30.0 fps; 36.8 fps average across menus/cutscenes/cockpit, 60.0 max, 2.4 min during Guide pause).
   - 0 guest faults, 0 host faults, 0 dropped frames, 69 stutters (9 severe >= 80 ms, max 432.7 ms during early JIT/load), 16 APU trap warnings (`method ... while trapped: the trap's 8000 (...) kept`).
   - Five unimplemented kernel imports declared by the XBE (`IoInvalidDeviceRequest`, `IoStartPacket`, `IofCompleteRequest`, `IoStartNextPacket`, `IoMarkIrpMustComplete`), none called.
5. **Steel Battalion controller emulation (`0A7B:D000`):**
   - Enumerated cleanly as `player 1: Steel Battalion Controller 0A7B:D000`.
   - Game read button and gear changes (`COCKPIT_HATCH` `0x10`, `IGNITION` `0x20`, `START` `0x40`, fire/weapons, gear `-2/-1/1`) and sent 22-byte lamp reports.
   - Pressing `COCKPIT_HATCH` closed the hatch and opened the cockpit CRT screen (`Primary Systems Status Check - TOGGLE SWITCHES CHECK`), verified visually in `2026-11-20_11-47-35_000E7943_Stuck-Idk-the-controles.png` (crisp gauges, cockpit geometry, CRT display, EC stamp `000E793E`).
6. **Catalog and `.hxpb` pipeline:**
   - `catalog.bat pack ... --for-web --apk-sha ... --commit ...` produced `tools/catalog/web/runs/c061b8e5229cff38.hxpb` (241,156 bytes, 3 verified files, valid `.hxpb` v1).
   - `catalog.bat ingest ...` ingested run `c061b8e5229cff38` into `tools/catalog/web/hakuxp.db` (0 warnings).
   - `catalog.bat show c061b8e5229cff38 --timeline --libs` confirmed:
     - Build: `HakuXP 0.1.0 (1) 528f49e8` (`apk=8a06da140ab5`, `commit=528f49e85fb7`).
     - Resolution: `640x480` guest -> `1920x1080` host (`scale=1x`).
     - XDK 4627 and all 7 XBE libraries (`D3D8`, `DSOUND`, `XAPILIB`, `XBOXKRNL`, `LIBCMT`, `DMUSIC`, `XGRAPHC`) plus 123 kernel imports and 17 subsystems.
     - EC per-second stability: `median=4257.0/s`, `halts=27s` (includes Guide menu pause), `dips=96s`, `cv=0.398`.
     - Score: **77/100** (`stability=23.0/35`, `performance=27.01/30`, `visuals=12.0/15`, `completeness=14.5/20`).
     - Game rollup (`catalog.bat list --games`): Steel Battalion `79` across 2 runs (12m 05s total).
   - `catalog.bat progress`: `7 / 2179 tested (0.32%)`, `Score 80+: 4 (0.18%)`, `Score 60+: 7 (0.32%)`.

## Findings and leads

1. **VT startup blocked at `TOGGLE SWITCHES CHECK`:**
   - In `2026-11-20_11-47-35_000E7943_Stuck-Idk-the-controles.png`, the cockpit CRT shows `Primary Systems Status Check - TOGGLE SWITCHES CHECK` with all five bars (`FILT CONTROL SYS`, `OXYGEN SUPPLY SYS`, `FUEL FLOW RATE`, `BUFFER MATERIAL`, `VT-LOCATION MEASURENT`) at 0%, even though HakuXP pre-sets all five toggle switch bits ON from boot (`0x7c00000000`, `SbInput.TOGGLES_ON`) and both `IGNITION` (`0x20`) and `START` (`0x40`) reached the game (`sbc 0: buttons 7c00000060`).
   - `SbcMapping.defaults()` leaves the five toggle switches (and other panel switches/buttons) unbound, so the player cannot flip them OFF -> ON during the startup sequence on a standard handheld controller without custom bindings or an on-screen touch panel.
   - Owner follow-up direction: research the exact Steel Battalion VT startup sequence and add touchscreen buttons/overlay for one-time and cockpit-panel Steel Battalion controls (startup switches, toggles, APU/panel buttons) that do not fit on a handheld gamepad.
2. **Catalog visual PNG pickup for app-lane Mark log screenshots:**
   - `debug.bat` pulled the Mark log PNG into `build/screenshots/`, and the mark text appears in `run_marks`, but app-lane `catalog.bat pack` / `extract_debug_log` currently only probes PNGs inside playbook run directories, leaving the `-3.00: no frame PNGs captured to verify output` deduction on app-lane runs.
3. **Guide pause counted in EC halts:**
   - The 27 s of EC halts (`-8.00` stability) coincide with pausing in the Guide menu to enter the Mark log and check controls.

## Follow-up run: VT startup completed with SBC touchscreen cockpit overlay

- **Change:** Updated `CompiledPad.Sbc` / `SbcOut` so the five toggle switches start OFF (`0L`) instead of pre-set ON, and added `SbcOverlayView` / `SbcOverlayPainter` / `SbcOverlayModel` / `SbcOverlayPrefs` (persistent drawer button with tap & long-press caps-lock, bottom row of physical-style flipping toggle switches + Startup/Systems/Monitors/Comms groups, live `SbcLamps` backlighting, and independent drawer/per-group opacity sliders). Host tests: `test.bat jvm` 181/181 passed.
- **Run 2 capture:** `build/logs/xbdm/2026-11-20_12-27-42.920263.*` on signed release APK SHA-256 `6f629b30dc6c02e9b945b68f235db0bc10e2e491be742a835eb4d5dfaf4974df` (`pid 17544`, 386.2 s, EC `00000000..0016D6AD`, clean exit, 36.6 fps avg, 60.0 max, 130 stutters, 0 faults).
- **Verified on Odin2:** Owner flipped all five toggle switches (`FILT`, `OXY`, `FUEL`, `BUF`, `VTLM`) on the bottom touchscreen row, watched `START` light up and flash from the game's lamp report, and booted the VT cleanly past `Primary Systems Status Check` into active cockpit gameplay with HUD, overhead radar, damage readout, and hangar exterior visible (owner screenshots).
- **Catalog / `.hxpb`:** Packed `tools/catalog/web/runs/47abf0372db9c786.hxpb` (161,258 bytes, verified) and ingested into `tools/catalog/web/hakuxp.db` (score 75/100; 29 total runs in catalog).
- **Overlay cleanup build:** Removed `v` / `^` arrows from the `SBC` drawer pill, made the pill slimmer (`88dp x 28dp`), and disabled outside-tap auto-hide so misclicks outside a button never dismiss the panel. Built as `build/apk/HakuXP-0.1.0.528f49e8-release.apk` (7,740,280 bytes, SHA-256 `847d00948c88cd3c95b710bd94076f781db34ff65e7cef24527adec70b6e5a98`, verified installed on the Odin2; 181/181 JVM tests passed).

## Not tested

- Full mission completion and extended combat after VT startup.
- The overlay cleanup build (`847d0094...`: no `^`/`v`, no outside-tap dismiss) in a live game run on the Odin2 (installation and SHA-256 verified; JVM tests passed).
- Audio fidelity (verified `DSOUND` active and APU trap warnings logged, but audio output quality was not systematically checked).
- Playbook recording/replay with the Steel Battalion controller type (`0A7B:D000`).
- Savestates or in-game controller type swapping (`Duke` <-> `Steel Battalion`).
- Live upload and rendering of `c061b8e5229cff38.hxpb` / `47abf0372db9c786.hxpb` in `compatibility.php` on the web host.
