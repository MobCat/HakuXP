# HakuXP project overview

Source audit: 2026-11-14. Build results are in [BUILDING.md](BUILDING.md).

## Baseline and scope

The starting files match hakuX master at
`a7cdb5a975e31730d488c47f6f10440b16c1e6f4`, dated 2026-10-31 UTC
(2026-11-01 in Sydney). This was checked using Git blob hashes from the
upstream tree: all 501 upstream files were present; before this work the
only changed upstream file was `.gitignore`. The draft Windows scripts
and TODO.md were additional local files. The folder had no root `.git`
then. Since 2026-11-15 it is a git repository with upstream's history:
branch `hakuxp` starts at the baseline commit, and the remote `upstream`
fetches from rfandango/hakuX (push disabled). The HakuXP changes are the
working tree's differences from that commit.

The Android app reports version 1.3.0, versionCode 9. The FEX gitlink pins
`52018bf46a8279b22bce738548ab3f40db467064`, not the latest FEX branch.
Keep that pairing until a deliberate upgrade.

Upstream's `x1_box` branch ends at `ba41160be642ae4f3f2607a987e721532c4bc900`
(2026-03-11, "fix build for linux"). That commit edits the old
`android/app/src/main/cpp/CMakeLists.txt` layout. It predates the current
standalone native core and is not a drop-in build fix for this tree.

