# Playbooks and the automation banner, first device run (2026-11-16)

The first run of playbooks and the red automation banner on the device
(docs/AUTOMATION.md, "Playbooks"), the evening after they were built.

- **Title:** Halo: Combat Evolved, title ID `4D530004`, XMID `MS00409A`
- **Image:** `Halo - Combat Evolved (USA) (Rev 2).iso` on the Odin2's SD
  card (the content URI in TESTING-GAMES.md); XBE MD5 unchecked
- **Device:** AYN Odin2 (Android 13, Adreno 740), over Wireless debugging;
  the app's GPU driver as installed
- **APK:** debug, SHA-256 `1d16d1267d63e628d8b88258002a100da7e16f68bcc163dc1612c3f14c157b93`
  (installed 15:53:57 by the guide session, with the owner's OK)
- **Evidence:** `build/playbooks/` (the playbooks, their runs' `checks.txt`,
  `result.txt` and pictures), `build/screenshots/playbook-hw1-banner.png`,
  `playbook-rec-banner.png`, `playbook-rec-stopped.png`

## What was run

All from the PC with `playbook.py`, the device awake and unlocked by the
owner (20:02: asleep and locked, `play` refused with exit 3 and started
nothing).

1. **A hand-written playbook** (`4D530004_handwritten-boot`, pushed with
   `playbook.bat push`): from the title's start, A at frames 300 and 345
   for 3 frames each, checkpoints without signatures at 600, 1200, 1800,
   the end at 2400. `play --wait`: Halo went past its intros to Select
   Profile; the banner showed, red, at the bottom left,
   `*Automated Testing In Progress*` and under it `4D530004_handwritten-boot:
   frame 615 of 2400, 1 checks, 0 differ`. It ended at frame 2401, then
   the tool closed the game and turned the banner off (exit 0). The three
   pictures were written (`nosig`: nothing to compare with).
2. **A recording from the title's start**, started by the PC (`record
   --uri`), with `debug.hakux.automation` set by hand (the agent drove it)
   and no one playing: `NC_PAD_SCRIPT` (through `debug.hakux.env`) pressed
   A at 300 and 345, Down at 1500 and Up at 1560, 3 frames each. The banner
   read `4D530004_rec-boot: frame 758, 4 changes, 1 checks`. Stopped from
   the PC at frame 2070. The playbook got the header (title, XMID, start
   boot, pads 1, image, recorded), the 8 changes at exactly 300, 303, 345,
   348, 1500, 1503, 1560 and 1563, checkpoints at 600, 1200 and 1800 with
   signatures and pictures, and `2070 end`.
3. **Two replays** of that recording, the pad script off
   (`debug.hakux.env none`), each with `play --wait`: both ended at frame
   2071 with all three checkpoints the same as recorded, difference 0
   (`playbook.bat compare` on the pulled runs; the pictures of frame 1800
   look the same: Select Profile).

## A mistake on the way, and the fix

The first try at step 2 passed the URI through `cmd.exe` with quotes
around it; the quotes ended up in the URI, the app couldn't open the disc
("This game can't start: The game's file can't be opened"), and the
recording ended at frame 0 ("game stopped"; the banner said so).
`playbook.bat end` closed the game behind the dialog. `playbook.py` now
takes quotes and spaces off a URI and refuses one that isn't `content://`
or `file://` (test added). The empty recording was deleted.

Also found: a folder `adb push` makes in the app's external files is the
shell user's; `push` now opens it (`chmod 2777`) so the app can write its
runs there. The app did write its run folders in it.

## What this shows, and what it doesn't

- Shown: the banner (playing, and recording while the PC drives), the
  PC's play, record, stop and end, the refusal while asleep and locked,
  recording what the title read at the frames it read it, writing
  checkpoint pictures and signatures, and two replays matching a recording
  on menus.
- Also seen, by the guide session at 15:54 on the same APK: Halo started
  normally with no banner (nothing was driving it), and guide > Debug
  lists the Playbooks row after Mark log
  (`build/screenshots/2026-11-16_guide-clock-debug.png`).
- **Not tested:** the menu the guide's Playbooks row opens (record from
  here or from the start, play, stop) and Mark log's checkpoint; recording from a
  state and playing from one (the state saved, the core started again into
  it); gameplay (only Halo's intros and menus, which change little; a
  difference of 0 there says little about drift in play); a title that
  relaunches; the banner over the on-screen controls (the Odin2's own
  controller hides them); `xboxrun`'s refusal (not pushed).
- The hand-written run's "0 not as recorded" counts checkpoints compared;
  its three had no signature, so nothing was compared.

Left on the device: both playbooks and their runs in the app's
`files/playbooks`, `debug.hakux.automation` `0`, `debug.hakux.env`
`none`, no game running.
