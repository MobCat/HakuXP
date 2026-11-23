# HakuXP Test Run Catalog, Scoring and `.hxpb` Container

`catalog.bat` (`tools/catalog/`) extracts telemetry from HakuXP runs across all
three testing lanes (`debug.bat` app captures, playbook runs, and legacy
`xboxrun` sweeps), builds a canonical **1-second timeline**, scores each run on
a **0-100 scale**, packages runs into self-contained **`.hxpb`** containers,
and stores everything in a **SQLite database** that can be queried from the CLI,
the PHP web viewer (`tools/catalog/web/compatibility.php`), or a future Go uploader.

All tools use Python's standard library only (`sqlite3`, `zipfile`, `zlib`,
`json`) so testers do not need to install extra packages or run the full native
debug toolchain.

## Quick start

Ingest all existing HakuXP app & playbook captures (`build/logs/xbdm`,
`build/logs/playruns`, `build/playbooks`, `playbooks/`). Runs are validated
against the official 2,179-release database (`docs/titleIDs.db`, synced into
`catalog_titles`) and filtered to HakuXP builds by default; pass
`--include-all` to include unversioned, headless, or unofficial titles, or run
`catalog.bat prune` to clean an existing database:

```bat
catalog.bat ingest --all
catalog.bat progress
```

Inspect the compatibility leaderboard (deduplicated by `(title_id, xmid)`):

```bat
catalog.bat list --games
```

Inspect one run with its 1-second timeline and libraries/imports:

```bat
catalog.bat show 4E4D0003 --timeline --libs
```

Package a playbook run (with paired logcat and checkpoint PNGs) into a `.hxpb`
container and verify it (use `--for-web` to write straight to
`tools/catalog/web/runs/<run_id>.hxpb` for `compatibility.php?download=<run_id>`):

```bat
catalog.bat pack build\logs\playruns\4E4D0003_idle-state\poll-01 --for-web
catalog.bat inspect tools\catalog\web\runs\<run_id>.hxpb
```

Compare two runs side-by-side (before and after an emulator change):

```bat
catalog.bat diff <run_a> <run_b>
```

Rank unimplemented kernel exports by how many catalogued titles import them:

```bat
catalog.bat missing-imports
```

Export the compatibility table as Markdown, JSON, or CSV:

```bat
catalog.bat export --format md --out build\catalog\compat.md
```

Preview the catalog locally in PHP (then open `http://127.0.0.1:8080/compatibility.php`):

```bat
php -S 127.0.0.1:8080 -t tools\catalog\web
```

By default `catalog.bat` writes `tools/catalog/web/hakuxp.db` right beside
`compatibility.php` (using single-file SQLite `DELETE` journal mode so there are no
`-wal`/`-shm` sidecars when uploading to a web host). `tools/catalog/web/.htaccess`
blocks direct HTTP downloads of `.db`, `.sqlite`, and `.hxpb` files. Set
`HAKUXP_CATALOG_DB` to override the database path.

## Scoring model (0-100, `score_v1`)

Runs are graded purely on a continuous **0-100** score rather than arbitrary
status labels (`broken`, `ingame`, `playable`, etc.):

- **0**: Untested (no runnable evidence).
- **1-99**: Graded from stability, frame pacing, visual health, and API completeness.
- **100**: Smooth full speed, zero stutters/hangs/drops, verified visuals, and every used function and library emulated cleanly.

Four transparent pillars sum to 100, with every deduction stored in
`runs.score_breakdown_json`:

| Pillar | Max | Signals used |
| --- | ---: | --- |
| Stability & Execution | 35 | Frontier boot stages, clean vs crash/fault/GPU-lost/hang ending, active hangs >= 1000 ms (display pause hangs separated out; 500-999 ms hitches count as severe stutters), frontier problems, error logs |
| Performance & Pacing | 30 | Average & p10 FPS vs target (console mode refresh / vsync pace: 60/30/20/15 fps NTSC, 50/25 fps PAL; uncapped `IMMEDIATE` titles target 60 fps NTSC / 50 fps PAL), severe stutters >= 80 ms per minute, minor stutters and dropped frames per 1,000 frames (scaled by speed ratio), input-to-screen latency (<= min(60, fps) ms good, up to 2x fps workable, > 2x steeper, >= 100 ms capped) |
| Visuals & Determinism | 15 | Pure-Python PNG probe (black, uniform, frozen vs changing frames) and playbook checkpoint match ratio (`same` vs `differs`) |
| API & Emulation Completeness | 20 | Unimplemented kernel imports declared by the XBE, runtime `not implemented` stub hits, zero-filled SCSI opcodes, APU trap/warning events, slow kernel calls |

Hard caps enforce honest grading:

