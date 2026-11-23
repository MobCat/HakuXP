# Building HakuXP on Windows

Run commands from the project root. In PowerShell use `.\build.bat` instead
of `build.bat` (and similarly for setup/test). The scripts also locate the
project correctly when invoked by absolute path from another directory.

## This machine

| Component | Selected path or version |
| --- | --- |
| Project | `F:\Projects\Xbox Projects\HakuXP` |
| JDK | `C:\Program Files\Java\jdk-21` (21.0.1) |
| Android SDK | `E:\android-kit\sdk` |
| NDK | `29.0.14206865` (Clang 21) |
| Android platform / build tools | API 36 / 36.1.0 |
| Native target | ARM64, Android API 28 minimum |
| CMake | `C:\Program Files\CMake\bin\cmake.exe` (3.29.0-rc2) |
| Ninja | `E:\android-kit\sdk\cmake\3.22.1\bin\ninja.exe` |
| Python | `C:\Python313\python.exe` (3.13.3) |
| Host test compiler | `C:\git-sdk-64\mingw64\bin\gcc.exe` (12.2.0) |
| Gradle / Android plugin / Kotlin | Wrapper 9.1.0 / 8.13.2 / 2.2.21 |

Machine paths live in ignored `scripts/buildenv.local.bat`. Use
`scripts/buildenv.local.example.bat` on another machine. The build environment
prefers native Windows CMake over the MSYS version first on this PC's PATH.
The configured GCC avoids the older TDM-GCC first on PATH.
The environment script strips embedded PATH quotes locally: this machine's
WSL entry contains a stray quote that otherwise breaks Java test workers.
It does not edit the system/user PATH. JVM tests use a fresh Gradle process
so a previously started daemon cannot retain the malformed library path.

## First build

```bat
setup.bat
build.bat doctor
test.bat
build.bat
```

Setup installs the pinned SDK packages if missing, fetches FEX plus its
required submodules and eight native dependencies, and applies two patches:

- Upstream's adrenotools KGSL context priority hook.
- HakuXP's FEX Python interpreter selection fix.

It uses the existing JDK and Android command-line tools. It does not install
Java, CMake, Ninja, Python or a host compiler. If SDK licenses are missing,
sdkmanager asks for them; read and accept them interactively before retrying.
Existing source checkouts must match their configured origin and revision.
Setup preserves local edits, rejects mismatches, and retries incomplete
fetches. Required patch conflicts stop setup rather than overwrite edits.

The exact source revisions remain in `setup.bat` and upstream's table in
`android/README.md`. FEX is pinned to `52018bf46a8279b22bce738548ab3f40db467064`.
Keep both saved patch files when publishing/recreating the fork.

## Commands

| Command | Result |
| --- | --- |
| `setup.bat sdk` | Install/check SDK packages only |
| `setup.bat sources` | Fetch/check native sources and apply patches only |
| `build.bat doctor` | Check all required tool/source paths; no build |
| `build.bat` | Check state schemas, configure/build core, stage libraries, assemble debug APK |
| `build.bat core` | Build `libhakuxcore.so`, `libhakuxdebug.so`, `xboxrun` and `xbdm_smoke` for Android |
| `build.bat libs` | Strip and copy all eight libraries from the last core build |
| `build.bat apk` | Stage last native build and assemble debug APK |
| `build.bat release` | Build the core, then the signed release APK, copied as `build/apk/HakuXP-<version>.<commit>-release.apk` with its signer and SHA-256; stops without `android/key.properties` (see "Release builds") |
| `build.bat release-apk` | The same from the last native build (no core build) |
| `build.bat release-check` | Build the core and inspect an unsigned release APK without a signing key |
| `build.bat keystore` | Make HakuXP's release key and `android/key.properties`, once; asks for the password |
| `build.bat install` | Install the newest signed release APK (`build/apk/HakuXP-*-release.apk`) using ADB, on `ANDROID_SERIAL` or the device `wireless.bat use` remembers; does not build it |
| `build.bat install-debug` | Install the newest debug APK (`build/apk/HakuXP-*-debug.apk`) using ADB; does not build it |
| `install.bat` | Install the last signed release build on the test device (Wi-Fi or USB), saying which device and how long it took; `install.bat build` builds release first, `install.bat debug [build]` targets the debug APK |
| `wireless.bat` | Wireless ADB: `pair`, `connect`, `use`, `status`, `tcpip`; see "Wireless debugging" below |
| `build.bat reconfigure` | Remove CMakeCache.txt and configure again |
| `build.bat clean` | Remove native output under project `build/`, then Gradle clean |
| `test.bat` | Build/run all 15 HLE host tests |
| `test.bat test_pusher` | Run matching CTest names; no matches is an error |
| `test.bat clean` | Recreate the host test build and run it |
| `test.bat jvm` | Run Android JVM tests without a device |
| `test.bat state` | Check all 17 savestate section locks and schemas |
| `test.bat xbdm` | Build/run three standalone XBDM debug harness tests |
| `test.bat xbdm client` | Also test the configured HX_XBDLOG client and real PC capture |
| `test.bat wireless` | Test `wireless.bat`'s tool against a simulated adb (no device), and `summary.bat`'s on a made-up capture |
| `debug.bat` | Capture XBDM logs on the PC through ADB; see DEBUGGING.md |
| `screenshot.bat` | The test device's screen as a PNG in `build/screenshots` (`<date>_<time>.png`, or a name given); prints the path. With the FPS overlay on, the frame shows the emulation count (below) |
| `summary.bat` | A short summary of each game launch in the newest capture (or a given `*.android.log`): image, title ID, GPU driver, how far boot got, how it ended, unimplemented imports, zero-filled SCSI commands, frame rate; `--json` for scripts |
| `python scripts/todo-scan.py` | Refresh the source TODO/FIXME inventory |

The default native build is Release, even inside the debug APK. Set
`HX_BUILD_TYPE=RelWithDebInfo` in the local environment for symbols and
optimization, or `Debug` for a native debug build. `HX_JOBS` defaults to 6;
lower it if compiler memory use is excessive. `HX_ARCH` defaults to generic;
set a more specific ARM architecture only after choosing the device baseline.
Build mode `core` reconfigures incrementally so these changes take effect.
Use a separate `HX_BUILD` directory when changing NDK/compiler or ABI.
Run APK builds and JVM tests sequentially in the same checkout; separate
Gradle processes can race over generated resource files. To run both in
one invocation after staging libraries, use `assembleDebug testDebugUnitTest`
as the tasks passed to `android/gradlew.bat` from the Android directory.

Clean refuses paths outside a subdirectory of this project's `build/`,
including redirected paths. Remove external build directories manually.
Clean does not remove staged `jniLibs`; a subsequent full build refreshes
them. Do not use `apk` after cleaning until the native build is restored.

## Outputs and installation

- Debug APK: `build/apk/HakuXP-<version>.<commit>-debug.apk` (raw Gradle output: `android/app/build/outputs/apk/debug/app-debug.apk`).
- Release APK: `build/apk/HakuXP-<version>.<commit>-release.apk` (raw Gradle output: `android/app/build/outputs/apk/release/`).
- Native core and symbols: `build/native/core/libhakuxcore.so`.
- Device CLI: `build/native/core/xboxrun`.
- Device debug smoke test: `build/native/core/xbdm_smoke`.
- PC protocol harness: `build/xbdm-tests/xbdm_host.exe` (not a Windows emulator).
- PC debug logs: `build/logs/xbdm/`.
- Stripped libraries: `android/app/src/main/jniLibs/arm64-v8a/`.
- Host test report: `build/hle-tests/Testing/Temporary/LastTest.log`.
- JVM report: `android/app/build/reports/tests/testDebugUnitTest/index.html`.

Gradle does not compile native code. The eight libraries must be staged
first; `build.bat` does this and stops on any missing/failed library.
For profiling, retain the matching unstripped `.so` before rebuilding.

HakuXP has its own package: `zip.mobcat.hakuxp` for release builds and
`zip.mobcat.hakuxp.debug` for debug builds, so it installs next to hakuX
(`com.rfandango.haku_x`) and the two builds install next to each other. The
launcher and Android's app list name the release "HakuXP" and the debug
build "HakuXP Debug" (`launcher_name` in `android/app/build.gradle.kts`);
inside, both say HakuXP. Only the application id changed: the Kotlin
package and the activity classes are still upstream's
(`com.rfandango.haku_x.LauncherActivity`), so an `am start` names
`zip.mobcat.hakuxp.debug/com.rfandango.haku_x.LauncherActivity`.
`adb install -r` can replace a matching package/signature and will reject a
mismatched signature. No device is installed to by default. Select a
specific device by setting `ANDROID_SERIAL` before `build.bat install`, or
remember one with `wireless.bat use`.

### Moving from the old package

Builds before 2026-11-18 installed as `com.rfandango.haku_x.debug`. The new
package is a new app to Android: it starts with no settings, games folder,
hard disk or playbooks, and the old one stays installed until you remove it.
To bring things across:

- The console (saves, game data, its EEPROM, system settings, savestates):
  in the old app, Settings > Console > System > Hard disk > Back up
  everything; in the new one, Restore a backup.
- Playbooks and their states (shared storage, readable over ADB):

  ```bat
  adb shell cp -r /sdcard/Android/data/com.rfandango.haku_x.debug/files/playbooks /sdcard/Android/data/zip.mobcat.hakuxp.debug/files/
  ```

  Start the new app once first, so its folder exists.
- The games folder, the GPU driver, controller layouts and app settings: set
  them again in the new app.

## Release builds

Run `build.bat release-check` to build and inspect the release before
setting up signing. It stages the native libraries, runs Gradle's
`assembleRelease` (including R8 and release vital lint), and reads the
finished APK. It checks the application ID, version against Gradle's output
metadata, debuggable flag, ARM64 ABI, required libraries, absence of debug
Vulkan layers, ZIP alignment and bundled notices. The output is
`android/app/build/outputs/apk/release/app-release-unsigned.apk`.

A release APK is signed with HakuXP's own key, or `build.bat release` stops:
an unsigned APK can't be installed, and Android installs an update only over
an app signed with the same key. Make the key once:

```bat
build.bat keystore
```

It asks for a password (twice, hidden), makes
`%USERPROFILE%\.android\hakuxp-release.jks` (PKCS12, RSA 4096, about 27
years) and writes `android\key.properties` (`storeFile`, `storePassword`,
`keyAlias`, `keyPassword`; the password in plain text, and both files kept
out of git by `android/.gitignore`). Back up the key store and its password:
a lost key means users must uninstall HakuXP, and lose its data, to install
a later release. On another PC, copy the key store back and run
`build.bat keystore` again: it checks the password against the existing key
store and writes only `key.properties`. `HX_KEY_PROPERTIES` (in
`scripts\buildenv.local.bat`) points at a `key.properties` elsewhere; its
`storeFile` is absolute or relative to that file.

Then `build.bat release` builds the core and the APK, checks the signature
with `apksigner`, and copies it to `build\release\HakuXP-<version>.apk`,
printing the signer's certificate digests and the APK's SHA-256 for the
release notes. Both signed build commands run the same package checks.
`build.bat release-apk` skips the core build.

When publishing, include the source revision used to build the APK,
including the pinned dependencies and the patches applied by `setup.bat`.
The APK carries the repository's `LICENSE`, `COPYING` and third-party
notices under `assets/notices/`.

- Version: `hakuxp.versionName` and `hakuxp.versionCode` in
  `android/gradle.properties` (HakuXP's own numbering, from 0.1.0 / 1;
  overridable with `-Phakuxp.versionName=...` / `-Phakuxp.versionCode=...`).
  Raise `hakuxp.versionCode` for every release.
