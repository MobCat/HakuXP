# HakuXP packages and file formats

Every file and package format HakuXP defines, in one place: what each one
is for, its layout, how it is versioned, which code writes and reads it, and
whether it is in use, reserved or only proposed. Other documents keep their
how-to guides and link here for the formats.

Formats HakuXP only reads, such as Microsoft's or another project's, are
listed at the end ([Formats HakuXP reads but does not define](#formats-hakuxp-reads-but-does-not-define)).

## At a glance

| Format | File | Container | Version mark | Status |
| --- | --- | --- | --- | --- |
| [Dashboard package](#dashboard-packages-hxpdash) | `*.hxpdash` (recommended) or `*.zip` | ZIP with `manifest.json` | `"format": "hakuxp-package"`, `"type": "dashboard"`, `"schemaVersion": 1` | In use since 0.4.0 (shipped as `.zip` so far) |
| [Content pack](#content-packs-hxpdlc) | `*.hxpdlc` (recommended) or `*.zip` | ZIP with `manifest.json` | `"hakuxp-package"`, `"type"`: `title_update`, `dlc` or `content` | Parsed, no installer yet |
| [Save package](#save-packages-hxpsave-planned) | `*.hxpsave`; other tools' save zips import too | ZIP with a manifest | Not chosen | Planned |
| [Profile](#profiles-hxprof-planned) | `*.hxprof` | ZIP with a manifest | Not chosen | Planned (no profiles in HakuXP yet) |
| [Mod pack](#mod-packs-cheats-and-settings-planned) | `*.hxpmod` | ZIP with a manifest | Not chosen | Named, not designed |
| [Cheat package](#mod-packs-cheats-and-settings-planned) | `*.hxpcht` | ZIP with a manifest | Not chosen | Named, not designed |
| [Settings backup](#mod-packs-cheats-and-settings-planned) | `*.hxpset` | ZIP with a manifest | Not chosen | Named, not designed |
| [Run container](#run-containers-hxpb) | `*.hxpb` | ZIP | `"format": "hxpb"`, `"version": 1` | In use |
| [Achievement pack](#achievement-packs-hxpacv-proposed) | `<XMID>.hxpacv` | ZIP (proposed) | Not chosen | Proposed |
| [Savestate](#savestates-hxs-and-info) | `*.hxs`, `*.info` beside it | Binary sections; `.info` ASCII | Magic `HXS1`, container version, a version per section | In use |
| [Playbook](#playbooks-and-playbook-runs) | `<name>.txt`, run folders | ASCII text, folders | First line `hakuxp-playbook 1` | In use |
| [Built-in mods and cheats](#mods-and-cheats) | `mods/<TITLE ID>.<VERSION>/` | Folders in the APK | None | In use |
| [Backup and save zip](#backups-and-save-zips) | `HakuXP backup <date>.zip` | ZIP | `hakuX.properties`, `format=1` | In use (from hakuX) |
| [Log export](#log-exports) | `HakuXP-logs-<date>.zip` | ZIP of session zips | None | In use |
| [Dashboard library feed](#dashboard-library-feed) | `C:\uixdata\HakuXP\` | Files on the emulated C: | None | In use since 0.4.0 |

## File extensions

The owner's rule (2026-11-23):

- **`.hxs` stays** for savestates, the format HakuXP shares with hakuX.
- **Every new file type HakuXP adds starts with `.hxp`**: `.hxpb` (HakuXP
  playbook runs), `.hxpdash` (dashboards), `.hxpdlc` (content packs: title
  updates, DLC and other official game files), `.hxpsave` (saves),
  `.hxprof` (profiles), `.hxpacv` (achievement packs), `.hxpmod` (mod
  packs), `.hxpcht` (cheat packages), `.hxpset` (settings backup and
  restore).
- **Older and other tools' files keep working**: a dashboard `.zip`
  (with or without a manifest), and save `.zip`s from Action Replay and
  other tools, import as before. The `.hxp` name is what HakuXP writes and
  recommends.
- Formats HakuXP only reads keep their own extensions (`.iso`, `.xbe`,
  `.qcow2`, `.bin`, a GPU driver's `.zip`).
- **The extension is only a suggestion.** What a package is comes from the
  `manifest.json` inside it (its `format`, and for `hakuxp-package` its
  `type`), never from the file's name. A reader opens the manifest first
  and refuses a format, type or version it doesn't know, whatever the
  file is called; a renamed file still installs as what it is.

**One type, many kinds.** A new kind of content goes into an existing type
with its manifest saying which kind, rather than getting an extension of
its own: texture packs, shader mods and gameplay mods are all `.hxpmod`.

**Not adding:**

- **No log package.** Logs stay plain text anyone can read, or travel
  inside a playbook run (`.hxpb`), which already carries the log with its
  screenshots, input and savestates.
- **`.hxpgame` (a per-game profile) and `.hxppad` (controller mappings and
  on-screen layouts)** are undecided until the per-game settings page
  settles.

## Conventions

These hold for the formats above today. Keep them for new formats unless a
format's section says otherwise.

- **Containers are plain ZIP archives**, so Android (`java.util.zip`),
  Python (`zipfile`), Go (`archive/zip`) and PHP (`ZipArchive`) read them
  with their standard libraries.
- **A JSON manifest at the root names the format and its version**, and is
  what identifies the file (see [File extensions](#file-extensions)). A
  reader refuses a format name or version it does not know.
- **All of a package's metadata lives in its `manifest.json`** (the owner,
  2026-11-23): what it is, what it holds, where it came from (which
  profile and user exported a save), which keys signed it. No new metadata
  formats and no sidecar files (`.info`, `.ini`, `.properties`) inside an
  `.hxp` package; the other files in it are the content itself. A new need
  is a new manifest field.
- **Games are named by title ID and XMID**, title IDs as uppercase hex
  (`4D53002A`, `MS04204A`), never by an image's file name. Mods are older
  and use title ID plus certificate version instead (see
  [Mods and cheats](#mods-and-cheats)).
- **Text HakuXP writes is ASCII** (playbooks, `.info`, result files); JSON
  is UTF-8.
- **Package metadata never lands on the emulated FATX partitions.** A
  manifest and its icon stay beside the hard disk (`<hdd>/packages/`), not
  on C: or E:.

## Packages with `manifest.json` (`hakuxp-package`)

HakuXP installs dashboards (and, later, title updates, DLC and other
console content) from ZIP archives carrying a `manifest.json` with
`"format": "hakuxp-package"`: `.hxpdash` and `.hxpdlc`, or `.zip` for
older ones. The manifest sits at the archive's root (or inside a
single top-level wrapper folder); a legacy zip has none.

- **Written by:** `Theseus/package-hakuxp.py` (the HakuXP Dashboard), or by
  hand.
- **Read by:** the app: `data/hdd/PackageManifest.kt` (parsing and path
  checks), `data/hdd/Dashboard.kt` (inspect, install, uninstall),
  `data/hdd/PackageStore.kt` (the installed record),
  `ui/hdd/DashboardInstallDialog.kt` (the confirmation).
- **Status:** only `"type": "dashboard"` installs today; the installer
  refuses other types (`this package is '<type>', not a dashboard`).

### Schema v1

The HakuXP Dashboard's own manifest (`HakuXP-Dashboard-0.1.7.zip`):

```json
{
  "format": "hakuxp-package",
  "schemaVersion": 1,
  "type": "dashboard",
  "id": "hakuxp.theseus",
  "name": "HakuXP Dashboard (Theseus)",
  "icon": "icon.png",
  "version": "0.1.7",
  "versionCode": 8,
  "author": "HakuXP / MrMilenko (Team UIX)",
  "description": "Original Xbox dashboard based on Theseus (OG Xbox XDK 5849 build).",
  "homepage": "https://github.com/MrMilenko/Theseus",
  "license": "GPL-3.0-or-later",
  "dashboard": {
    "entryXbe": "c:/dashboard.xbe",
    "supportsInsignia": true
  },
  "install": {
    "layout": "partitions",
    "preserveOnUpdate": [
      "c:/UIX Configs/config.ini",
      "c:/UIX Configs/network.ini",
      "c:/uixdata/System/Config.ini",
      "c:/uixdata/Skins/**"
    ],
    "cleanBeforeInstall": [
      "c:/uixdata/Xips"
    ]
  }
}
```

### Required top-level fields

- `format`: must be `"hakuxp-package"`. Marks the file as a HakuXP package
  manifest rather than part of the Xbox title or dashboard itself.
- `schemaVersion`: integer, currently `1`.
- `type`: one of `"dashboard"`, `"title_update"`, `"dlc"`, `"content"`,
  `"save"`, `"mod"`.
- `id`: stable package identifier matching `[A-Za-z0-9][A-Za-z0-9._-]{0,63}`.
  It tells updating or repairing the same package from replacing it with
  another.
- `name`: the title shown in confirmation dialogs, settings and package
  browsers.
- `version`: human-readable version string (for example `"0.1.7"`).
- `versionCode`: positive integer. Within one `id` it classifies an install
  as an update (`>`), a repair (`==`) or a downgrade (`<`).

### Optional top-level fields

- `icon`: a path inside the zip (for example `"icon.png"`). Shown in the
  confirmation dialog and kept with the installed record under
  `<hdd>/packages/` for settings and browser views. The icon and
  `manifest.json` are metadata: they are never unpacked onto the Xbox
  partitions.
- `author`, `description`, `homepage`, `license`: shown to the user.
- `minAppVersion`: parsed but not enforced yet.

### Dashboard packages (`.hxpdash`)

`"type": "dashboard"`. `.hxpdash` is the recommended name; `.zip` stays
accepted so older dashboard packs still install. The HakuXP Dashboard
package is `HakuXP-Dashboard-0.1.7.zip` so far, and the app's file picker
offers zip files only: both move to `.hxpdash`.

- `dashboard.entryXbe`: Xbox path of the dashboard executable to boot
  (default `"c:/xboxdash.xbe"`). HakuXP passes the NT device path (for
  example `\Device\Harddisk0\Partition2\dashboard.xbe`) to the native core
  as `dashpath=`, used both for the library's **Dashboard** button and for a
  title's `LDT_LAUNCH_DASHBOARD` relaunch.
- `dashboard.supportsInsignia`: hint that the dashboard can manage Xbox
  Live (Insignia) accounts.

### Content packs (`.hxpdlc`)

One pack type for a game's official content: title updates (`$u`),
downloadable content (`$c\<offering ID>`) and other files a game or its
disc installs to E:. `"type"` says which: `"title_update"`, `"dlc"` or
`"content"`. Today such content comes as a plain zip. On import, content
metadata (`ContentMeta.xbx`) is re-signed for the console unless it was
already signed with its key (see [Signing and console keys](#signing-and-console-keys)).

Named `.hxpdlc` (the owner, 2026-11-23), with `.zip` still accepted: on the
original Xbox both title updates and add-ons came down from Xbox Live as
downloadable content into `TDATA`, and players know the word. The
manifest's `type` tells the install dialog which it is.

Reserved for the content installer and browser. Parsed and validated
today, not installed:

```json
{
  "format": "hakuxp-package",
  "schemaVersion": 1,
  "type": "title_update",
  "id": "54430003.tu.0001020400010604",
  "name": "Ninja Gaiden - Hurricane Pack Update",
  "icon": "icon.png",
  "version": "1.0",
  "versionCode": 1,
  "content": {
    "titleId": "54430003",
    "offeringId": "00010204",
    "resignContentMeta": true
  },
  "install": {
    "layout": "mapped",
    "mappings": [
      { "from": ".", "to": "e:/TDATA/54430003/$u" }
    ]
  }
}
```

`content.titleId` and `content.offeringId` are uppercased when read.

### Install layouts and rules

Xbox drive prefixes map to HakuXP's hard disk partition folders:

| Drive | Partition folder | Role |
|---|---|---|
| `c:` | `Partition2` | System / dashboard |
| `e:` | `Partition1` | Saves (`UDATA`), title data (`TDATA`), music |
| `x:` / `y:` / `z:` | `Partition3` / `4` / `5` | Cache partitions |

Paths may be written `c:/dashboard.xbe`, `C:\UIX Configs\config.ini` or
`/c/uixdata/Xips`. Unknown drives and `.` or `..` segments are refused.

- `install.layout: "partitions"` (default): zip entries are rooted by drive
  letter (`c/...`, `e/...`, `x/...`, `y/...`, `z/...`).
- `install.layout: "mapped"`: zip entries under each mapping's `from` path
  (`"."` for the zip's root) are placed under the Xbox path `to`.
- `install.preserveOnUpdate`: case-insensitive Xbox path patterns (`*`
  within a segment, `**` across folders) kept when updating or repairing
  with "Keep existing settings and skins" on.
- `install.cleanBeforeInstall`: Xbox files or folders cleared before
  unpacking (respecting `preserveOnUpdate` when it applies), so obsolete
  files such as old XIPs do not stay after an upgrade.

### Installed record

`<hdd>/packages/dashboard/installed.json` holds the manifest, the install
time, every file installed and the icon's name; the icon sits beside it.
Uninstalling deletes those files (or the whole C: drive, if asked), the
record, and the X:, Y: and Z: cache drives. Content packages will use
`<hdd>/packages/content/<id>/`.

### Legacy zips (no `manifest.json`)

- **Dashboards**: a zip holding `xboxdash.xbe` (at its root or in a
  folder) installs onto C: as before and boots
  `\Device\Harddisk0\Partition2\xboxdash.xbe`.
- **Content (planned)**: a zip without `manifest.json` will ask for the
  target path, suggesting one from `contentmeta.xbx` or `default.xbe` when
  present (for example `e:/TDATA/<TitleID>/$u` or
  `e:/TDATA/<TitleID>/$c/<OfferingID>`).

## Signing and console keys

Save packages and content packs move signed data between consoles, so
both follow the same rules. How the signatures work (from
`data/hdd/SaveResigner.kt`, after feudalnate's port of XAPI's
`XCalculateSignature`):

- **Roaming saves** are signed with the title's key alone:
  HMAC-SHA1(title key, data), the title key coming from the game's XBE
  certificate. They work on any console as they are.
- **Non-roaming saves** are locked to a console: HMAC-SHA1(HD key, the
  roaming signature), the HD key being the console's hard disk key from
  its EEPROM.
- **Content metadata** (`ContentMeta.xbx` of title updates and DLC):
  HMAC-SHA1(HMAC-SHA1(HD key, title ID), its header).

Re-signing replaces a signature only when it checks with the source keys,
and needs the game's title key, so a title whose game isn't in the library
keeps its files as they are. HakuXP already re-signs saves brought in from
another console or from xemu (`SaveResigner`, `ConsoleKeys.kt`).

**The nulled HD key** (the owner, 2026-11-23): when a non-roaming save or
content is exported without the console's own HD key, HakuXP re-signs it
with an HD key of all ones (16 bytes of `0xFF`), as Insignia suggests for a
nulled key. An all-zero key doesn't work with Insignia and Xbox Live.

**The manifest records the signing key**, so an import knows what to do:

- which HD key signed the pack: `nulled`, or a console's;
- a fingerprint of that key (a hash, never the key itself);
- whether the key itself is in the pack (only if the user chose to export
  it).

On import, a pack whose fingerprint matches this console's HD key is copied
as it is, with no re-signing. Otherwise HakuXP re-signs from the pack's key
(the nulled key, or the console key the pack carries) to this console's.
An export therefore always signs with a key an importer can have: the
nulled key, or the console's own when the user includes it.

## Save packages (`.hxpsave`, planned)

HakuXP's own save file, carrying more than a save zip can, with a manifest
that says what is in it and which key signed it.

- **Imports stay open:** Action Replay save `.zip`s and other tools'
  exports (the `E:\UDATA` layouts `SaveArchive` reads today), and hakuX's
  and HakuXP's own save zips, import as before.
- **What it can hold**, chosen when exporting:
  - a game's saves (`E:\UDATA\<title ID>`) and title data
    (`E:\TDATA\<title ID>`);
  - its data on the cache drives (X:, Y:, Z:);
  - its title update and DLC;
  - the console's HD key, if the user wishes;
  - a profile (`.hxprof`), if the user wishes.
- **Signing:** as [Signing and console keys](#signing-and-console-keys)
  says. A non-roaming save exported without the HD key is signed with the
  nulled key; the manifest records the key, and an import with a matching
  key skips re-signing. Saves, DLC and title updates are re-signed on
  import when the keys differ.
- **Games** are named by title ID and XMID, as everywhere.
- **Privacy:** an HD key or a profile's credentials in the file identify
  the console or the account. Both are off unless the user ticks them, and
  the import dialog says when a pack carries them.

Everything about the export is in its manifest: the games, what it holds,
the signing key, and which profile and user it came from. A manifest sketch
(field names not settled):

```json
{
  "format": "hakuxp-package",
  "schemaVersion": 1,
  "type": "save",
  "id": "4D530004.MS00409A.saves",
  "name": "Halo: Combat Evolved saves",
  "version": "1",
  "versionCode": 1,
  "save": {
    "titles": [{ "titleId": "4D530004", "xmid": "MS00409A" }],
    "contains": ["udata", "tdata", "cache", "title_update", "dlc"],
    "exportedAt": "2026-11-23T10:00:00Z",
    "exportedBy": { "profile": "<profile ID>", "gamertag": "<gamertag>", "app": "0.4.0" },
    "signing": {
      "hdKey": "nulled",
      "hdKeyHash": "<sha256 of the 16-byte key>",
      "hdKeyIncluded": false
    },
    "profileIncluded": false
  }
}
```

## Profiles (`.hxprof`, planned)

One user's own things, neither a console setting nor a game setting.
HakuXP has no profiles yet; this is where they will be kept.

- **Xbox Live:** the account as the hard disk keeps it (the online account
  sectors).
- **HakuXP's profile:** gamertag, profile icons, earned achievements and
  points (progress stays out of `.hxpacv` packs, keyed by achievement ID).
- **The network provider and its settings:** Insignia, XB.Live and the
  others in [ACHIEVEMENTS.md](ACHIEVEMENTS.md) ("Future Xbox Live
  providers").
- **Alone or inside a save package:** `.hxprof` on its own, or included in
  a `.hxpsave` when the user chooses to export or import a profile with
  the saves.
- **Open:** how the account's credentials are protected inside the file.

## Mod packs, cheats and settings (planned)

Named, not designed yet. Each is a ZIP with a manifest saying what it is.

- **`.hxpmod`: mod packs.** One type for every mod; the manifest tells
  HakuXP and the user which kind it is: a texture pack, a shader mod, a
  gameplay mod, XBE patches, replaced disc files. No separate texture or
  shader extensions. Today's built-in mods
  ([Mods and cheats](#mods-and-cheats): `mod.txt`, `files/`) are the
  starting point.
- **`.hxpcht`: cheat packages.** Today cheats are a `cheats.txt` in a
  built-in mod folder.
- **`.hxpset`: settings backup and restore.** Whether a game's own
  settings go here or into a per-game profile (`.hxpgame`, undecided)
  waits for the per-game settings page.

## Run containers (`.hxpb`)

One test run, packed with everything needed to score and review it on
another machine: the run's summary and score, its 1-second timeline, the
playbook and its results, the log and the screenshots. Despite the name
("HakuXP playbook"), it also packs `debug.bat` app captures.

- **Written by:** `catalog.bat pack` (`tools/catalog/hxpb.py`) and the
  tester app `tester.exe` (`debugger/internal/catalog/hxpb.go`).
- **Read by:** `catalog.bat inspect` and `ingest`, `tester-admin.exe`
  (`validate`, `ingest`), and `compatibility.php?download=<run_id>`, which
  serves `tools/catalog/web/runs/<run_id>.hxpb`.
- **Scoring, timeline and database:** [CATALOG.md](CATALOG.md).

### Layout (v1)

```text
<run_id>.hxpb
  manifest.json        Format, version, run identity, score, SHA-256 of every other member
  summary.json         Normalized run, score breakdown, libraries, checkpoints, marks, events
  timeline.jsonl       Canonical 1-second timeline, one JSON object per line
  device.json          The device's specs as it reported them (optional)
  playbook.txt         The playbook's text, hakuxp-playbook 1 (optional)
  result.txt           The playback's result.txt (optional)
  checks.txt           The playback's checks.txt (optional)
  taken.txt            The playback's taken.txt (optional)
  log/run.log          Paired logcat or debug.bat capture (optional, DEFLATE)
  frames/<name>.png    Checkpoint screenshots (optional, stored uncompressed)
  marks/<name>.jpg     Mark log and tester screenshots as JPEGs (optional, Go writer)
  grid.jpg             3x3 grid of the run's scheduled screenshots (optional, Go writer)
```

Run IDs look like `41510003_AQ00301J_2026-11-21_22-07-01_6ea8e2fa`: title
ID, XMID, start time and a short hash.

### `manifest.json`

```json
{
  "format": "hxpb",
  "version": 1,
  "created_at": "2026-11-21T11:07:01+00:00",
  "run_id": "...",
  "started_at": "...",
  "lane": "playbook",
  "title_id": "41510003",
  "xmid": "AQ00301J",
  "image_name": "...",
  "playbook_name": "...",
  "playbook_run": "...",
  "device": "...",
  "device_id": "5f6c558fa7d12a4e",
  "gpu_driver": "...",
  "app_version": "...",
  "apk_sha256": "...",
  "git_commit": "...",
  "score": 0,
  "score_version": "...",
  "timeline_seconds": 0,
  "files": { "summary.json": "<sha256>", "timeline.jsonl": "<sha256>" }
}
```

`files` maps every other member's path to its lowercase hex SHA-256.
`inspect` and `ingest` check each digest before accepting the archive.
`device_id` is the catalog's hardware hash (see [CATALOG.md](CATALOG.md),
"Per-device rollup scoring"); a reader computes it from `device` or
`device.json` when it is missing.

### What the two writers differ in

The Python and Go writers both write version 1, but not the same members:

| Member | `hxpb.py` | `hxpb.go` |
| --- | --- | --- |
| `device.json` | Read only | Written and read |
| `frames/*.png` | Full size | Optionally shrunk (`FrameMaxWidth`) |
| `marks/*.jpg` | No | Written and read |
| `grid.jpg` | No | Written and read |

## Achievement packs (`.hxpacv`, proposed)

Not built yet. The plan, its research and the session rules (one XMID for
the whole game session, relaunches, title updates) are in
[ACHIEVEMENTS.md](ACHIEVEMENTS.md). The proposed format:

- **One pack per release**, named by the disc's XMID (`VU02101K.hxpacv`),
  found when the game starts. The same XMID looks up the game's patches and
  cheats.
- **Container:** start from `.hxpb`'s approach (a ZIP with a manifest), with
  its own format name and version.
- **Manifest:** the release's XMID, the pack's revision, the XBE paths and
  hashes it supports, and the memory map for each build.
- **Per-XBE memory watches and events**, so two builds can share a rule
  when their addresses differ.
- **Achievements:** stable IDs, unlock conditions, display strings, points
  and icons. A player's progress stays outside the pack, keyed by those
  IDs, so replacing a pack to fix a rule keeps earned unlocks.

File names inside the pack and the rule syntax are still open.

## Savestates (`.hxs` and `.info`)

- **Written and read by:** the native core (`native/state/`). The app
  writes the `.info` (`data/StateInfo.kt`); the core writes it for a
  playbook's save lines (`native/dev/padstate.c`).
- **Where:** save slots are `slot<N>.hxs` in the game's states folder; a
  playbook's states are `<playbook>/states/<state ID>.hxs`; a recording's
  starting state is `<playbook>.hxs` with a `<playbook>.jpg` picture.

### `.hxs`

Magic `HXS1`, the container version, a header (`NcStateInfo`'s fields,
length first so a longer header still reads), a section table (tag,
version, offset, length and CRC-32 per section), then the sections. Each
section (`KERN`, `PBD`, `USB`, `FILE`...) has its own version. A change to
a section's layout raises its version, adds a schema file under
`native/state/schema/` and an entry in `native/state/CHANGELOG.md`; older
versions are upgraded when they load. `test.bat state` checks the schema
locks (`tools/native/statelock.py`).

A state exported with a playbook brings its cache files with it
(`nc_cache_export`; the cache index starts with `HXI2`).

### `.info`

ASCII `key=value` lines beside the state (`slot3.hxs`, `slot3.info`):

```
id=4D530004_MS00409A_20261116-210400
title=Halo: Combat Evolved
xmid=MS00409A
saved=2026-11-16 21:04:00
by=playbook
label=after intro
frame=1200
part=1
playbook=4D530004_rec-states
game=Halo.iso
app=0.4.0
```

The state ID is `<title ID>_<XMID>_<yyyyMMdd-HHmmss>`. `by` is `guide` (a
save slot, with `slot`), `playbook` (a save line, with `label`, `frame`,
`part`) or `recording`. Every key but `id` is optional.

## Playbooks and playbook runs

A playbook is a text file of controller input by frame, with checkpoints
and savestate lines. The format reference and how to write one are in
[PLAYBOOKS.md](PLAYBOOKS.md) ("Format reference", "Reading the results");
this section only lists the files.

- **The playbook:** `<name>.txt`, ASCII, first line `hakuxp-playbook 1`. A
  recording's name is `<TITLE ID>_<yyyyMMdd-HHmmss>`. In the repository,
  `playbooks/<TITLE ID>_<XMID>/` holds them per release.
- **On the device:** the app's external files folder, `playbooks/`, which
  the PC reaches with `adb push` and `pull`.
- **A run folder** per playback, `<name>/<date-time>/` (`<name>/rec/` for
  the recording): `result.txt` (`key=value`, rewritten every 2 seconds),
  `checks.txt` (each checkpoint's verdict), `taken.txt` (the input as the
  game took it, frame by frame) and `f<frame>.png` checkpoint pictures.
- **Read by:** the core (`native/dev/playbook.c`, `padplay.c`),
  `playbook.bat`, and the catalog, which packs a run into an `.hxpb`.

## Mods and cheats

Folders the build packs into the APK (`syncModAssets`). The authoring
guide is [mods/README.md](../mods/README.md).

```
mods/
  <TITLE ID>.<VERSION>/    the disc release: title ID and certificate version (54430006.6)
    cheats.txt             its cheats (optional)
    <mod>/                 one mod per folder (the folder name is its id)
      mod.txt              name, description, author, refresh, XBE patches
      files/               disc files it replaces or adds (optional)
```

Text is UTF-8, `#` starts a comment, numbers are hex with `0x`, else
decimal. A savestate keeps the patches of the mods that were on (its
`MODS` section), so a load with other mods takes them back first.

## Backups and save zips

The hard disk backup from hakuX, kept as it was so hakuX and HakuXP read
each other's zips (`data/hdd/Backup.kt`, `SaveArchive`).

- **The first entry is `hakuX.properties`**, a Java properties file:
  `format=1`, `kind` (`backup` or `saves`), `created`, `states` and
  `caches` (whether savestates and the X/Y/Z cache drives are in it).
- **A backup** holds the hard disk folder: each partition's folder, the raw
  partition heads and `eeprom.bin`, and `states/` when asked. Saves stay
  signed for the console's key, so no BIOS is needed to make or restore one.
- **Names:** new backups are `HakuXP backup <date>.zip`; hakuX's restore as
  before.

## Log exports

`HakuXP-logs-<date>.zip`, made by Settings' Export logs
(`data/Logs.kt`): one zip per app session (`logs/sessions/<start>.zip`,
with why its processes ended in `exits.txt`) plus the running session.
No version mark: it is for people to read, not for a program to load.

## Dashboard library feed

Files the app and the HakuXP Dashboard pass between them on the emulated
C: drive (`data/hdd/DashboardLibrary.kt`), so the dashboard's launcher lists
the HakuXP library and can start a game from it.

```
C:\uixdata\HakuXP\           (Partition2/uixdata/HakuXP/)
  library.tsv     <key> TAB <title ID> TAB <title> TAB <icon file>, one line per game
  icons/<key>.bmp 24-bit uncompressed BMP, at most 128 x 128 (optional)
  launch.req      <key>, written by the dashboard just before it reboots
```

The app rewrites the feed each time it starts the dashboard, and clears
any old `launch.req` first. Its own map
from `<key>` to the game's image (`files/dashboard_library.tsv`) never goes
on the Xbox drive. When the dashboard reboots with a `launch.req`, the app
starts that game in a new core process and deletes the request.

## Formats HakuXP reads but does not define

- **Disc images:** XISO, and full DVD dumps (Redump-style ISOs).
- **XBE** executables, and Microsoft's own content on E:
  (`UDATA`, `TDATA`, `$u` title updates, `$c` content, `contentmeta.xbx`).
- **Hard disks and EEPROMs:** xemu's qcow2 images or raw dumps, and
  `eeprom.bin` (the console's 256-byte EEPROM).
- **GPU drivers:** adrenotools driver ZIPs (Turnip and others), with their
  `meta.json`.
- **The dashboard's own files** inside its package, such as Theseus's
  `.xip` archives and `.ini` settings.

## Open questions

Things to settle while working on these formats:

- **`.hxpb` has two writers with different members** (the table above).
  Either write the same members from both, or keep the extra ones optional
  and say which readers need them, before a version 2.
- **`minAppVersion`** is read but not checked: an older app installs a
  package meant for a newer one.
- **Content types** (`title_update`, `dlc`, `content`, `save`, `mod`) are
  parsed but have no installer yet.
- **The `.hxpacv` container and rule syntax** are not chosen.
- **Mods use title ID plus certificate version; achievements and runs use
  XMID.** ACHIEVEMENTS.md proposes adding XMID lookup for mods and keeping
  installed mods working.
- **`.hxpdash` in the app and the packager**: the file picker offers zip
  MIME types only, and `package-hakuxp.py` writes `.zip`. Android has no
  MIME type for `.hxpdash`, so the picker has to accept any file and let
  the manifest decide.
- **Two kinds of manifest**: content packages use
  `"format": "hakuxp-package"` with a `type`, while `.hxpb` uses
  `"format": "hxpb"`. Decide which the new types (`.hxpmod`, `.hxpcht`,
  `.hxpset`, `.hxpacv`) use: a `type` under `hakuxp-package` (the schema
  already lists `mod` and `save`) or a `format` of their own.
