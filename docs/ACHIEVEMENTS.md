# HakuXP achievement packs

Deferred roadmap, 2026-11-22. Dashboard bring-up takes priority. The goal is
one self-contained achievement pack per game release that anyone can author,
update, and share. Players should be able to earn achievements offline by
installing a pack, without waiting for a HakuXP release.

## One XMID for the whole game session

On launch, read the XMID from the disc's `default.xbe` and use it to find
the pack, for example `VU02101K.hxpacv`. Use that same XMID to look up the
game's patches and cheats. Keep it until the player exits the game or starts
another title. The filename identifies the disc release. Inside the manifest,
the XBE path and a hash of the unmodified executable identify the build whose
addresses were mapped.

| When | Proposed behavior |
| --- | --- |
| The disc launcher starts a second XBE | Carry the session XMID, achievement progress, and enabled patch and cheat selections across the core restart. Activate the new XBE's maps and rules. |
| A title update takes over | Select the map for the installed update. The disc's `default.xbe` and the update's `$u\default.xbe` can use different addresses. |
| The game loads an unrecognized XBE | Suspend memory watches and writes for that image. Keep the session and its progress, and resume matching rules when a known XBE loads. |
| The player returns to the dashboard or launches another game | End the previous game's session and release its mappings. Earned achievements stay in the player's profile. |

HakuXP already handles XBE relaunches and title update substitution. Its
existing mods use disc title ID and certificate version for lookup and can
target individual XBE paths. Preserve those installed mods when adding XMID
lookup. [Core boot and relaunch](../native/core/core.c) and
[existing mod and cheat formats](../mods/README.md).

## What goes in a .hxpacv

The proposed pack format (a ZIP with its own manifest and version, after
`.hxpb`'s approach: the release's XMID, per-XBE memory maps and watches,
achievements with stable IDs, progress kept outside the pack) is in
[PACKAGES.md](PACKAGES.md#achievement-packs-hxpacv-proposed), with
HakuXP's other package and file formats. File names and rule syntax are
still open.

Achievements observe guest state. Patches and cheats use the same session
and XBE selection, with writes applied at an emulator synchronization point.

## Research order

1. **Inspect Microsoft's official sets first.** Microsoft added achievements
   to the backward compatibility releases of Blinx, Conker, KOTOR, and
   KOTOR II. Start with the Xbox PC release of KOTOR. XCT's inspection reports
   a separate rule file with 45 rules, including a level-20 check and a
   Pazaak win event. Identify the source of the rules, strings, and icons,
   then check whether both triggers can be observed in the matching original
   Xbox executable. Record the XMID and XBE build used for each finding.
   [Xbox announcement](https://news.xbox.com/en-us/2026/11/12/25-original-xbox-games-now-available-on-pc/),
   [XCT's PC inspection](https://xct.live/news/how-xbox-added-achievements),
   and [Microsoft's achievement metadata](https://learn.microsoft.com/en-us/gaming/gdk/docs/reference/live/rest/json/json-achievementv2).
2. **Map XB.Live's achievement flow.** Trace one unlock in its public
   Simpsons Road Rage example from the JSON memory watch through the compiled
   `.ach` descriptor to the local unlock and server report. Document which
   parts a community pack can express and which require XB.Live registration.
   RetroAchievements has an original Xbox catalog with zero published sets
   at the time of this research. [XB.Live achievement SDK](https://github.com/OGXBL/AchievementsSDK)
   and [RetroAchievements Xbox catalog](https://retroachievements.org/system/22-xbox/games).
3. **Settle the pack format from those examples.** Check that a contributor
   can describe a memory threshold, a one-time event, and a counter that
   survives an XBE relaunch. Use that evidence to choose the rule syntax
   and document how to author a pack.

## Future Xbox Live providers

When Xbox Live work begins, offer a provider setting for **Insignia**,
**XB.Live**, and **OpenBox** (planned). HakuXP will translate guest API and
network calls into the operations each provider supports. Keep local unlocks
usable offline. Any online achievement reporting and provider-specific IDs
belong in the integration so pack authors can define a challenge once.

XB.Live's October 15, 2026 notice says achievement and playtime tracking
require its XB Online network, while leaderboard data continues to sync from
Insignia. Treat them as separate providers. [XB.Live service notice](https://xb.live/news/end-of-insignia-support)
and [XB Online account setup](https://xb.live/xb-online).

This roadmap is based on published research and a read of HakuXP's launch,
mod, and archive code. Extracting the official assets and verifying unlocks
in HakuXP remain future work.