- Update checks: the build names HakuXP's GitHub releases,
  `hakuxp.updateRepo=MobCat/HakuXP` in `android/gradle.properties` (another
  with `-Phakuxp.updateRepo=owner/name`). The app checks that repository's
  latest release at start and in Settings > General, and installs only an
  APK that is `zip.mobcat.hakuxp` signed with the same key. A release is
  seen when it is a full release (GitHub's "latest" skips drafts and
  pre-releases), tagged `v<versionName>` with a higher version than the
  installed app, with the APK attached (the first `.apk` asset, such as
  `build/release/HakuXP-<version>.apk`). With no release yet the check
  finds nothing and says the app is up to date. An empty value
  (`-Phakuxp.updateRepo=`) turns checks off, and Settings > General then
  says the build has no update source.
- Debugging: release builds start with Settings > HakuXP > Debug >
  Debugging off (no XBDM service, overlays, guide Debug tab or playbooks);
  debug builds start with it on. See docs/DEBUGGING.md.

### Release checks on 2026-11-19

- `build.bat release-check` built `zip.mobcat.hakuxp` 0.1.0 (1). Release
  vital lint passed. The unsigned APK was 7,697,188 bytes, SHA-256
  `c243e820b3306a60f9840247c4d82ecd68ad5712f511ddae9440c206ffe6dab0`.
  Its 22 notice files and 18 mod files matched the source files byte for byte.
- `build.bat release-apk` ran in a temporary copy of the app project with
  the same native build and a disposable key. Android's `apksigner` verified
  the result. The versioned copy matched the verified APK. The temporary
  project and key were removed. The normal checkout stopped at the missing
  owner signing configuration when running `build.bat release`.
- All 29 native host tests, three XBDM host tests and 176 Android debug JVM
  tests passed.
- After release signing was configured, the 20:11 build stopped at
  `:app:processReleaseResources` because the AAPT2 daemon failed to start.
  At 20:14, rerunning `build.bat release` ran the resource task successfully
  and produced `build/release/HakuXP-0.1.0.apk` (7,705,380 bytes, SHA-256
  `8daa44be28a62245d0b8d97a19339ad93c72e685ecb55ba4c5df3f917795ba4f`).
  `apksigner verify --verbose --print-certs` verified the final copy's v2
  signature with signer `CN=HakuXP`. On-device testing of this APK is pending.

## Wireless debugging

Android 11 and up can run ADB over Wi-Fi with no cable (the Odin2 runs 13).
Everything that uses ADB then works wirelessly: `build.bat install`,
`debug.bat`, `adb forward` for XBDM, and the test runs to come.

Once per PC, with the device and the PC on the same network:

1. On the device: Developer options > Wireless debugging > on. HakuXP's
   Settings > HakuXP > Debug > Wireless debugging has a button that opens Developer
   options, and shows the device's address.
2. Tap **Pair device with pairing code**. Keep that dialog open.
3. On the PC: `wireless.bat pair`. It finds the device offering to pair and
   asks for the six-digit code. If the network hides it, pass the address
   the dialog shows: `wireless.bat pair 192.168.1.23:41234`.
4. `wireless.bat use` remembers the device (by its hardware serial, in
   `scripts/device.local.bat`, not versioned).

After that the PC connects by itself whenever the device has Wireless
debugging on (adb's mDNS discovery). `wireless.bat` shows what is connected,
what the network announces and where the remembered device is: the
connection marked "the scripts use this" is the one every script takes,
and the announcement adb is on reads "paired, connected". With both
USB and Wi-Fi connected, the scripts use USB. `ANDROID_SERIAL` overrides
the remembered device. `wireless.bat forget` drops it.

A device often announces itself twice: once more as `<name> (2)` when
Wireless debugging restarts while the network still remembers the old
announcement. Its serial then has a space in it
(`adb-c3e8b774-IKUygG (2)._adb-tls-connect._tcp`). That is still one
device: `wireless.bat connect` keeps the connection adb has (or tries the
latest announcement first), and `wireless.bat use` with nothing named
takes the one device whatever its connections. Only with several different
devices does either show a numbered list to pick from (run by hand; in a
script it stops with the list instead).

If discovery doesn't work, connect by hand with the IP address and port
from the Wireless debugging screen: `wireless.bat connect 192.168.1.23:37555`.
That port changes whenever Wireless debugging restarts, so reconnecting is
by hand too until discovery works. Discovery listens for multicast replies
(mDNS, UDP 5353), which two things commonly stop:

- The Windows firewall, when it has no rule for `adb.exe` (this PC had none
  on 2026-11-15). Allow it on private networks, from an administrator
  Command Prompt; it is your call, since it changes the firewall:
  `netsh advfirewall firewall add rule name="adb (Android SDK)" dir=in action=allow program="E:\android-kit\sdk\platform-tools\adb.exe" profile=private`
- A router that filters multicast between Wi-Fi and wired clients, or
  isolates Wi-Fi clients. That is a router setting.

Before suspecting either, check that Wireless debugging is still on:
Android turns it off by itself, for example when Wi-Fi drops or the device
joins another network. On 2026-11-15 the PC (wired, 192.168.68.13) first
saw nothing from the Odin (Wi-Fi, 192.168.68.6) while it answered ping;
Wireless debugging had switched itself off. Once it was back on, discovery
found and reconnected the Odin with no firewall change. Pings to it were
slow and lossy (86 to 283 ms, one of two lost), so expect wireless installs
to be slower than USB.

**Is it on?** Settings > HakuXP > Debug > Wireless debugging > Status shows it live
(the debug service reads adbd's `persist.adb.tls_server.enable` and
`service.adb.tls.port` every second; Android doesn't let apps read the
`adb_wifi_enabled` setting itself), and while it's on, the `IP:port` a
paired PC connects to, which is what `wireless.bat connect` takes when
discovery can't find the device. Each change goes to the app log as
`HakuXP-ADB: Wireless debugging turned off` (or `on, port N`), so a
`debug.bat` capture records when it went off. **Keep the screen on while
debugging** in the same section (off by default) stops the device from
sleeping and locking while Wireless debugging is on and HakuXP runs, open
or in the background; the log shows `HakuXP-ADB: Keeping the screen on`
and `No longer keeping the screen on`. The power button still turns the
screen off. It needs the app running: headless `xboxrun` runs don't start
it. On Android 10 and
older, `wireless.bat tcpip` moves a USB-connected device onto its Wi-Fi
address at port 5555, until it reboots.

Wireless ADB is slower than USB for large copies. Copy game images to the
device over USB or with a file manager.

## Verification on 2026-11-14

- SDK setup completed; all required paths pass `build.bat doctor`.
- Source setup rerun preserved pinned revisions and the adrenotools patch.
- Script checks passed for incomplete-fetch recovery, preservation of local
  edits, revision-mismatch rejection, guarded clean and failure exit codes.
- All 15 HLE host tests passed with GCC 12.2 and warnings treated as errors.
- All 17 savestate section locks matched.
- Native Release core, SDL3, five hook libraries and `xboxrun` built with
  NDK 29; the seven app libraries staged successfully.
- Debug APK assembled successfully: 22,889,910 bytes. `apksigner verify`
  passed; all native libraries in it are ARM64, and all seven core libraries
  match the staged outputs byte-for-byte.
- All 18 Android JVM tests passed across six test classes, with no skips.
- Release signing and on-device runtime behavior have not been tested.

The initial APK SHA-256 is
`3f16ef81fedcd3392ba0d5f1efe1d622f9357ce92eb6e1396338353baf8f6f8f`.
Local validation logs are in ignored `build/audit/` (`build-final.log`,
`host-tests.log`, `jvm-validation.log`, `setup.log`).

The first test build exposed outdated positional `HlePbSink` initializers
in `test_pusher.c`; named fields fixed this without weakening warnings.
The first native build exposed FEX's hardcoded `python3` calls; the saved
patch makes the three core generators use `Python_EXECUTABLE`.
An overlapping Gradle build/test attempt collided on generated resources;
a single clean invocation rebuilt the APK. The JVM launcher failure was
then traced to the PATH quote described above and resolved locally.

Device boot, rendering, audio, controllers, save/load and performance still
need runtime validation on selected Android hardware.

## Debug harness update on 2026-11-15

The APK and Android smoke executable rebuilt successfully with the XBDM
logging service. Three additional host CTest cases pass, plus both local
xbdlog clients in callback and same-socket modes and the PC launcher
integration checks. The 15 HLE tests still pass. See [DEBUGGING.md](DEBUGGING.md)
for tested behavior, scope and pending device validation. The APK size/hash
above describe the initial build, before this debug service was added.

The updated debug APK is 25,257,697 bytes, SHA-256
`af4e58100d3f1699f48a91e890a05ff95c93b68a8c6321b3cb90359c8926df0a`.
Its signature verifies, and its seven ARM64 core libraries match the staged
outputs byte-for-byte. Logs: `build/audit/xbdm-build.log`, `xbdm-tests.log`,
`xbdm-original-client.log` and `xbdm-hle-tests.log`.

## Tabbed settings update on 2026-11-15

The global and per-game settings screens now use category tabs. The debug
APK builds, all 18 Android JVM tests pass, and its signature verifies. A
source audit confirms the existing setting labels, preference writes and
per-game override definitions were retained. Device layout/input checks
remain pending because ADB has no connected device.

The freshly packaged APK is 22,987,586 bytes, SHA-256
`92311a8c35c8e5d7c789a657cc7e82070e5da6ed26e42c9267795a5dc19803b6`.
Its seven native core libraries still match the staged outputs. Fresh
packaging removed unused space left by incremental APK updates. Logs and
the audit are in `build/audit/settings-validation.log`,
`settings-package.log` and `settings-verification.json`.

## Settings regression checks

`test.bat jvm` now includes Robolectric/Compose UI checks alongside the
existing 18 JVM tests. They exercise the actual settings screen on portrait
and landscape host layouts, plus lazy row composition and per-tab scroll
restoration. Robolectric downloads its Android runtime on the first run.

The first layout reproduced a bug: the last tab ended at x=552 on a
900-pixel-wide screen. The corrected navigation uses the full width when
the labels fit. Smaller screens and larger fonts retain scrolling labels.
Content uses a full-size lazy list; folder labels and GPU metadata load off
the UI thread. The four UI checks and 18 existing tests pass. Device frame
timing, controller focus and physical-device rendering still need a device.

Baseline evidence: `build/audit/settings-regression-baseline.xml`.
Validation log: `build/audit/settings-layout-tests.log`.

The rebuilt APK reports application label `HakuXP` and its signature verifies.
It is 23,060,656 bytes, SHA-256
`1a06d256e69e023d09db5305c04fc65dcce0f5171a6c4be1619b22c34bb0ce9a`.
Build log: `build/audit/hakuxp-settings-build.log`; verification record:
`build/audit/hakuxp-settings-verification.json`. Cover-art integration was
deferred at the user's request; no cover lookup was added to this APK.

## App-wide XBDM update on 2026-11-15

The Debug tab now controls an app-wide monitor hosted in a separate bound
service. `build.bat` builds and stages the eighth ARM64 library,
`libhakuxdebug.so`, which only depends on Android system libraries. The
launcher can log before SDL, FEX, Vulkan or a game is loaded. `debug.bat`
also saves a separate app-UID Android log on the PC for early failures.

Validation: 37 JVM/UI tests pass, all three XBDM CTest cases pass, and the
configured unmodified xbdlog client passes both notification dialects and
PC log survival checks. Additional checks cover repeated listener restarts,
subscriber counts, app UID filtering across PIDs, and capture/setup cleanup
failures. These host checks do not establish on-device service/logcat behavior.

The APK signature verifies, its label is `HakuXP`, and all eight packaged
ARM64 libraries match the staged outputs. The final APK is 23,140,108 bytes,
SHA-256 `3f10741b273ce9dcdd65e8a6e5f36c3511ab498c232c9ad76db7e36ef487f85a`.
Evidence: `build/audit/xbdm-app-build.log`, `xbdm-app-jvm.log`,
`xbdm-app-host.log`, `xbdm-app-signature.log` and `xbdm-app-verification.json`.
ADB still reports no connected device; installation and live testing remain
pending. See [DEBUGGING.md](DEBUGGING.md) for the launcher-only test procedure.

## Re-check on 2026-11-15

A second session re-ran the whole pipeline on this machine after the XBDM
and settings work. `build.bat` stopped at Gradle with "'gradlew.bat' is not
recognized": `build.bat` and `test.bat jvm` called it by bare name, and cmd
doesn't search the current directory when `NoDefaultCurrentDirectoryInExePath`
is set (it was, in that shell). Both now call `android\gradlew.bat` by full
path. After the fix:

- `build.bat doctor`: every path OK.
- `build.bat`: runs through. Ninja and Gradle found the core and the APK
  up to date (no sources changed since the last session's build), so this
  re-check is not a from-scratch compile; the eight libraries were stripped
  and staged again. Debug APK 23,140,104 bytes.
- `test.bat`: 15 of 15 HLE host tests pass. `test.bat state`: 17 sections
  match their locks. `test.bat xbdm`: 3 of 3 pass.
- JVM/UI: 38 tests, no failures or skips, on a forced rerun
  (`testDebugUnitTest --rerun`; Gradle otherwise reuses the last result).
- No device attached: nothing installed or run on hardware.

## Wireless debugging build on 2026-11-15

`wireless.bat` (tools/native/adb_wireless.py) and the Wireless debugging
section in Settings > HakuXP > Debug. `build.bat install` and `debug.bat` use the
device `wireless.bat use` remembers.

- `test.bat wireless`: 23 of 23 pass, against a simulated adb (pairing,
  discovery, USB-over-Wi-Fi preference, connect failures reported in adb's
  text, the remembered-device file).
- `wireless.bat` ran against the real adb 37.0.1: no device connected, no
  wireless service seen on the network, nothing remembered.
- JVM/UI: 43 tests pass (5 new: address filtering, the section's rows and
  button, the section in the real Debug tab). `test.bat xbdm`: 3 of 3.
- Native core unchanged (Ninja: no work to do).
- Debug APK repackaged from scratch: 23,161,348 bytes, SHA-256
  `9e30866d7a4f32e966d989c9ac53908c24392461b315194f3a4907e07c8945d8`,
  label HakuXP, `com.rfandango.haku_x.debug` 1.3.0 (9), signed with this
  PC's debug key. An incremental package was 33.6 MB, 10.5 MB of it gaps
  left by Gradle; delete `app-debug.apk` before `build.bat apk` to drop them.
- Not yet checked on a device: pairing, discovery, wireless install, the
  new section on the Odin.

Later the same day, on the Odin2 (Android 13, `Odin2_V1.0.0.380`): with
Wireless debugging back on, discovery listed it and adb connected by
itself (`adb-c3e8b774-IKUygG._adb-tls-connect._tcp`, 192.168.68.6:39307);
`wireless.bat use` remembered it as `c3e8b774`. Under the app's own
identity (`run-as`), `getprop` read `persist.adb.tls_server.enable` (1) and
`service.adb.tls.port` (39307), while `settings get global
adb_wifi_enabled` was refused. That is what the live Status row reads.

Then the Status row: `libhakuxdebug.so` may read those two properties too,
the debug service polls them and logs each change (`HakuXP-ADB`), and the
section shows on or off and where a PC connects. JVM/UI: 49 pass. Wireless
tool: 23. XBDM: 3. Debug APK 23,170,796 bytes, SHA-256
`5980389a074ba10c38b2c83a80b2628092ac41bc8b4cbdc1f1bac06f6d13f91a`.

Installed on the Odin over Wi-Fi with `build.bat install` (on the
remembered device): "Performing Streamed Install / Success" in 3 seconds,
despite the slow pings. The installed `base.apk` has the same SHA-256 as
the build; `lastUpdateTime` 2026-11-15 14:04:59. The GPU driver package
the owner had added at 14:02 (Turnip T30, Mesa 26.3.0) was still installed
afterwards: `install -r` keeps the app's data.

## SCSI logging build on 2026-11-15

`scsi_pass_through` (`native/kernel/io.c`) logs the whole CDB and the
direction of every SCSI command it answers with zeros, for Midtown
Madness 3's READ DVD STRUCTURE. Native core rebuilt (`io.c`, no warnings).
Debug APK 23,170,804 bytes, SHA-256
`896701e5cde1f6520b34925ceab983dc10058977f5abd9a7dfa6e5108bf456ee`.
Installed on the Odin over Wi-Fi with the new `install.bat` in 2 seconds,
after the owner closed HakuXP and stopped `debug.bat`; the installed
`base.apk` has the same SHA-256. Wireless tool tests: 28 pass (5 new, for
`install`).

The first run of Midtown Madness 3 with that build logged `cdb` with no
bytes: the title leaves the pass-through's CdbLength at 0. The next build
shows ATAPI's 12 bytes when CdbLength is 0 (native core rebuilt, `io.c`, no
warnings). Debug APK 23,170,848 bytes, SHA-256
`5120faed041db887a1cc700f5d1356089966aef1cac428896807df9f8b36d224`.
`summary.bat` (tools/native/runlog.py) added; `test.bat wireless`: 28 + 3
pass.

## Title IDs and XMIDs in the logs, 2026-11-15

Title IDs print in upper case everywhere in the core's logs (and the
title-update lookup's `TDATA\%08X`, which matches folder names
case-insensitively, as FATX does). The stage S0 line and the library's
launch line add the XMID, derived from the XBE certificate as the owner's
XBELib (`F:\Projects\go\XBELib\XBEJson.go`) does it:
`stage S0 xbe loaded: title 4D53002A xmid MS04204A ...` and
`launch: title=4D53002A xmid=MS04204A path=...`. `summary.bat` reads both
and still reads older captures.

