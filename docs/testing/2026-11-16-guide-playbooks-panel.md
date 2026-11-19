# The guide's Playbooks panel on the device (2026-11-16)

The Playbooks row moved from a pop-up dialog into the guide's own detail
panel (`Detail.ITEMS`, the general list panel; docs/PLAYBOOKS.md, "On the
device"). Checked on the Odin2 the same evening.

- **Device:** AYN Odin2 (Android 13), over Wireless debugging
- **APK:** debug `c2596e31bb1385205a518d49ec4b158ffee7747173d44342c9da10c351edb21e`,
  installed 21:54:55 with the owner's OK (the device's `base.apk` matched)
- **Game:** Halo (`4D530004`, `MS00409A`), started from the PC with the
  automation banner; the guide driven by `adb shell input keyevent` (Back,
  L1, D-pad, A, X, B), a pause between keys
- **Screenshots:** `build/screenshots/guide-playbooks-panel.png`,
  `guide-playbooks-entry.png`, `guide-playbooks-delete.png`,
  `guide-playbooks-playing.png`

## Seen

- Debug > Playbooks shows the panel as Save state's does: the summary ("4
  playbooks for this game. A plays one: ... X deletes it."), two dashed
  action cards with the record icon (Record from here, Record from the
  game's start, each with its line), then the four Halo playbooks, newest
  first: "Today 9:35 PM" with its starting state's picture (from a state),
  the others with the play icon (from the start), each with its last
  run's result.
- A, then down twice: the 9:35 PM playbook outlined, the list scrolled to
  it, and the footer's X Delete hint shown (not on the action cards).
- X: "Delete this playbook?" naming it (`4D530004_20261116-213431`); B
  cancelled it, and the playbook and its folder were still there.
- A on it: the guide closed, the game started again into its state and
  played it (run `20261116-215628`): ended at frame 924, 1 checkpoint, 0
  not as recorded; the banner showed it.

## Found

- The count read "1 checks" (panel, banner): the strings that count
  checkpoints are plurals now, and the banner says "N not as recorded" as
  the panel does. Built (debug APK
  `661f57bdfe47d719dd8ecfa7553b7ce4455cef0a681c775f906852570dce842f`), not
  installed by this session; it is in the guide session's whole-tree build
  `3e01d0b4...`, on the Odin2 since 2026-11-17 08:21, not looked at there.
- Moving down to a playbook scrolls it to the top, so the action cards go
  out of view: the same as the save state list.

## Not tested

Recording from the panel's cards (the same functions the PC's
`record:here` / `record:start` reached earlier), Stop from the panel,
deleting for real, a game without savestates.
