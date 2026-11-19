# A title's crash stops the game, not the app

**Title used to test it:** Halo: Combat Evolved (USA) (Rev 2), `4D530004`
`MS00409A` (certificate version 9).
**Image:** `Halo - Combat Evolved (USA) (Rev 2).iso` on the Odin2's SD card,
3,625,713,664 bytes.
**XBE:** `default.xbe`, 2,535,424 bytes, MD5
`7F6CC2900594C9E46C927D2D304B43DA`, read from the Odin2's image with
`tools/native/xbehash.py --adb` on 2026-11-16 (it matches the 2026-11-15
record's).
**Device:** AYN Odin2 (QCS8550, Adreno 740), Android 13. **GPU driver:**
PurpleVK 26.3.0-devel (git-62ac221a33), Turnip, the app's custom package
(copied next to the runner for the headless runs).
**APK:** debug, SHA-256 `e3dd725e47a82f78...` (built by the settings
session) and `97e52dc9f18b3a80037a72127402823709e2a52069637eb216f55bb4c7b9cbe4`.

## Before

A page fault in the title's code that the title has no handler for (no
#PF handler of its own in the IDT, as almost every title) went to
`nc_core_fault` (`native/core/core.c`), which logged
`host fault: the title doesn't handle it ...` and returned false: the
signal took its default action and the whole process died. In the app that
is the whole app, back to the home screen (Halo on 2026-11-15 at 16:59,
[the record](2026-11-15-halo-visibility-test.md)); headless, exit 3.

## Fix

- `nc_core_fault`: such a fault now logs
  `guest fault: the title doesn't handle it: read of X at eip Y`, the last
  kernel calls and the threads, and stops the guest:
  `nc_kernel_unhandled_fault` (`native/kernel/kernel.c`) sets the stop
  reason (`page fault at X from eip Y (read) not handled by the title`, the
  wording of the kernel's own trap stub) and resumes the faulting thread at
  the HLT stub, which leaves the JIT. The process goes on.
- `native/app/app_main.c`: when the guest stops on its own (not leaving to
  the dashboard, not relaunching), the core thread sets a new fatal kind,
  `NC_FATAL_STOPPED`, with the stop reason as a sentence.
- `NativeCoreActivity.kt`: that kind shows a dialog titled "The game
  stopped" with the reason and the "Export logs" hint, and OK goes back to
  the library (as the other fatal kinds do). New string
  `native_stopped_title`.
- `xboxrun` keeps its exit codes: a title's crash is now exit 7 (guest
  stopped) instead of 3; 3 is left for faults the core can't take as the
  guest's (outside translated code, or a mapped page).

Still killing the process: a fault inside a kernel call (the kernel's host
code touching a page the guest doesn't have, for a title without a page
handler), and faults outside the guest's memory or in the emulator's own
code.

## After

The kernel-area fix of 2026-11-15 keeps Halo from crashing, so the crash
was made again on purpose: a cheat (an `xboxrun` `mods=` manifest, not
shipped) writes 0 every vblank to the pointer to Halo's first
visibility-test page, `[0x00192390]+0x48C` (its D3D's
`BeginVisibilityTest` at 0x181080 keeps the pages there). Then
`GetVisibilityTestResult` reads NULL + 0xC, as it did before that fix.

Headless, 2026-11-16, Turnip (PurpleVK, loaded through `vkdriver=`), the
2026-11-15 copy of Halo's disk with its save and map cache (copied fresh
for each run), A pressed every 45 frames from frame 300. Logs in
`build/logs/xboxrun/2026-11-16-fixes/`.

| Run | Runner | Result |
| --- | --- | --- |
| `halo-crash-old` | before the fix, the sweep's (stripped, MD5 `9dcfd184...`), cheat on | 11 s, as the save loads: `host fault: the title doesn't handle it at guest 0000000c, eip 0018110f`, exit 3 (the app would close) |
| `halo-crash-new` | with the fix (stripped, MD5 `a51fc134...`), cheat on | 11 s: `guest fault: the title doesn't handle it: read of 0000000c at eip 0018110f`, then `stop: page fault at 0000000c from eip 0018110f (read) not handled by the title`, exit 7: the process went on and ended normally |
| `halo-plain-new` | with the fix, no cheat | the save loads, runs to the 90 s limit (exit 5) at 30 fps |

**In the app**, 2026-11-16, Turnip, Halo started from the PC (a VIEW
intent), capture `build/logs/xbdm/2026-11-16_10-26-20.128488.*`. During
its intro and main menu, `setprop debug.hakux.memscan poke=00192390:0`
zeroed its D3D device pointer (0x0018F800 before, read with
`peek=00192390:4`), so its next D3D call read address 0:

| APK | Result |
| --- | --- |
| `e3dd725e...` (installed 10:21 by the settings session, with this fix) | 10:27:00 `guest fault: the title doesn't handle it: read of 00000000 at eip 00188664`, `stop: ...`, `core: stopped`. A dialog over the last frame: "The game stopped" / "The game stopped: page fault at 00000000 from eip 00188664 (read) not handled by the title." / the Export-logs hint / OK (screenshot `build/screenshots/halo-game-stopped-dialog.png`) |
| `97e52dc9...` (installed 10:29:10; the message is now the reason alone) | 10:29:45, the same fault and stop; the dialog reads "Page fault at 00000000 from eip 00188664 (read) not handled by the title." (`halo-game-stopped-dialog-2.png`) |

Both times OK ended the game through the activity's own exit (`SIG: 9`,
as Exit to library), no `Fatal signal` in the capture. As the game was
started from outside the app, it went back to the Android home screen
(`exitGame` returns to the library only when the library started the
game: `launchedExternally`).

## Not tested

- OK back into the library after a game started from the library.
- A fault inside a kernel call (still closes the app), or outside the
  guest's memory.
- Other titles' crashes: none of the 22 titles in the first sweep crashed
  this way.