- `test.bat`: 15 of 15 (test_xbe gained the XMID cases, from the owner's
  ROM database: MM3 USA `MS04204A`, Japan `MS04202J`, RSC2 `MS05701W`).
- JVM: 51 (`DiscInfoXmidTest`, the same cases). `test.bat wireless`: 28 + 3.
  `test.bat state`: 17 locks match.
- Native core: 37 objects rebuilt, no warnings. Debug APK 23,172,428 bytes,
  SHA-256 `9aa8882e3c1ddfb5ae13c501dc9617a0c074f13238af0f94109a6d737f8ed948`.
  Includes the 12-byte CDB logging. Not installed yet.

## Security sector build, 2026-11-15

The emulated drive answers XAPI's security-sector read with a sector it
builds and signs with HakuXP's own key (`native/kernel/xbox_ss.c`,
`xc_emukey.h`), and the kernel exports that key as `XePublicKeyData`
(docs/research/2026-11-15-security-sector-check.md). `test.bat`: 16 of 16
(new `test_ss`). Native core rebuilt (`xbox_ss.c`, `kernel.c`, `io.c`),
no warnings; savestate locks matched. Debug APK 23,178,236 bytes, SHA-256
`f3aee1536e26d51e16daf3a76d1942c11d505e5429899678b30c39618bc6aa7e`. Not
installed yet.

## Emulation count and screenshots, 2026-11-15

Every native log line (core, HLE GPU, guest DbgPrint) starts with the
emulation count in hex: the kernel's total of emulated kernel calls
(`k->calls`, kept in savestates), e.g. `hakuX-native-io: [0001A2B3] ...`;
a guest line has it after its `[guest-thread=...]` marker. With the FPS
overlay on (`stamp=1` at launch), each presented frame shows the same
count in its top right corner, as 7-segment hex digits drawn with clears in
the present pass (`hw/xbox/hle/util/stamp.c`, `hle_present_stamp`), turned
with the swapchain's pre-rotation. A frame left unchanged keeps the count
it was presented with. `screenshot.bat` saves the screen;
`summary.bat` reads the counts (each launch's range, the stop line's,
problems') and the security sector line; `imports.py` reads the prefix.

`test.bat`: 17 of 17 (new `test_stamp`). JVM: 51. XBDM: 3.
`test.bat wireless`: 31 + 3. Native core rebuilt (73 objects), no
warnings. Debug APK 23,178,496 bytes, SHA-256
`a39a8ffee03218f393eee3e3959a757b7cfabca93f3c5fec8cb4033f0214f8bd`. Not
installed yet; the stamp is unchecked on a device (its rotation in
particular).

The stamp was checked on the Odin2 on 2026-11-15 (APK `a39a8ffe`): upright
and readable in the top right corner, e.g. `00936498` on Midtown Madness
3's title screen and `022616C0` on its results screen, matching the log's
`[022606D5] stutter #60` a moment earlier.

## Overlay layout, 2026-11-15

The FPS overlay holds three short lines: the frame rate, `input lag: --`
(a placeholder) and the running XBE's `XMID | title ID` (JNI
`nativeGetTitleIds`, from the loaded certificate). The stutter block below
it (`hle_hitch_overlay`) puts each reason on its own line and shows the
stutter or hang number, which the full log line also has. `test.bat`: 17;
JVM: 51. Debug APK 23,179,240 bytes, SHA-256
`f2a038c416771e373906c9dbe6f64ae19ec313dd662e2fbd4bf64e76409d12f7`. Not
installed yet.

## One app icon, 2026-11-15

