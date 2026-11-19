# Savestates in playbooks and state IDs, device run (2026-11-16)

The save and load lines, state IDs and `.info` files
(docs/AUTOMATION.md, "Savestates in a playbook") on the device, the same
evening they were built.

- **Title:** Halo: Combat Evolved, `4D530004`, `MS00409A`, the image in
  TESTING-GAMES.md; XBE MD5 unchecked
- **Device:** AYN Odin2 (Android 13, Adreno 740), over Wireless debugging
- **APKs** (both installed with the owner's OK): debug
  `3fab7cfb353de43b830e5fa523a557125931d8f2dd63a6489431cd5646079fd9`
  (21:25:01), then `9a5a97414589a43085b4a429a0820b7d17cdc30b89aef498d8d7ff59e11eba93`
  (21:31:24) with the fix below; the device's `base.apk` matched each
- **Evidence:** `build/playbooks/4D530004_rec-states/` (pulled: the
  playbook, its states, runs `rec`, `s1`, `s2`, `s3`),
  `build/screenshots/playbook-rec-part2.png`, `playbook-rec-here.png`

## What was run

All from the PC (`playbook.py`, `debug.hakux.state`), the banner on
(`debug.hakux.automation` set), the owner's Halo slots 1 and 2 left alone.

1. **A recording with the guide's Save and Load state.** From Halo's start,
   `NC_PAD_SCRIPT` pressing A at 300 and 345. At frame 707
   `debug.hakux.state save:10` (the guide's Save state): slot 10 got its
   `slot10.info` (`id=4D530004_MS00409A_20261116-212538`, title, XMID,
   saved, by guide, slot, game, app) and the recording a `742 save slot10`
   line. Then `load:10`: the state was copied into the playbook's
   `states/` with its 34 cache files and an `.info` (by recording, part 1),
   the part ended with `1329 load 4D530004_MS00409A_20261116-212538`, and
   the recording went on in part 2 from that state: the banner read
   `4D530004_rec-states, part 2: frame 407, 4 changes, 2 checks`, and the
   scripted A presses went from Select Profile to Choose Difficulty.
   Stopped at part 2's frame 830 (`830 end`), its checkpoint `1:600`
   (`s1_f600.png`).
2. **Three replays** (`play --wait`, the script off): each saved a state
   at frame 742 into its run (`<run>/4D530004_MS00409A_<time>.hxs`, its
   `.info`: by playbook, frame 742, part 0, label slot10), loaded the
   kept state at the load line, played part 2 into the same run, and ended
   at part 2's frame 831 with all three checkpoints (600, 1200, 1:600) the
   same as recorded, difference 0. `playbook.bat states` listed the three
   IDs; `compare` lined up `rec`, `s1` and `s2`.
3. **Record from here** (`playbook.bat record --here`, Halo left at Select
   Profile by a hand-written playbook with `--keep`): the start state went
   into the new playbook's `states/` as `4D530004_MS00409A_20261116-213431`
   (header `start state <that ID>`), with its `.info`, picture and cache
   files; stopped at frame 923. Played back from that state: ended at 924,
   its checkpoint the same (difference 0).

## Found and fixed

The core's state saver makes a state's cache folder (`blobs/`) the app's
alone (0700, files 0600). In the save slots that is right; in a playbook's
folder it kept `adb pull` from the cache files (runs s1 and s2 came to the
PC without theirs). `nc_padstate_share` now opens a playbook state's
`blobs/` to the folder's group after each save (run saves, and "Record
from here"'s start state). Checked with the second APK: run s3 and the
"from here" state pulled with their 34 cache files each. Runs s1 and s2,
saved before the fix, keep private cache folders.

## Not tested

- The guide's own menus for this (Playbooks > Record from here, the guide's
  Save state and Load state while recording): the same code paths, reached
  here through `debug.hakux.state` and `debug.hakux.playbook`.
- A load line naming a guide save slot by its ID (only the playbook's own
  states were loaded); a `push`ed playbook's states on another device.
- Gameplay: everything stayed in Halo's menus.

## Left on the device

The playbooks `4D530004_rec-states` (with runs rec, s1, s2, s3) and
`4D530004_20261116-213431` (run h1) in the app's `files/playbooks`; the
slot 10 test state removed (cache files only it used stay in Halo's slot
store until its next save tidies them); `debug.hakux.env` `none`,
`debug.hakux.automation` `0`, no game running.