- Boot failure: max **5**
- Zero rendered frames: max **15**
- All captured frames black: max **25**
- Host fault / crash / GPU lost: max **35**
- Frozen output across captures: max **40**
- Guest fault / unhandled exception / HLT: max **45**
- Severe slowdown (< 25% of target FPS: max **50**; < 50%: max **65**)
- Legacy headless sweep (90 s first boot, no audio, no display hitch monitor): max **80**
- Score **100** requires full verified telemetry (FPS/hitch log, API log, verified non-black changing frames) and zero deductions.

Per-game rollup scoring (`games.game_score`) additionally factors in:

- **Total & active test time** (`total_test_seconds`, `active_test_seconds`, `input_seconds`): short tests (< 1 min max 70, < 5 min max 82, < 15 min max 88, < 1 hr max 94) and idle-only sessions (no input or changing frames: max 75) are bounded below sustained active play.
- **Test device coverage** (`device_count`): 1 device max 92, 2 devices max 96, 3+ devices unlock 100.

Per-device rollup scoring (`devices.device_score`) grades how well a hardware model runs HakuXP:

- **Deterministic hardware hash (`device_id`)**: 16-character lowercase hex SHA-256 prefix of `hakuxp-device-v1|<model>|<codename>|<soc>|<ram_tier_gb>gb`. Android OTA builds, GPU drivers, and hardware serial numbers are intentionally excluded so driver/OS updates stay grouped under one `?device=<hash>` profile while distinct RAM SKUs (e.g. 8 GB vs 16 GB) stay separate.
- **Overall device score (0-100)**: mean of the best substantive run score per unique game tested on that device, paired with 4-pillar averages (`avg_stability`, `avg_performance`, `avg_visuals`, `avg_completeness`) and sample-depth caps (< 3 games max 75, < 10 games max 85, < 25 games max 92, 25+ games unlock 100; < 5 min max 75, < 30 min max 85).
- View all devices via `catalog.bat devices`, `tester-admin.exe devices`, `compatibility.php?tab=devices`, or a specific device via `compatibility.php?device=<hash>`.

Run `catalog.bat rescore` to recompute stored scores and device rollups after tuning weights.

### Target FPS, `none` vs `n/a`

Original Xbox display modes run at **60 Hz** (NTSC) or **50 Hz** (PAL), never
120 Hz. In push-buffer mode, HakuXP tracks the title's presentation pace in
`hw/xbox/hle/hitch.c` (`hle_hitch_pace`, logged as `hakuX-hle-hitch: pace: ...`)
separately from the live `FPS:` counter:

| Overlay `Target:` | Log (`hakuX-hle-hitch`) | Meaning |
| --- | --- | --- |
| `Target: --` | *(none yet)* | Too few frames seen (< 8) or display monitor not active yet. |
| `Target: 30 fps (60 Hz)` | `pace: 30 fps (60 Hz, 2 vblanks)` | Vsync-paced title with a valid Xbox D3D8 vblank divisor (`1..4`: 60/30/20/15 fps at 60 Hz; 50/25/17/12.5 fps at 50 Hz). |
| `Target: none (60 Hz)` | `pace: none (60 Hz)` | The developer explicitly set no framerate cap or target (`D3DPRESENT_INTERVAL_IMMEDIATE`, e.g. *Star Wars: Republic Commando*). Intentionally named `none` rather than `uncapped` so it is not confused with the emulator's own "Uncapped frame rate" setting. |
| `Target: n/a (60 Hz)` | `pace: n/a (60 Hz)` | No valid framerate target/cap is available or measurable due to a game/build issue or override -- for example missing/broken frame pacing (8+ stalled frames slower than 4 vblanks / < 15 fps with no vsync cadence) or the emulator's own "Uncapped frame rate" override (`HAKUX_HLE_UNCAP` / `debug.hakux.uncap`). |

Stalled frames slower than 4 vblanks (e.g. 10 fps = 6 vblanks) never train
`Target:` or `expected_ms`, preventing bogus low targets from masking stutters
or skewing catalog scores. For `none` and `n/a` titles, the catalog scoring
engine grades average and p10 FPS against the console's display refresh rate
(**60 fps** NTSC / **50 fps** PAL), never 120 fps.

## Canonical 1-second timeline (`timeline.jsonl` / `run_timeline`)