The launcher icon comes from `android/app/icon/icon.png` alone
(`android/app/icon/README.md`): Gradle's `generateLauncherIcons` (before
`preBuild`, only when the image changes) writes the adaptive icon's
foreground and monochrome layers at mdpi to xxxhdpi into
`build/generated/launcherIcons/res`; the background is the colour
`ic_launcher_background`. Removed: 20 per-density PNGs, the duplicate
`ic_launcher_round.xml` (the manifest's round icon is `ic_launcher`) and
x1box's `ic_launcher-playstore.png`. The seed `icon.png` is hakuX's star with
its white square turned transparent (colour to alpha; identical over the
white background), so the monochrome layer has the star's shape. The APK
holds 11 icon entries; `aapt2 dump badging` resolves the icon to the
adaptive XML at every density. JVM: 51. Debug APK SHA-256
`0ef74a2ee7e335fa8956b7865a8a4ca94df7829b10948946ae263c4da930911f`. Not
installed yet.

## Device and CPU in the logs, 2026-11-15

Each game process logs, first thing (`nc_device_log`, `native/core/devinfo.c`;
also `xboxrun`): `hakuX-native-device: device: AYN Odin2 (kalama) | SoC QTI
QCS8550 (kalama) | Android 13 (API 33), <build> | RAM 14.9 GiB` and `cpu: 8
cores: 1x Cortex-X3 3.19 GHz, 2x Cortex-A710 2.80 GHz, 2x Cortex-A715 2.80
GHz, 3x Cortex-A510 2.02 GHz | atomics lrcpc ilrcpc flagm flagm2` (the
Odin2's values, read as the app with `run-as`; the CPU line is
`native/core/cpuinfo.c`, host test `test_cpuinfo`). `summary.bat` shows
them, and the GPU driver from the HLE GPU's `vk: driver "NAME" (id N) INFO`
line. `test.bat`: 18; JVM and XBDM pass. Debug APK SHA-256
`a0b0e99a5fc74deee03e154aeffda3255a07c391e4ad48684a8c2ca9fa0e3eac`. Not
installed yet.

## Kernel area 128 KiB, 2026-11-15

`NC_KERNEL_SIZE` is 0x20000 (was 0x40000): the kernel pool's first range
moves from 0x50000 to 0x30000 and 0x40000-0x61000 is free for titles,
which Halo's D3D needs (docs/testing/2026-11-15-halo-visibility-test.md).
A failed contiguous request logs its caller, range and alignment. Native
core rebuilt, no warnings; `test.bat`: 18 of 18 (no host test covers the
kernel). On the Odin2, headless `xboxrun` with Halo's save: host fault
before, runs in the level after. Debug APK 25,408,363 bytes, SHA-256
`18c430dd8915b731646535bdb6f89844c2d0c50e33e680cca8cef0d52200b567`.
Installed on the Odin2 at 18:34 with `install.bat` over Wi-Fi (3 s); the
device's `base.apk` has the same SHA-256.

## Overlay, input timings and dashboard icon, 2026-11-15

The stutter overlay drops its reason lines (the stutter or hang number and
the count stamp find them in the log). The FPS overlay's second line is
`Input: Out Nms / diff Nms` (`native/dev/inlat.c`, `xid.c`, display.c's
`hle_disp_on_shown`, pbdraw.c's `hle_pbd_swap_id`; JNI
`nativeGetInputTiming`). The library's dashboard button shows the HakuXP
icon: `generateLauncherIcons` also writes `drawable-<density>/ic_hakuxp.png`,
the artwork cut to its bounds at 24 dp. Native core rebuilt, no warnings;
`test.bat`: 19 of 19 (new `test_inlat`); `test.bat wireless`: 31 + 4. Debug
APK 25,411,310 bytes, SHA-256
`ae67befb6564a06d12030775dc0843277fb08c1c57346d2e6f39fed8aaef9b39`, installed
on the Odin2 at 19:10; the library's top bar shows the new icon (screenshot).
The overlay and input line are not checked in a game yet.

## The EC across save states, 2026-11-15

The log's emulation count is its own (`native/core/log.c`: the kernel's
call total plus an offset), not `k->calls`: a state load keeps it going
(`nc_log_count_continue` in `kstate.c`), and a new core process of the same
session starts one past the last (`EXTRA_EC` through RelaunchActivity, the
core's `ec=`). `test.bat`: 19 of 19. Debug APK 33,531,352 bytes (this build
stores `classes.dex` uncompressed), installed on the Odin2 at 20:16.
Checked there with Halo started from the PC (a VIEW intent to the
launcher) and `debug.hakux.state`: saved to slot 1 at `01237B7A` (state
10.7 MiB, plus a 185 MiB gzipped store of Halo's disk cache), loaded it:
the old process logged `00000000..0123CA38`, the new one started at
`0123CA4F` (its `ec=`) and went on to `01243F73`. The test state was
removed afterwards (Halo had none before).

## The guide, 2026-11-15

The Back menu is now the guide (`android/app/src/main/java/com/rfandango/haku_x/guide/`,
`GameGuide.kt`; mockup: the HakuXP Guide Menu canvas): a ComponentDialog
hosting Compose over the paused game, four tabs, rows with one dot, a detail
pane per row, controller keys handled in the screen (LB/RB, D-pad, A, B, X,
Y, Start/guide). New native calls: `nativeMarkLog` (a
`hakuX-native-mark` line), `nativeSetStamp`. Screenshots and mark pictures
go to `Pictures/HakuXP` (MediaStore); `debug.bat`'s log reader pulls a
mark's picture into `build\screenshots` (`tools/native/marks.py`). Rumble
strength scales the router's vibration. Native core rebuilt, no warnings;
`test.bat`: 19; JVM: pass (new `GuideTextTest`, 4); `test.bat wireless`:
31 + 4 + 3 (new `test_marks`). Debug APK 33,719,196 bytes, SHA-256
`58d97c4ddb953f2b01d8fc02b1d63ac517174d33f0cdfe6589835441aa7f16f7`, installed on the Odin2 at 23:23. Not checked on screen:
the Odin2 was locked (screen off), so the guide's look, keys and actions on
the device are untested.

2026-11-16, first look on the Odin2 (the owner's screenshots): the tabs
were squeezed to a letter a line and pushed the rows off the panel. Each
tab's bar was a `fillMaxWidth` box in a Row, so the first tab took the
whole width. Fixed: the bar is drawn behind the tab. The guide is now laid
out in the canvas's own px (a 1260 x 972 panel scaled to fit the screen:
1:1 on the Odin2's 1920 x 1080), with its own text style (Material's 24 sp
line height clipped the larger text). New `GuideLayoutTest` (Robolectric,
1920 x 1080): the tabs sit in one line, every row fits above the hints, the
panel is the canvas's size; 4 pass, `GuideTextTest` 4. Debug APK SHA-256
`047438cc69fd66d5260752fba14e9a3550cdb19033d318e066cf2b8b247c87f1`, installed 04:16. On the Odin2 (Halo
started from the PC, keys sent by adb): Game, Save state (one state with
its picture), Controls/Rumble, Tweaks/Mods and Debug/Mark log all match
the canvas. Saving, loading, marking and changing rumble were not tried.

## Game testing fixes, 2026-11-16

Core changes from the first headless sweep (records in `docs/testing/`,
2026-11-16): a title's unhandled page fault stops the guest instead of
the process (`nc_kernel_unhandled_fault`, `NC_FATAL_STOPPED`, "The game
stopped" in `NativeCoreActivity`); RDMSR/WRMSR (`native/kernel/msr.c`,
`nc_cpu_guest_tsc`); `NtYieldExecution` yields to lower priorities
(`nc_ps_yield_any`); `xboxrun`: `vkdriver=<library path>` (it was ignored,
so headless runs used Qualcomm's driver), `NC_FOLLOW` for relaunches
(SIGPROF ignored before `execv`), and the loader logs a spec it can't
use. Native core rebuilt several times, no warnings; `test.bat`: 20 of 20
(new `test_msr`); `test.bat wireless`: 31 + 4 + 3 + 3 + 3 (new
`test_sweep`, `test_xbehash`). Stripped runners pushed to
`/data/local/tmp/native` on the Odin2 (`xboxrun-new2` last, MD5
`3d532fff3054e4ba613003ced683d916`); the sweep's `xboxrun` (MD5
`9dcfd184...`) kept for before/after runs. Debug APKs: `76e3e5d2...`
(10:06) was never installed; the settings session's `e3dd725e...` (with
these changes) went on at 10:21; then the stop dialog's text was made the
reason alone (`app_main.c`) and debug APK SHA-256
`97e52dc9f18b3a80037a72127402823709e2a52069637eb216f55bb4c7b9cbe4`
(with the settings session's `ex.c` and ConsoleTimeZone changes of that
moment) was installed on the Odin2 at 10:29:10. Checked there: Halo
started from the PC and crashed on purpose (`debug.hakux.memscan` poke)
showed "The game stopped" with the reason, and OK ended the game cleanly,
on both APKs. Not checked: anything else of the app.

## The guide's status line, 2026-11-16

From the owner's canvas edits (HakuXP Guide Menu, version 20): the guide's
header has, above the EC, the device's status as Android's status bar shows
it: network speed up and down (`TrafficStats`, the whole device, over the
second since the last read), Wi-Fi's bars (the active network's signal
level, a slash when not on Wi-Fi) and the battery (`BatteryManager`: the
percent, a bolt while charging, red at 15% or less when not). Read once a
second off the UI thread (`guide/StatusReader.kt`, `GuideHost.status`,
`GuideStatus`). The subtitle is `[4D530004][MS00409A] session 4:45`
(`GuideText.ids`), the EC `EC: 007FC19E`, and the Paused chip is gone.
`GuideLayoutTest` 5 (new: the status sits above the EC at the panel's
right edge, speeds left of the battery) and `GuideTextTest` 7 (new: ids,
speeds, Wi-Fi bars) pass, run with `--no-daemon`: the Gradle daemon started
at 09:21 had a system PATH entry with a stray quote (`C:\Program
Files\WSL"`), which it passes to JVM test workers as `java.library.path`,
and they failed with "Could not find or load main class Files\WSL;...".
The native core is the game-testing session's of 10:28 (APK `97e52dc9`).
Debug APK 33,900,523 bytes, SHA-256
`ab660f822932bcf9d06e7d557a875ae399ecc75464dccb16510dd14f6e177749`,
installed on the Odin2 at 10:44; the device's `base.apk` has the same
SHA-256. Checked there at 11:33, after the game-testing sweep ended
(Halo started from the PC, Back sent by adb; screenshot
`build/screenshots/2026-11-16_guide-status-game.png`): the header as the
canvas, `49 B/s` up and `39 B/s` down, full Wi-Fi bars, the battery full
with the bolt and `100%`, `[4D530004][MS00409A] session 0:47`,
`EC: 011FFCD6`. `dumpsys` agreed: battery level 100, status 2 (charging,
AC), Wi-Fi RSSI -34 dBm. Not seen: the low-battery red, a weak or absent
Wi-Fi signal, and speeds under real traffic.

## Playbooks and the automation banner, 2026-11-16

The owner's request: no more games run headless in the background on the
device; a red "Automated Testing In Progress" line at the bottom left
while the PC drives it; and recorded controller playbooks played back the
same way each run (docs/AUTOMATION.md, "Playbooks"). Core:
`native/dev/playbook.c` (pure: the format, playback, recording, picture
signatures), `native/dev/padplay.c` (the run's playbook between the pad
reads in `xid.c`, the render thread's frame hook and a writer thread),
`native/core/png.c` (the PNG writer moved out of `run/capture.c`),
`native/app/playbook_jni.c` (`playbook=`, `record=`, `playrun=`, the
`PlaybookNative` JNI, `debug.hakux.playbook` and `debug.hakux.automation`).
`xboxrun` exits 11 on Android; `xboxrun.sh`, `sweep.sh`, `census.sh` and
`frontier-wait.sh` say so and stop. App: `playbook/` (`PlaybookSession`,
`AutomationBanner`, `Playbooks`, `PlaybookHeader`, `BannerLines`), the
guide's Debug > Playbooks row, Mark log adding a checkpoint while
recording, the launch extras passed on by `LauncherActivity` and
`RelaunchActivity`, and `NativeCoreActivity`'s three restart intents made
one (`relaunchIntent`). PC: `tools/native/playbook.py`, `playbook.bat`.
`build.bat`: the native core and the APK built, no compiler warnings in
the new or changed C files; the seven `PlaybookNative` JNI symbols are
exported from `libhakuxcore.so` and the classes are in the APK. `test.bat`:
22 of 22 (new `test_playbook`); JVM: 107, all passed (new
`PlaybookHeaderTest` 5, `PlaybooksTest` 4, `BannerLinesTest` 6), run with
the trimmed PATH; `test.bat wireless`: 31 + 4 + 3 + 3 + 15 + 3 (new
`test_playbook.py` 15). Debug APK 33,947,079 bytes, SHA-256
`1d16d1267d63e628d8b88258002a100da7e16f68bcc163dc1612c3f14c157b93`, with
the other sessions' changes in the tree at 15:50 (the guide session's
header clock among them). Installed on the Odin2 at 15:53:57 by the guide
session, with the owner's OK; the device's `base.apk` has the same
SHA-256. Checked on the Odin2 at 20:02, the device asleep and locked:
`playbook.bat status` and `list` read it right; `push` made the
`playbooks` folder and sent a hand-written Halo playbook
(`build/playbooks/4D530004_handwritten-boot.txt`); `play` refused with
exit 3 ("asleep") and started nothing, the banner property left unset.
The folder adb makes belongs to the shell user, so `push` now opens it up
(`chmod 2777`) for the app's run folders. From 20:50, the owner having
unlocked it: the banner while playing and while the PC recorded, a
hand-written playbook played from Halo's start to its end, a recording
from the start (scripted presses kept at their exact frames, checkpoints
with signatures), and two replays of it matching all three checkpoints
(difference 0, menus only); record:
[testing/2026-11-16-playbooks-first-device-run.md](testing/2026-11-16-playbooks-first-device-run.md).
`playbook.py` now strips quotes from a URI (one that reached the app broke
the first recording). Not seen there: the guide's Playbooks menu, Mark
log's checkpoint, states, gameplay, a relaunch, `xboxrun`'s refusal.
`test.bat wireless`: the playbook tests are 16 now.

## Savestates in playbooks and state IDs, 2026-11-16