Primary references: [baseline commit](https://github.com/rfandango/hakuX/commit/a7cdb5a975e31730d488c47f6f10440b16c1e6f4),
[FEX pin](https://github.com/rfandango/FEX/commit/52018bf46a8279b22bce738548ab3f40db467064),
[x1_box commit](https://github.com/rfandango/hakuX/commit/ba41160be642ae4f3f2607a987e721532c4bc900).

## How a game runs

1. The Kotlin/Compose launcher scans disc images through Android storage
   APIs, manages settings, controllers, HDD data, mods and save slots, and
   starts `NativeCoreActivity` in the separate `:native` process.
2. SDL3 loads `libhakuxcore.so`. `native/app/app_main.c` provides the JNI and
   SDL entry points, opens the disc and starts `nc_core_boot`.
3. `native/core/core.c` creates guest memory, kernel and emulated devices,
   opens XDVDFS, applies selected mods, loads the XBE and its kernel imports,
   creates the FEX CPU, and attaches graphics and savestate services.
4. FEX translates x86 instructions to ARM64. Kernel thunks enter the native
   Xbox kernel implementation. Fault handling and MMIO hooks route memory
   and port accesses to emulated devices.
5. The GPU path decodes NV2A commands, translates shaders, tracks guest
   memory and surfaces, and submits Vulkan work. SDL presents the result;
   adrenotools supports custom Android GPU drivers.
6. The MCPX APU runs voice processing and DSP code, with AAudio output.
   Network frames use disabled, NAT, LAN or XLink Kai backends.

This is a native kernel/HLE GPU design. The historical xemu/QEMU build is
not the current emulator core. Windows hosts the cross-compiler; both
`libhakuxcore.so` and `xboxrun` run on Android ARM64.

## Source map

| Area | Entry points and responsibilities |
| --- | --- |
| Android UI | `android/app/src/main/java/com/rfandango/haku_x/`; Compose, storage, HDD import/backup, updates, input |
| App/native bridge | `native/app/`; SDL/JNI lifecycle, display, vsync, Vulkan driver/layer loading |
| Boot and memory | `native/core/`; XDVDFS, XBE loader, VM, MMIO, IRQ, mods, cheats, memory scans |
| CPU | `native/cpu/cpu.cpp`; FEX embedding; implementation in `external/fex/` |
| Xbox kernel | `native/kernel/`; threads, scheduling, objects, waits, memory, file I/O, exceptions, crypto |
| Devices and audio | `native/dev/`; NV2A registers, USB/OHCI/XID (controller types and their USB identities in `xidtype.c`), NIC, ACI, MCPX APU |
| Graphics | `hw/xbox/hle/pb/`, `shader/`, `tex/`, `mem/`, `vk/`; command decoding through Vulkan |
| Networking | `native/net/`; packet queues, LAN, Kai, NAT/libslirp |
| Savestates | `native/state/`; versioned sections, field definitions, lock, schemas, migrations |
| Playbooks | `native/dev/playbook.c` (format, playback, recording), `native/dev/padplay.c` (the run's, at the pad reads), `native/app/playbook_jni.c`, `android/.../playbook/`, `tools/native/playbook.py`; recorded controller runs on screen, with the automated testing banner |
| Device harness | `native/run/xboxrun.c`, `tools/native/`; headless execution, captures, import census (off on devices since 2026-11-16: xboxrun exits 11 on Android) |
| Debug service | `native/debug/`, `debug.bat`; XBDM-compatible log streaming to PC-side xbdlog |
| Game patches | `mods/`, `tools/mods/`; title/version-specific XBE patches and generators |

## Tests and debugging resources

- [DEBUGGING.md](DEBUGGING.md) documents the modular debug harness, XBDM
  logging subset, PC log capture, and local xbdtools/DinoRec references.
  Start here for debugger work. Full guest execution control is pending.
- [PLAYBOOKS.md](PLAYBOOKS.md) is how to record, write, play and read
  playbooks: repeatable controller runs, savestates and state IDs.
- [TESTING-GAMES.md](TESTING-GAMES.md) is the runbook for testing a game on
  the device: in the app with `debug.bat`, repeatable runs with playbooks
  (`playbook.bat`; headless `xboxrun` is off on devices), and how to write
  the result down.
- `test.bat xbdm` tests the protocol, bounded log queue, actual transport
  and PC launcher cleanup. `test.bat xbdm client` also tests real xbdlog.
- `test.bat` runs 15 CTest executables in `tests/hle/`: XBE and disc parsing,
  resources, ring/arena/page tracking, push buffers, swizzling, shaders,
  APU HRTF, cheats and Vulkan compatibility mappings.
- Android JVM tests cover previous-version handling, overlay layout and HDD
  formats/backup/resigning. Run `test.bat jvm` as described in BUILDING.md.
- `tests/native/` also contains crypto, USB, RTL, GLib and audio output
  harnesses. The USB one (`tests/native/usb`, every controller type
  enumerated through OHCI) runs in `test.bat` as `test_usb`; the others
  are separate from that suite, are not all wired into one host command,
  and some assume POSIX/device facilities.
- `tests/native/fexrun/` and `tools/native/fexrun-suite.sh` run FEX ASM cases
  on Android. They need a separate build, NASM-generated inputs and ADB;
  host HLE tests do not validate the JIT.
- `python tools/native/knobs.py --md` discovers environment variables and
  `debug.hakux.*` properties from source. Use this instead of the missing
  upstream `tools/hle/README.md`.
- `tools/native/hxs.py` inspects savestates. `statelock.py` checks schemas;
  `statelock.py --sizes` uses native compile commands for record sizes.
  Follow `native/state/CHANGELOG.md` when changing saved layouts.
- `tools/native/census.sh` boots device-side ISO files using `xboxrun` and
  emits import/frontier logs. `imports.py` turns these into missing-import
  reports. These use Android shell, not Windows batch.
- Debug APKs carry the Khronos validation layer. RenderDoc is an optional
  local layer; see `android/app/src/debug/jniLibs/arm64-v8a/README.md`.

Savestates include guest kernel code and installed fast paths. Compare
kernel changes using a fresh boot as well as old-state loading. Keep the
unstripped native build for symbols and match it to the APK under test.

## Settings navigation

Settings use a fixed tab bar that spans wide screens and scrolls horizontally
when labels do not fit. Each page fills the remaining area with a lazy list.
The selected tab and each category's scroll position use Compose saved state,
including when returning from a child screen. The category modules are in
`android/app/src/main/java/com/rfandango/haku_x/ui/settings/`.

| Tab | Settings |
| --- | --- |
| General | Game folders, appearance, update checks |
| Graphics | Frame pacing, aspect ratio, render scaling, filters, shaders, GPU driver |
| Controls | Touch overlay editor and controller mappings |
| Console | Hard disk, migration, language, clock, guest video modes and AV cable |
| Network | Insignia, network mode, DNS, LAN peers and XLink Kai |
| Debug | FPS/hitch overlays, debug-only drive timing switch and log export |

Per-game overrides use the same tab component, with Display, Graphics,
Console and Console video categories. Reset still clears all overrides for
that game. Existing preference keys, defaults and next-start behavior are
unchanged. Update checks and log exports are owned by the containing screen
so they continue when changing tabs. `SettingsScreen.kt` handles navigation
and shared actions; category files own their controls and dialogs, with
shared choices in `SettingChoices.kt` and tab state in `SettingsTabs.kt`.
Folder labels and GPU driver metadata are read on the IO dispatcher. Host
Compose tests cover full-width tabs, full-size portrait/landscape content,
lazy composition and independent scroll restoration.

## Using the ROM set and debug tools

First create a manifest outside the source tree with title ID, title/version,
region, image format, image hash and exact XBE hash. Revisions matter for
mods and reproducing bugs. Keep images unchanged; use copies of saves and
per-run output directories.

Run a representative subset on a selected Android device before a full
census. Record build identity, device/Android/GPU/driver, settings and mods,
boot outcome, reached screen, logs, audio/video symptoms and performance.
The existing census measures early boot/imports, not whole-game playability,
and its default glob only covers `.iso`.

Rank missing exports by titles that actually call them. Use debugger traces
or hardware comparisons to establish expected behavior, then add focused
regression cases. Keep title-specific fixes separate from general kernel/GPU
behavior. Test graphics changes across GPU families before generalizing a
driver workaround.

`native/kernel/kexports_table.h` has 371 named entries: 337 functions and
34 data exports. The census parser finds 207 literal function registrations,
leaving 130 absent by that static heuristic. This is triage input, not a
compatibility score: registered functions can be stubs, and many missing
functions may never be called by retail games.

## Historical material

`HANDOFF.md` is an upstream snapshot from 2026-10-22. Its devices, private
paths and active task describe the original author's environment. Its
app-category and tools-tracking work has since landed. Preserve the DOA2
research as reference; use TODO.md as HakuXP's active backlog.

The referenced private review, `CLAUDE.md`, `tools/hle/README.md`, RE scripts
and optional shader generators are absent. The 65 review findings cannot
be reconstructed accurately from the handoff alone.