Every second `sec = 0, 1, 2, ...` of a run is stored as one row (in SQLite) and
one line of JSON (in `.hxpb`'s `timeline.jsonl`), including per-second
`ec_delta` (`ec_end[sec] - ec_end[sec-1]`) so mid-run EC stalls (`ec_halt`) and
rate drops (`ec_dip`, < 70% of median EC/s) are detected independently of
5-second FPS windows:

```json
{
  "sec": 12,
  "ec_start": "000040C1",
  "ec_end": "000051A2",
  "ec_delta": 60,
  "fps": 59.8,
  "frame_ms_avg": 16.72,
  "frame_ms_max": 33.4,
  "expected_ms": 16.7,
  "guest_res": "640x480",
  "inputs": 3,
  "stutters": 1,
  "severe_stutters": 0,
  "hangs": 0,
  "drops": 1,
  "kcalls_per_s": 1907,
  "sysops_per_s": 6453,
  "mmio_faults_per_s": 1854,
  "ram_free_kib": 52028,
  "pipe_compiles": 2,
  "not_impl_hits": 0,
  "apu_warns": 0,
  "errors": 0,
  "flags": ["display_miss"]
}
```

This schema is designed so both retroactive log extractors and a future live
1 Hz telemetry recorder (in the emulator core or Go companion app) emit the
exact same format.

## `.hxpb` container format (v1)

The `.hxpb` layout, its `manifest.json`, and how the Python and Go writers
differ are in [PACKAGES.md](PACKAGES.md#run-containers-hxpb), with
HakuXP's other package and file formats. In short: a ZIP whose
`manifest.json` carries `"format": "hxpb"`, `"version": 1` and the SHA-256
of every other member, which `catalog.bat inspect` and `ingest` check
before accepting it.

## SQLite schema (v1)

Defined in `tools/catalog/schema.py`:

- `schema_meta(key, value)` -- schema and scoring version metadata.
- `games(title_id, xmid, ...)` -- per-release rollups (`game_score`, `best_score`, `latest_score`, `total_test_seconds`, `active_test_seconds`, `device_count`, `device_list`, `xdk_version`, `run_count`, `latest_run_id`).
- `devices(device_id, ...)` -- per-device hardware profiles and overall scores (`device_score`, `mean_game_score`, 4-pillar averages, `games_tested`, `games_score_80_plus`, `run_count`, `total_test_seconds`, `os_list_json`, `gpu_drivers_json`).
- `runs(run_id, ...)` -- one row per ingested run across all lanes (`app`, `playbook`, `sweep`, `hxpb`), including `device_id`, run date (`started_at`), HakuXP build version (`app_version`, `git_commit`, `apk_sha256`), `xdk_version`, and EC stability metrics (`ec_rate_median`, `ec_rate_cv`, `ec_halt_seconds`, `ec_dip_seconds`, `active_seconds`, `input_seconds`, `input_count`).
- `run_timeline(run_id, sec, ...)` -- 1-second telemetry buckets including `ec_delta`.
- `run_libraries(id, run_id, kind, name, ordinal, status, call_count, detail)` -- linked XBE libraries (`kind = 'xbe_lib'` from `hakuX-native-xbelibs`, distinguishing `observed_active` vs `linked_only` and emulation state), imported/missing kernel exports (resolved against `native/kernel/kexports_table.h`), subsystem rollups, runtime `not_impl` hits, zero-filled `scsi_op` entries, and `apu_warn` records.
- `run_checkpoints(...)` -- checkpoint verdicts and PNG probe stats (`width`, `height`, `mean_luma`, `is_black`, `is_uniform`, `is_frozen_vs_prev`, `sha256`).
- `run_marks(...)` -- guide `Mark log` notes and screenshots.
- `run_events(...)` -- discrete timeline markers (stages, resolution changes, hangs, stops, problems, marks).
- Views: `v_game_summary` and `v_missing_imports_impact`.

## Module layout

- `tools/catalog/device.py` -- deterministic hardware `device_id` hashing, spec parsing, and overall device scoring.
- `tools/catalog/png_probe.py` -- pure-Python PNG decoder and black/uniform/frozen frame classifier.
- `tools/catalog/timeline.py` -- 1-second timeline accumulator and builder.
- `tools/catalog/extract.py` -- unified parsers for `debug.bat` logs, playbook runs, and `xboxrun` sweeps.
- `tools/catalog/scoring.py` -- 0-100 scoring engine and tier classifier.
- `tools/catalog/hxpb.py` -- `.hxpb` archive packer, verifier, and reader.
- `tools/catalog/schema.py` -- SQLite DDL, idempotent upsert, queries, diffing, and rescoring.
- `tools/catalog/cli.py` -- standalone CLI entry point (`catalog.bat`).
- `tools/catalog/web/index.php` -- landing page summarizing HakuXP with live catalog stats.
- `tools/catalog/web/compatibility.php` -- zero-dependency PHP SQLite viewer, 2,179-game gradient grid, and safe `?download=<run_id>` streamer for `runs/<run_id>.hxpb`.
- `tools/catalog/web/devices_view.php` -- `?tab=devices` leaderboard and `?device=<hash>` per-device profile renderer included by `compatibility.php`.
- `tools/catalog/web/.htaccess` -- blocks direct HTTP downloads of `.db`, `.sqlite`, and `.hxpb` files.

### PHP compatibility note (logged 2026-11-20)

Shared PHP hosts may run PHP 7.4. Keep all files under `tools/catalog/web/`
compatible with PHP 7.1+ and PHP 8.x: do not use PHP 8.0-only syntax or helpers
(`match (...)`, `str_contains`, `str_starts_with`, `str_ends_with`, `fn(...)`,
nullsafe `?->`, or union types). On 2026-11-20 a PHP 8.0 `match ($gridSort)`
caused HTTP 500 on live hosting until replaced with `switch` / `strpos`.