The owner's requests: a playbook that saves and loads states, and states
that say what they are (a date, an ID tied to the XMID). Core:
`playbook.c` reads `F save [LABEL]` and `F load ID` and splits a playbook
into parts at its loads (`nc_pb_parse`'s segment); `padplay.c` saves on
its writer thread when a save line's frame comes (never on the guest's),
ends a part at its load, carries a run's counts into the next part, names
a later part's checkpoints `part:frame`, and records the guide's saves and
loads; `padstate.c` (new) saves a state under a new ID with its `.info`;
`state/cache.c` `nc_cache_export` copies a state with the cache files
(`blobs/`) it uses; `playbook_jni.c`: `playseg=`, `playid=`,
`nativeNoteSave`, `nativeNoteLoad`, `nativeExportState`. App:
`data/StateInfo.kt` (IDs, `.info`), `slotN.info` for the guide's slots and
`findById`, `Playbooks.statesOf`/`stateFile`, `PlaybookSession` starting
the next part after a load, keeping a recording across the guide's Load
state (`NativeCoreActivity.restartInto(ownLoad)`), and the start state of
"Record from here" in the playbook's states. PC: `playbook.py` follows a
run across its parts, `push` sends the `states` folder, `states` lists IDs.
Native core built with no warnings; the ten `PlaybookNative` symbols are
exported. `test.bat`: 22 of 22 (`test_playbook` with parts, saves and
loads); JVM: 113, all passed (new `StateInfoTest` 5, a `PlaybooksTest`
case); `test.bat wireless`: 31 + 4 + 3 + 3 + 20 + 3. Debug APK 33,964,027
bytes, SHA-256
`3fab7cfb353de43b830e5fa523a557125931d8f2dd63a6489431cd5646079fd9`.
Installed on the Odin2 at 21:25:01 with the owner's OK and checked there
([record](testing/2026-11-16-playbook-savestates-device-run.md)): a
recording with the guide's Save and Load state (through
`debug.hakux.state`) kept as `save` and `load` lines, the loaded state
copied in with its cache files, part 2 recorded from it; three replays
saving their own state, loading at the load line and matching all three
checkpoints (difference 0, menus); "Record from here" into the playbook's
states and its replay. Found there: a playbook state's cache folder was
the app's alone, so `pull` missed it; `nc_padstate_share` (new) opens it
after each save. Rebuilt with no warnings; debug APK 33,964,087 bytes,
SHA-256 `9a5a97414589a43085b4a429a0820b7d17cdc30b89aef498d8d7ff59e11eba93`,
installed at 21:31:24 (the device's `base.apk` matches) and checked: a
run's state and a "from here" state pulled with their cache files. Not
checked: the guide's own menus for this, a load by a save slot's ID,
gameplay.

## The guide's clock, 2026-11-16

The guide's status line ends with the time, after a thin divider: bold,
in the device's 12/24-hour format (`GuideStatus.clock`, read with the rest
each second by `StatusReader`). The owner chose this place over a line of
its own under the status (both on the canvas, the Clock tweak). New in
`GuideLayoutTest`: the clock at the panel's right edge, on the battery's
line, above the EC; 5 and `GuideTextTest` 7 pass (`--no-daemon`). Shipped
in the automated-testing session's debug APK of 15:48 (it was built after
the clock's edits): SHA-256
`1d16d1267d63e628d8b88258002a100da7e16f68bcc163dc1612c3f14c157b93`, with
its 15:43 core; this session's `build.bat apk` at 15:53 gave the same
file. Installed on the Odin2 at 15:53:57 (the device's `base.apk` matched)
and checked at 15:54 with Halo started from the PC
(`build/screenshots/2026-11-16_guide-clock-game.png`, `-debug.png`): `3:54
PM` beside `77%` (on battery: no bolt), `22.6 KB/s` up, `4.7 KB/s` down,
full Wi-Fi; the game started as before, no automation banner, and the
Debug tab has the new Playbooks row. With the clock, Halo's title ("Halo -
Combat Evolved (USA) (Rev 2)", the image's file name) ends in "...".

## Playbooks in the guide's own panel, 2026-11-16

The owner's nitpick: the Playbooks row opened a framework pop-up where the
guide already has its own detail panels (Save state's). The guide gained a
general list panel, `Detail.ITEMS` (`guide/GuideModel.kt` `GuideItem`,
`GuideIcon`; `GuideHost.items`, `itemsSummary`, `activateItem`,
`deleteItem`, with defaults; `GuideScreen.kt` `ItemList`, drawn as the
state list: dashed action cards, entries with a picture or icon, title,
subtitle and dot; A, and X when deletable, with its hint), meant for the
coming mods and cheats menus too. The Playbooks row uses it
(`PlaybookSession.guideEntries`, `guideSummary`, `askDelete`;
`GameGuide.items`...): Record from here, Record from the game's start (or
Stop while one runs), then this game's playbooks with the start state's
picture, when recorded and the last run's result. The pop-up and its list
dialog are gone. Done in coordination with the guide session (told which
files, before editing). JVM: 114, all passed (new
`GuideLayoutTest.aRowsItemsListInTheDetail`); Kotlin compiled with no new
warnings. Debug APK 33,992,671 bytes, SHA-256
`c2596e31bb1385205a518d49ec4b158ffee7747173d44342c9da10c351edb21e`, with
the tree's other changes as of its build; installed on the Odin2 at
21:54:55 with the owner's OK and checked there
([record](testing/2026-11-16-guide-playbooks-panel.md)): the panel, moving
in it, the X hint and delete prompt, and playing from it. Then the counts
that read "1 checks" became plurals (`playbook.xml`; the banner now says
"N not as recorded"): `build.bat` with the guide session's latest changes,
no warnings; JVM 119, all passed; debug APK 33,993,571 bytes, SHA-256
`661f57bdfe47d719dd8ecfa7553b7ce4455cef0a681c775f906852570dce842f`, not
installed (the Odin2 then ran the guide session's `ac34bfd8...`, which has
the panel but not the plurals). The fix (`playbook.xml`, 01:11) is in the
guide session's later build from the whole tree, `3e01d0b4...`, installed
on the Odin2 at 08:21; its wording not looked at there yet.

## The clock's line, and the overlay's target and resolutions, 2026-11-17

The owner found the guide's top line too crowded: the clock now has its
own line under the status (speeds, Wi-Fi, battery), then the EC
(`GuideScreen.kt` Header; the canvas's Clock tweak now defaults to
below). The FPS overlay (and the guide's Debug stats) gained two lines
after the frame rate:

- `Target: 30 fps (60 Hz / 2)`: the title's pace, the median of its
  recent front intervals in whole vblanks and the mode's vblank rate
  (`hle_hitch_pace` in `hw/xbox/hle/hitch.c`, the stutter monitor's own
  cadence, what it holds frames to). There is no presentation interval to
  read (the title's D3D paces its flips), so this is what the screen
  showed: `--` until 8 frames are known, and a title that drops to 20 fps
  for a while reads 20.
- `Res: 640x480 x2 -> 1440x1080`: the title's resolution, the render
  scale (no `x1`), and the picture's size in the window as placed
  (`nc_present_sizes` in `native/app/present.c`: the front frame is the
  title's size times the scale, `back.c`'s display images).

JNI `nativeGetVideoInfo` (`app_main.c`), formatted by `FpsOverlayText.kt`;
the overlay's view holds 5 lines now. Native core rebuilt with the other
sessions' tree of 01:07 (26 objects), no warnings. JVM, run with
`--no-daemon`: `GuideLayoutTest` 7 (new: the status, the clock, then the
EC, right-aligned; Mark log's form under seven lines of stats, above the
hints), `GuideTextTest` 7, `FpsOverlayTextTest` 4 (new). Debug APK
33,993,407 bytes, SHA-256
`ac34bfd8b93e9a5c965f7e281ae4e32a61f46125f5352381c91866d5ca73ba38`,
installed on the Odin2 at 01:09:09 (the device's `base.apk` matched).
Checked there at 08:04, after the owner woke it (Halo started from the
PC, keys by adb; `build/screenshots/2026-11-17_overlay-ingame.png`,
`_guide-clock-below.png`, `_guide-debug-stats.png`, `_guide-marklog.png`):
in Halo's main menu the overlay reads `FPS: 30`, `Target: 30 fps (60 Hz /
2)`, `Res: 640x480 -> 1440x1080` (the picture between the black bars is
1440 wide); the guide's header shows the status line, `8:04 AM` under it,
then `EC: 011DBF70`, and Halo's full title fits again; the Debug stats
show the same lines, and Mark log's form fits under its seven lines of
stats with room above the hints. Not seen: a render scale above 1x (the
`x2` form), a PAL title's 50 Hz, a title whose pace changes.

## The overlay's delta, 2026-11-17

The owner's follow-up. `Target: 30 fps (60 Hz)` (the vblank divisor is
gone); the running game's `XMID | title ID` moved to the overlay's top,
above the frame rate; and a new line, `Delta: 2.25` (`Delta: 1.13 |
overall 2.25` at a render scale above 1x):

- delta: how far the rendered frame (the title's resolution times the
  render scale) is stretched to its picture on the screen. 1.00 is pixel
  for pixel (1440x1080 at 1x in a 1440x1080 picture); under 1 the frame is
  drawn larger than shown.
- overall: from the title's own resolution to the picture, what the
  screen asks of the emulator (640x480 is 2.25 on a 1080p panel, 4.50 on
  4K), shown when it differs from the delta.
- An uneven stretch (a widescreen title's anamorphic 640x480 at 16:9)
  counts both axes: the geometric mean (2.60 on a 16:9 1080p screen, 1.39
  letterboxed on a 4:3 1024x768 one).
- Measured as close to the panel as the app can see: the picture's size in
  the window times the panel's physical pixels per app pixel (the display
  mode over the app's maximum window; 1 unless Android draws the app
  smaller than its screen, as a game mode's downscaling does).

`nativeGetVideoInfo` also gives the window's size; `FpsOverlayText.delta`,
`screenRatio`; `NativeCoreActivity.videoLines` serves the overlay and the
guide's Debug stats. JVM (`--no-daemon`): `FpsOverlayTextTest` 7,
`GuideLayoutTest` 7 (Mark log's form under eight lines of stats),
`GuideTextTest` 7. Native core rebuilt, no warnings. Debug APK 33,995,895
bytes, SHA-256
`3e01d0b42ec5ab2a2e1825d521ce442d05331cafb0b41e1ad744c9ae6c7b2a7c`,
installed on the Odin2 at 08:21 (matched). Checked there at 08:22 with
Halo (`build/screenshots/2026-11-17_overlay-delta-ingame.png`,
`_guide-delta-marklog.png`): the IDs on top, `Target: 30 fps (60 Hz)`,
`Res: 640x480 -> 1440x1080`, `Delta: 2.25`; Mark log's form fits under
the stats. Not seen: a render scale above 1x, a widescreen title, a
screen ratio above 1.

## The stutter monitor's last timeout, 2026-11-17

The owner: the overlay's `last hang #1: 0.50 s, 12m30s ago` counts up for
something long over. Settings > Debug > Last timeout (debug builds, under
Show stutter monitor; 1, 5, 10 or 30 min or Never, default 5 min,
`hitch_timeout_s`): a last hang or stutter older than that shows `--` for
its age (`last hang #1: 0.50 s, --`); the log keeps every time as before.
The age text is its own pure module, `hw/xbox/hle/util/age.c`
(`hle_age_text`), used by `hitch.c`'s overlay; `hle_hitch_set_age_limit`,
JNI `nativeSetHitchTimeout`, set from the preference at each
`onResume`. Not in the guide (the owner: Settings only).

`test.bat`: 23 of 23 (new `test_age`: ages, the limit at 5 min, 12m30s
as `--`, Never). JVM (`--no-daemon`): 123, all passed (new
`SettingsLayoutTest.debugLastTimeoutPicksFromItsChoices`: Debug > Last
timeout reads 5 min, picks Never, kept). Native core rebuilt, no
warnings. Debug APK `ef43aecf...10e3d7` (the age formatting still inline
in `hitch.c`) was installed on the Odin2 at 08:41; Halo's overlay showed
ages counting (`last hang #2: 20.35 s, 31s ago`, `last stutter #23: 49
ms, 7s ago`), but the 5-minute look was overtaken: by 08:47 another game
was running there (`IG05702W | 49470039`, 60 fps, `Target: 60 fps (60
Hz)`). The build with `age.c`, SHA-256
`3da0f3df6504382e4c187a0ccd4c6241f27e5b197638624f86ef81bce8de9d5a`, is
not installed yet. Not seen on a device: `--` after the timeout. Noticed:
that Halo boot had a 20.35 s hang (#2), where earlier boots had one of
about 0.5 s; not looked into.

## The delta on the Res line, and the timeout seen, 2026-11-17

The owner: the delta is short enough for the end of the Res line. The
overlay (and the guide's Debug stats) now read `Res: 640x480 > 1440x1080 -
2.25`; at a render scale above 1x, `Res: 640x480 x2 > 1440x1080 - 1.13
(2.25)`, the overall figure in brackets. The separate Delta line is gone
(`FpsOverlayText.resolution`; the overlay holds 5 lines). The owner keeps
the render scale at 1x by default: the x2 form is there for when it is
raised, not looked into on this fork yet. JVM: `FpsOverlayTextTest` 6,
`GuideLayoutTest` 7, `GuideTextTest` 7. Kotlin only (the core as the
`age.c` build). Debug APK 34,003,723 bytes, SHA-256
`ef33904e1bd84aebe35667ece3650bf7a90e7509c32983993147435727b905cd`,
installed on the Odin2 at 08:53 (matched). Halo left on its main menu
from 08:53:53 (`build/screenshots/2026-11-17_res-delta-early.png`,
`_res-delta-5m.png`): at 08:54:44 `last hang #1: 0.54 s, 49s ago`; at
08:59:44 `last hang #1: 0.54 s, --` while `last stutter #311: 49 ms, 0s
ago` still counted, and `Res: 640x480 > 1440x1080 - 2.25`. The Last
timeout's `--` is so seen on the device at its default 5 minutes. Seen
too: left idle, Halo's attract mode took the stutters from 29 to 311 in
those five minutes (not looked into).

## The vblank and a synthetic vsync, 2026-11-17

Halo's attract mode stuttered 1.5 times a second (48.8 ms frames). The
Odin2's SurfaceFlinger was sending synthetic vsyncs with the screen on (a
16 ms timer, about 16.3 ms apart), and the NV2A's vblank, locked to the
panel within 5%, ran at 61.5 Hz. New `native/dev/vsynctrack.c`: the
panel's period and how far its times fall from that period's grid; the
vblank locks only while they are regular (under 50 us, again once back
under after passing 100 us) and a whole number of periods is within 0.5%
of a frame (`nv2a.c`, logged when it changes; the presenter line shows
the jitter). Host tests: `test_vsynctrack` new, 24 of 24 pass. Debug APK
34,003,723 bytes, SHA-256
`e74808dabd1dd80fbf82fc5b4eccf58958fcba73c9fecb87df9520c85ca4b86f` (the
shared tree at 09:13), installed on the Odin2 at 09:19 with the owner's
OK (matched). The device slept and woke before the run, and SurfaceFlinger
was back on the panel's vsync, so the run showed the lock to a real panel
(`follows the panel: 1 vsync of 16.665 ms`, 0.002 ms off its grid), not
the refusal of a synthetic one. Halo from boot, idle into its demos
(playbook `4D530004_attract-idle`): the menu at 30.0 fps (was 30.75), the
demos' late frames 0.3-0.4 a second, in bursts every ~15 s from Halo's
29.97 fps against the 60.006 Hz panel (TODO.md). Record:
[testing/2026-11-17-halo-attract-stutter.md](testing/2026-11-17-halo-attract-stutter.md).
Then a log-only change (the first decision at boot no longer blames a
synthetic vsync before measuring): APK
`983fd9d863e880e2f410e7fd9bc9d95bef801519362d6f41d288dca1cea34e1c`, built,
not installed.

## The playbook banner's ETA, 2026-11-17

The owner: the banner's progress line should say when the run ends. It
now ends in `ETA 6m20s`: the frames left at the pace the playbook's frame
counter moved over the last 5 s (`playbook/PlaybookEta.kt`, from
`PlaybookSession`'s 500 ms poll), so 1200 frames at 30 fps read 40s; `--`
under a second of samples or while the frames stand still (the guide
open, a load), and it starts over at a new part. JVM:
`PlaybookEtaTest` 7, and `BannerLinesTest`, `PlaybookHeaderTest`,
`PlaybooksTest` 16, pass. Debug APK 34,006,767 bytes, SHA-256
`945f8a6d78316b702bc89cb2ef389aea9166a47de5327228f254c4d63b182a3f` (with the
vblank's log-only change above), installed on the Odin2 at 09:36 with the
owner's OK (matched). Playbook `4D530004_handwritten-boot` (2400 frames):
`frame 276 of 2400, 0 checks, 0 not as recorded, ETA 1m12s`, then at
09:36:51.8 `frame 898 of 2400, 1 check, 0 not as recorded, ETA 51s`; it
ended at 09:37:41.95, a second before that ETA
(`build/screenshots/2026-11-17_banner-eta-1.png`, `-2.png`,
`-lines.png`). The vblank's first line at boot is now `follows the panel`
alone. Not seen: `--` while paused (the guide wasn't opened), a
multi-part playbook, a game below 30 fps.

## Playbooks that repeat: input sync and the game's clocks, 2026-11-17

`native/dev/inputsync.c` (a playbook's frame read on the game's own thread
at each vblank it takes, the USB controller's frames run there; hooks in
`native/kernel/intr.c`, `native/dev/ohci.c`, `hw/xbox/hle/pb/pbmode.c`,
`native/dev/padplay.c`, which now writes each run's `taken.txt`);
`native/kernel/detclock.c` (RDTSC hooked through core.c's hook query and
answered from a vblank-locked counter, the kernel's time reads the same;
`nc_cpu_tsc_of_cycles`, `nc_kern_state_loaded_clock`, `nc_ke_det_epoch`;
`NcThread` gains skipped members, state sizes updated with
`statelock.py --sizes`, 17 sections match their lock);
`native/kernel/calltrace.c` (`NC_TRACE_EIP`); `tools/native/hxs.py --ram`
and `--ramdiff`. No change to FEX (a patch was started and reverted: FEX's
own CLAUDE.md asks for no AI-written code). Native core rebuilt, no
warnings; `test.bat`: 26 of 26 (new `test_inputsync`, `test_detclock`);
`test.bat wireless`: 31 + 4 + 3 + 3 + 20 + 3. Debug APKs installed on the
Odin2 (the owner's go-ahead): `8acd6732...` at 13:25 (input sync),
`4665227c...` at 13:37 (and the clock), `20d0036dc775d912...` at 14:07
(and the tracer). Checked there with Soulcalibur II (the record,
`docs/testing/2026-11-17-playbook-determinism.md`): the input lands the
same in two runs; the clock starts from the same values and answers the
title's RDTSC; the fight still drifts after 5.9 s. JVM tests not rerun
(no Kotlin change).

## Playbooks that repeat: the vblank at the game's point, the GPU in lockstep, 2026-11-17 evening

`native/kernel/detevents.c` (the playbook's vblank given when the guest
waits or spins on the GPU, the 100 ms fallback in the guest's time; hooks
in `ps.c`, `intr.c`, `kernel.c`; the gate in `native/dev/nv2a.c`);
`hw/xbox/hle/pb/pbmode.c` `hle_pb_lockstep` / `hle_pb_settle` (the
guest's PUT, FIFO, PGRAPH interrupt and flip writes wait for the decoder;
called in `nv2a.c`); `native/state/save.c` (under the lockstep a save
pauses the guest first and parks the decoder from its pause point);
`native/kernel/memtrace.c` and `tools/native/memtrace.py`
(`NC_TRACE_MEM`). The first event build ran Soulcalibur II's playbook at
8 fps (every vblank forced); fixed here. Native core rebuilt with
`build.bat all`, no warnings; `test.bat`: 26 of 26 each build; no new
host test (these need the device's GPU and timing). One APK
(`f9630d95...`) was built with `build.bat apk`, which packages the last
core built, and ran the old core: rebuild with `all` after native changes.
Debug APKs installed on the Odin2 (the owner's go-ahead to install and
test): `6fffd18d...`, `316a6d7d...`, `7b5847ba...`, `0db37ade...` and,
last, `d2b1080e1b07ca90c27cb59e8df9a9367fca1247a725a6f9efe4f5850bfaef8a`
(18:43). Checked there with Soulcalibur II's no-input playbook (the
record): 56-60 fps (45-54 while eight states are saved), no forced
vblanks, no lockstep timeouts, saves 254-282 ms; two runs' random draws
the same for 1070 vblanks (17.8 s) and all five pictures the same. Other
games' playbooks not run with it. JVM tests not rerun (no Kotlin change).

## Playbooks that repeat: the sound and the controller on the game's vblanks, 2026-11-17 night

`native/dev/apu/lockstep.c` (the APU's frames on the guest's thread at
each vblank given and while DirectSound waits on the GP; XGSCNT from the
frames run; hooks in `native/dev/apu/apu.c`, `dsp/gp_ep.c`, `vp/vp.c`;
the target in `native/cmake/hle.cmake`; started from `detevents.c`, held
from before the state's load in `core/core.c`; `nc_ke_told_ns` declared
in `kernel.h`); `native/dev/ohci.c` (no frames of its own thread in a
playbook; HcFmRemaining a whole frame); `native/kernel/memtrace.c`
(`NC_TRACE_MEMDUMP`). The first APU build (`2ff36075...`) hung Soulcalibur
II at its first frame (an interrupt raised at each FECTL write; fixed in
the next). Native core rebuilt with `build.bat all`, no compiler
warnings; `test.bat`: 26 of 26 each build; no new host test. Debug APKs
installed on the Odin2 (the owner: "Yes, do the sound"): `2ff36075...`,
`a005ebf3...`, `81a1eb07...`, `b201eca2...` and, last,
`324de83835c68a8f4b52e3097bf978d6c353cd49f0ac8eb78a90ca4610d30096`
(before 19:13). Checked there with Soulcalibur II (the record): two no-input
runs the same at all 7 checkpoints (0.0) and in the traced memory at every
vblank of the playback but one; two replays of the owner's recording the
same as each other at all 5 checkpoints; 56-60 fps (60 throughout the
recording; 44-54 while eight states are saved). Other games not run with
it. JVM tests not rerun (no Kotlin change).

## The Debugging switch, HakuXP's package and release builds, 2026-11-18

`android/.../debug/DebugMode.kt` (the switch, a one-line file all three
processes read), `XbdmRuntime.kt` (binds the debug service only while it is
on), `XbdmService.kt` (reads it each second; off: no listener, no screen
lock), `XbdmIpc.kt` (DEBUGGING); `ui/settings/DebugSettings.kt`,
`XbdmSettings.kt`, `WirelessSettings.kt`, `ui/common/Components.kt`
(`SwitchItem`'s `enabled`, `Modifier.dimmed`), `GameSettingsScreen.kt`;
`guide/GuideHost.kt` and `GuideScreen.kt` (the host's tabs), `GameGuide.kt`;
`NativeCoreActivity.kt`, `data/GameSettings.kt` and
`playbook/PlaybookSession.kt` (the switch where `BuildConfig.DEBUG` was);
`data/Updates.kt` and `GeneralSettings.kt` (the build's update source);
`res/values/debugging.xml`; `android/app/build.gradle.kts` (id
`zip.mobcat.hakuxp`, 0.1.0 (1), `RELEASE_APP_ID`, `UPDATE_REPO`, the key's
path); `build.bat` (`release`, `release-apk`, `keystore`),
`scripts/keystore.ps1`; `tools/native/playbook.py`, `xbdm-log.py`,
`fexrun-suite.sh` and their tests.

Checked on the PC only:

- `:app:compileDebugKotlin`; `:app:testDebugUnitTest --rerun`: 146 of 146
  in 30 classes, among them the new `DebugModeTest` (3), `RunSettingsDebugTest`
  (1), `SettingsLayoutTest.debuggingOffGraysOutTheDebugTab`,
  `XbdmSettingsTest.debuggingOffLocksEveryRow` and
  `GuideLayoutTest.noDebugTabWhileDebuggingIsOff`.
- `tests/native/wireless/test_playbook.py` 20 of 20;
  `tests/native/xbdm/test_launcher.py` with `xbdm_host.exe` and xbdlog
  (127.0.0.1 only): all passed.
- `scripts/keystore.ps1` with a throwaway key in the scratch folder
  (password from `HAKUXP_KEYSTORE_PASSWORD`, with a backslash, spaces and
  `=` in it): made the key store and `key.properties`; refused to replace
  `key.properties`; refused a wrong password for an existing key store and
  wrote nothing. The interactive prompts were not run.
- `build.bat release-apk` without a key: stopped with the message. With
  `HX_KEY_PROPERTIES` at the throwaway key: built, `apksigner` verified
  (CN=HakuXP Test), copied as `build\release\HakuXP-0.1.0.apk`; `aapt2`:
  `zip.mobcat.hakuxp` 0.1.0 (1), label HakuXP, not debuggable. That APK and
  the key were deleted afterwards. `build.bat release` (with the core build)
  not run.
- `build.bat apk`: `zip.mobcat.hakuxp.debug` 0.1.0 (1), debuggable, SHA-256
  `8c454bccf098b63dfe2398055c81810cd744938e0fce894da307adf8b8150e61`.

On the Odin2 (the owner: "Yes, install it"), the debug APK above, then a
rebuild with the fix below (SHA-256
`dd973a211f7766d9fc0ab94dfe4a7724bc5b03edf362cf934c1feea61bb49302`; it also
has another session's string and update-source changes). It installed next
to the old `com.rfandango.haku_x.debug`, which keeps its data; the owner
chose the games folder and added Turnip T30 in the new app. Smashing Drive
(USA), `4E4D0001`/`NM00102A`, copied into the app's own folder and started
by a `file://` URI with the banner (copy deleted afterwards):

- Debugging on (the default): the Debug tab's switch on, the `:debug`
  service running; in game the FPS overlay, hitch monitor, EC stamp and
  banner, 60 fps; the guide with Game, Controls, Tweaks and Debug.
- Switched off in Settings: the tab grayed with its hint, a tap on the
  grayed FPS row changed nothing, `files/debug_mode` read `off`, the service
  logged "Debugging switched off" and `dumpsys activity services` listed
  none (the emptied `:debug` process stayed cached, as Android does).
- A game with it off (PurpleVK, driver id 18): no overlay, stamp or hitch
  monitor; the banner read "debug-off-check: Debugging is off (Settings,
  HakuXP, Debug)." for a playbook the launch named; no debug service with
  the game running; the guide with three tabs, LB from Game going to
  Tweaks; `playbook.bat mark` dropped (logged).
- Found: `playbook.bat end` couldn't close the game with it off (the app
  dropped `exit` too) and still printed "Game closed". Fixed: `stop` and
  `exit` are taken with it off, and `end` says when the game is still up
  (`test_playbook.py` 22 of 22, two new). After the rebuild, `end` closed
  the game with it off (exit 0) while `mark` was still dropped.
- After an app restart with it off, no service ran and Connection status
  read "Off: debugging is off." with the saved port. Switched on again: the
  service ran in a new process; left on.

JVM after the fix: 147 of 147. Not checked: the release APK on a device
(no real release key yet), Wireless debugging's keep-awake, XBDM with a PC
client, and Export logs while it is on.

Then the debug build's own name (the owner: "Yes, rename it"): the
application label is `@string/launcher_name`, "HakuXP Debug" in debug
builds and "HakuXP" in release; `app_name` (the Settings tab's title) stays
HakuXP. `aapt2`: `application-label:'HakuXP Debug'`; the release's
generated resValue is "HakuXP" (`:app:processReleaseResources`, no APK
built). JVM 147 of 147. Installed on the Odin2 (SHA-256
`3d648aed25d72efb79982ec3b0484d5777663e5026b91665302eb9f7e9014c6e`);
Debugging still on after the update. The launcher icon's text itself was
not looked at.

## Library views: Icons and Covers, 2026-11-18

A view button beside Refresh (`ui/library/LibraryView.kt`, kept in
`AppPrefs.libraryView`) and the Covers view (`CoverCard.kt`): front covers
from MobCat's original Xbox game list, by the disc's XMID.
`data/CoverIndex.kt` picks the file from the list's thumbnail index (pure,
host-tested); `data/CoverStore.kt` fetches the index (GitHub's tree API,
kept a week, ETag) and the covers (`cacheDir/covers/<XMID>.jpg`), decodes
them as RGB 565 and keeps the recent ones in memory.

Checked on the PC:

- `:app:testDebugUnitTest --tests CoverIndexTest --tests LibraryViewTest
  --rerun`: 9 of 9 and 2 of 2, `DiscInfoXmidTest` 2 of 2. The cases include
  the list's real drift: Fuzion Frenzy's XBE `MS00201A` finds the disc's
  `MS00202A`, Xbox Music Mixer's `MS09004A` finds `MS09005A` (same
  `default.xbe` MD5 in the owner's API), and the Forza CDX menu's
  `XL3278870W` finds nothing (the disc is `XL02070W`).
- The list's thumbnail index by the same call the app makes: 2417 files,
  610 KB of JSON (98 KB gzipped), not truncated; `thumbnail/MS/MS00409A.jpg`
  and others 300 x 420.
- `build.bat apk`: SHA-256
  `dd973a211f7766d9fc0ab94dfe4a7724bc5b03edf362cf934c1feea61bb49302`.

On the Odin2 (17:07-17:15, the owner's OK; library screen only, no game
run), the working tree's APK installed at 17:04 (SHA-256 `3d648aed...`),
then this session's fix builds:

- The button beside Refresh showed the list icon; one press: the grid icon,
  `library_view` = `covers` in the prefs, the log's `hakuX-covers: cover
  index: 1802 releases` (one GitHub call; `cache/covers/index.txt`, 2417
  files, with its ETag) and the covers of the first rows within seconds.
  Six columns in landscape. Scrolled to the end: 38 covers cached, 1.4 MB.
  Xbox Music Mixer (`MS09004A`) and Fuzion Frenzy (`MS00201A`) show their
  discs' covers (`MS09005A`, `MS00202A`); CDX (the Forza disc's menu,
  `XL3278870W`) and Doom 3 (no disc info) their initial.
- D-pad: the focused card outlined in green, the grid following it; from
  the button row Down reached the first card when the grid was at its top
  (from the end of the grid it scrolled instead).
- Another press: the Icons view, as before, near the same games; the view
  kept. Reopened after an install: the index read from the cache (no
  download), covers from disk.
- Found and fixed: a card whose three flags wrapped onto a second line was
  taller than its row's others (ragged edges). Cover cards now give the
  flags a line of their own (it never wraps), then the title ID and the
  XMID; a disc without regions keeps an invisible flag's line. Seen even on
  the device with APK
  `22dfb63633ac970f7c6f8c3667bf5b451b3d6959bb03b5b447fe019816658675`
  (installed 17:14, left in the Covers view).

Not checked: offline (no index and no network), a week-old index's ETag
refresh, portrait or another screen size, TalkBack, Y and long press in
the grid, a library of hundreds of games.

## Xbox controller types and compiled mappings, 2026-11-18

Each player's emulated controller has a type (`native/dev/xidtype.c`):
Duke `045E:0202`, Controller S `045E:0289`, Steel Battalion `0A7B:D000`,
racing wheel `3767:0101`, each with its real device and configuration
descriptors, XID descriptor, capabilities and report (`xid.c`). The app
passes `padtypes=` (`data/XboxPad.kt`), swaps a type as the game runs
(`nativeSetPadType`, a re-plug on the controller hub's port 1), and sends
a Steel Battalion controller's state with `nativeSetSbc`. Mappings are
compiled once per assignment (`input/CompiledMapping.kt`,
`CompiledPad.kt`). Savestates: USB section v2 (`NcUsbPads`).

Checked on the PC only (no device run):

- `test.bat test_`: 29 of 29, with the new `test_xidtype` (each type's
  bytes against pontifex-hidimus's Duke and S captures and xboxdevwiki's
  Steel Battalion and wheel values, `padtypes=` parsing, report packing)
  and `test_usb` (`tests/native/usb/usb_test.c`, which no longer built:
  stubs added, now in the suite with `NC_USB_HOST_TEST`). It enumerates
  each type through OHCI as the console does (pontifex's seven requests),
  reads its report on its own IN endpoint (the other stalls), sends its
  output on its OUT endpoint (rumble; the Steel Battalion's 22-byte lamps),
  checks no string descriptors, and swaps a Duke for a Steel Battalion
  controller mid-run (hub change bit, old address gone, new one at 0).
- `test.bat state`: 17 sections match (USB bumped to v2, v1 loads).
- `:app:testDebugUnitTest` (trimmed PATH): `CompiledPadTest` 9 of 9
  (gamepad keys and axes, unchanged events send nothing, Steel Battalion
  rest state, gear and tuner steps, toggles, defaults from the gamepad
  mapping, JSON round trip, `forRun`, the input names array), with
  `SettingsLayoutTest` 10, `GuideLayoutTest` 8 and the `data` tests.
- `build.bat core` (NDK 29), then `build.bat apk`: SHA-256
  `0dbf5ed7adb8f5d0a396ddfeece76f9ab187c76835016e6eb2e3daab8d9de87b`.

Installed on the Odin2 on 2026-11-19 at 09:09 (the owner's OK),
`build.bat install` over Wi-Fi in 4 s: the device's `base.apk` has the
SHA-256 above. It was the first install of `zip.mobcat.hakuxp.debug`
there (the device had only the old `com.rfandango.haku_x` packages, left
as they were), so the app starts empty: games folder, Turnip and hard disk
to set up or move. Steel Battalion (USA) is on the device's SD card
(`ISOs and ROMs/Xbox/Steel Battalion (USA).iso`). No game run yet.

Not checked: any game on the device, Steel Battalion booting with it,
the in-game swap and a state saved with one type and loaded with another
on the device, input timing on the device.

## debug.bat and a device announced twice, 2026-11-19

`debug.bat` refused the Odin2 ("not connected and authorized") while
`wireless.bat` showed it connected as
`adb-c3e8b774-IKUygG (2)._adb-tls-connect._tcp`: `tools/native/xbdm-log.py`
split `adb devices` lines on spaces. It now uses `adb_wireless.py`'s parse
(`ready_serials`), and with no device named takes `target()` as the other
scripts do. In `adb_wireless.py`: devices grouped by hardware serial
(`device_groups`), `connect` and `use` without arguments take the one
device (two announcements, or USB and Wi-Fi, are one) and show a numbered
list only for several devices (`choose`, when run at a console), and
`status` marks the connection the scripts use and the live announcement.

- `test.bat wireless`: `test_adb_wireless.py` 42 of 42 (new: the Odin2's
  two announcements already connected, the latest tried first, two devices
  listed or picked, USB and Wi-Fi as one device for `use` and `target`,
  `ready_serials` with a space), and the other five tool suites (35).
- `tests/native/xbdm/test_launcher.py` (the PC part): its eight rollback
  cases and a new one with the Odin2's serial, its space included, reaching
  adb whole.
- On the Odin2 at 09:20: `wireless.bat` marked the `(2)` connection as the
  scripts' and its announcement (`:35213`) as connected, the older one
  (`:39307`) connectable; `wireless.bat connect` answered "already
  connected"; `debug.bat --duration 5` captured the running game's log
  (165 records; the XBDM queue reported 159 lost before delivery) and
  restored the device's properties (exit 0).

The game it caught was the owner's Steel Battalion (USA) run in the
controller-types build (`zip.mobcat.hakuxp.debug`, pid 11790). From the
device's logcat, not driven from the PC: the core logged
`player 1: Steel Battalion Controller 0A7B:D000` and the XBE `title
43430002 xmid CC00201A`; the game read the controller's reports (button
changes: right lever fire, Start, magazine change, sub weapon, gear 3 to 4,
the five toggle switches on) and sent 22-byte lamp reports (the Start
lamp's brightness stepping down, the neutral gear lamp lit before the
first shift). Whether each input does the right thing in the game is the
owner's to say.

The owner's capture of the next run (`build/logs/xbdm/2026-11-19_09-23-46.112226`,
`summary.bat`: Steel Battalion (USA) for 88.7 s, closed from the app,
44.8 fps on average, 48 stutters; "reached: S2" is the last boot stage the
core logs, not a stop). From its Android log:

- `player 1: Steel Battalion Controller 0A7B:D000`, polled from 0.9 s in,
  the toggle switches on and neutral at the first read, no unhandled
  request.
- The gear lamps follow the gear lever: the game lit N (byte 18's low
  nibble) at the start, then 1, 2, 3 and 2 again as the owner shifted
  (bytes 18-19), the lamp moving within ~50 ms of the shift. So the game
  reads a real unit's encoding (N -1, 1-5). Gears 1-5 reached in all;
  no reverse.
- Buttons that reached the game: right lever fire (0x02), lock-on (0x04)
  and main weapon (0x01), magazine change and sub weapon (their lamps,
  bytes 14 and 13, lit as pressed), cockpit hatch, ignition, Start, and
  eject twice (09:24:58 and 09:25:14).
- Input timing, 5 s windows during play: read by the title in 2.2-5.3 ms
  on average (max 15.3), on screen in 22.7-47.4 ms (max 67.4).
- The five unimplemented kernel imports (`IoStartPacket` and others) are
  never called.
- The lamp log reached its 100-line cap 17 s in; later lamps aren't logged.

## Controller pictures and per-game buttons, 2026-11-19

The emulated controllers drawn (`ui/pad/`): `PadLayout.kt` (each input's
part, tap hits; pure), `PadPainter.kt` (the Duke and the S from the
on-screen controls' `ControlPainter`, the wheel, the Steel Battalion's
cockpit), `SbcLamps.kt` (the lamp report, xboxdevwiki's nibble order),
`PadPicture.kt` (Compose). The buttons screen shows the picture live; the
guide's Controls > Buttons (`GuideButtons.kt`, `Detail.BUTTONS`, the
guide window's events through `GuideHost.padEvent`) maps for the running
game alone (`Controllers.Config.games`, compiled by `PadRouter` with the
game's key). The press-to-bind rule and input names are shared
(`input/BindingCapture.kt`, `ui/pad/InputNames.kt`). New JNI:
`nativeGetPadOutput` (the last output report, for the lamps).

Checked on the PC only:

- `:app:testDebugUnitTest` (trimmed PATH): 169 of 169 in 36 suites. New:
  `PadLayoutTest` (4: every input's part inside the picture, taps, the
  Steel Battalion panel without overlaps, the lamps of the Odin2's 09:24
  capture: Start 13, magazine change 15, gear 3), `PadPictureRenderTest`
  (3, Robolectric native graphics: a pressed A, a moved stick, a pulled
  trigger, start, the focus ring change the Duke's picture there and not
  at B or the right stick; the wheel's rim and gas; the Steel Battalion's
  Start and magazine lamps, a switch off, the aiming lever, gear, pedal),
  `ControllersGameTest` (3: a game's mapping there only, its key, stored
  and read back), `GuideButtonsDetailTest` (the row, its inputs, A's
  capture and prompt; it caught the list squeezed under the picture, now
  without the row's text in the list and a smaller gamepad picture) and
  `GuideButtonsPictureTest` (the guide drawn with a Duke and a Steel
  Battalion: `build/pad-previews/guide_buttons_*.png`).
- `build.bat` (core and APK): SHA-256
  `57327366ebcf4f1301cd2329597a41140c69b4a9928aab704cdfab157df8e381`.
  Installed on the Odin2 at 10:13 (the owner's OK; `build.bat install`
  over Wi-Fi, 4 s, the library open and no game running): the device's
  `base.apk` has that SHA-256.

Not checked: anything on the device (a real controller moving the
picture, a capture in the guide, the mapping reaching the game).

## The welcome screen in steps, 2026-11-19

The first-run setup (`ui/setup/`) is four steps in one frame
(`SetupScreen.kt`: the steps above, Back and Next below, the step scrolled
between): `SetupGames.kt` (folder, full path, games found),
`SetupControls.kt` (player 1, the Xbox controller type, connected
controllers' Buttons through the mapping route), `SetupDriver.kt` (the GPU
driver screen's model; Turnip on Adreno) and `SetupConsole.kt` with
`SetupConsoleViewModel.kt` (the EEPROM read only, dashboard settings, your
own Xbox: `DiskImport.run` without a re-signer, then `ConsoleEeprom.adopt`).
New helpers: `Storage.folderPath`/`treePath`, `ConsoleEeprom.check`/`adopt`
(drops a pending `eeprom.renew`), `LibraryView.afterSetup`,
`GpuDriverHelper.systemVulkan13`. `LauncherActivity` shows the setup until
`setup_complete`.

Checked on the PC only:

- `:app:compileDebugKotlin`: no errors or warnings in the changed files.
- `:app:testDebugUnitTest --rerun` (trimmed PATH): 176 of 176 in 37
  suites. New: `StoragePathTest` (4: internal storage, an SD card, a
  volume's root and the Documents shortcut, an unknown volume),
  `ConsoleEepromTest` 9 of 9 (adopted whole, the HD key the resigner then
  reads, the renew marker dropped; a short file, zeros and a broken section
  refused, the console's EEPROM untouched; one under the last known key
  taken), `LibraryViewTest` 3 of 3 (Covers after the setup unless chosen).
- `build.bat apk`: SHA-256
  `5737c2e52078656c12533fcd29025aecf21e811169ebac17dbbbb91924e76f00`; its
  `classes.dex` has the setup's classes and `resources.arsc` its strings.

Installed on the Odin2 at 10:44 (the owner's OK; `build.bat install` over
Wi-Fi, 4 s, the library open and no game running): the device's
`base.apk` has that SHA-256. Its `x1box_prefs` already had
`setup_complete` true and `library_view` `covers` (the old setup finished
that morning), so the app opens on the library and the new setup isn't
shown until `setup_complete` is false. At the owner's word it was set
false at 10:46 (app force-stopped, `run-as ... sed` on `x1box_prefs.xml`,
the rest kept) and the app opened: the Games step, the four steps in the
header row, the folder "Xbox" and its full path
`/storage/6262-3239/ISOs and ROMs/Xbox`. In landscape the fixed header and
footer leave the step about 600 of 1080 px: the games count and the folder
button are below the fold until scrolled.

Not checked: the other steps on the device, controller focus,
the folder picker's return, the mapping screen's return and a ZIP added
from the step are unseen. A real disk image brought in with its EEPROM,
end to end. Portrait, larger fonts, TalkBack.

### Setup footer and Finish, 2026-11-19

The Odin2 crash log recorded two release crashes in the setup wizard.
Retracing with the matching R8 mapping placed both at the lookup of step
index 4 in a four-step list. A Compose regression test reproduced the
earlier symptom after a single Next press: the Controls page opened while
the bottom bar still showed Games as current.

The page and footer now change together. Back and Next target a specific
page, and Finish records completion once. `SetupNavigationTest` covers
the active indicator, Back, a direct jump, two Next activations before
the next frame, and repeated Finish. All 177 Android JVM tests passed.
A separate Robolectric run opened the actual app navigation graph with
update checks disabled, advanced through all four setup screens, and
verified that Finish opened the library and saved completion.

The signed release was installed in place on the Odin2 at 20:37. Its
installed APK matched the build's SHA-256
`a5592178ff65e6a66e311f29a3a2d610fff0dbd55fc2eb8e8859dc6b2aacdb25`.
The setup interaction was verified on the host. Device verification
covered the installation and APK hash.

## Overlay target pace (`none` vs `n/a`) and Republic Commando intro, 2026-11-22

*Star Wars: Republic Commando* (`4C410013` / `LA01907A`) exposed two intro bugs
and a `Target:` reporting bug:

- **PersistDisplay raw-RAM glitch**: `nc_dev_nv2a_display_mode` (`native/dev/nv2a.c`)
  now updates `NV_PCRTC_START`, and `hle_pbd_cpu_display` (`hw/xbox/hle/pb/pbdraw.c`)
  hands off `shown_id` via `display_mode_fb` when `AvSetDisplayMode` switches to
  a persisted GPU surface (`D3DDevice::PersistDisplay`), stopping `hle_pbd_scanout`
  from scanning freed heap RAM.
- **IMMEDIATE intro slowdown**: `pbdraw.c` (`swap_pace`) now bounds the decoder
  to 3 in-flight swaps (`HLE_FRAMES`), preventing clear-only `IMMEDIATE` loops
  from queueing tens of thousands of swaps ahead of Vulkan and exhausting the
  32 MiB upload arena on PVIDEO overlays.
- **Overlay `Target:` wording (`none` vs `n/a`) and scoring target**:
  `hw/xbox/hle/hitch.c` (`hle_hitch_pace`) now clamps cadence tracking to valid
  Xbox D3D8 vblank divisors (`1..4`) and distinguishes:
  - `Target: none (60 Hz)` (`HLE_PACE_NONE`, log `pace: none (60 Hz)`): the
    developer explicitly set no framerate cap/target (`D3DPRESENT_INTERVAL_IMMEDIATE`),
    kept distinct from the emulator's own "Uncapped frame rate" setting.
  - `Target: n/a (60 Hz)` (`HLE_PACE_NA`, log `pace: n/a (60 Hz)`): no valid
    target/cap is available or measurable due to an issue/missing pacing (8+
    stalled frames slower than 4 vblanks with no vsync cadence) or the
    emulator's own "Uncapped frame rate" override (`HAKUX_HLE_UNCAP`).
  - Both catalog extractors (`tools/catalog/extract.py`,
    `debugger/internal/catalog/extract.go`) parse `pace:` lines and cap original
    Xbox inferred targets at console refresh (`60.0` NTSC / `50.0` PAL) rather
    than `120.0`. See `docs/CATALOG.md` ("Target FPS, `none` vs `n/a`").

Host tests passed: `test.bat` 29/29, `tests/native/wireless/test_catalog.py` 11/11,
`go test ./internal/catalog/...`, and Android JVM `FpsOverlayTextTest`. Signed
release APK `build/apk/HakuXP-0.4.0.2c61c165-release.apk` (SHA-256
`78ba33800b3d84ec824a6c35b8467d68b2c3075dbea4b81dbe2cbe885769dbf3`) installed on
the Odin2; Republic Commando played its intro cleanly at ~30 overlay fps to the
Press Start screen with `Target: none (60 Hz)` on screen and `pace: none (60 Hz)`
in logcat (`build/screenshots/rc_target_none.png`). Not tested on device:
`Target: n/a (60 Hz)` under `HAKUX_HLE_UNCAP` or a stalled title, PAL 50 Hz.
