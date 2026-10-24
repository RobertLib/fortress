# Fortress production development

The target is a coherent medieval horror action game: deliberate crossbow combat,
readable soldiers, discoveries with practical value, and an unstoppable pursuer
that can be outwitted. The existing five-chapter estate and painted artwork remain
the foundation. Features must survive save/load, work with a controller, and be
explained in the game.

## Delivery and verification

- [x] Authored chapter mechanisms, distinct objectives, journal and gate conditions.
- [x] Optional NPC errands with persistent, useful rewards and changed dialogue.
- [x] Hidden relics, cross-chapter progression and limited tactical supplies.
- [x] Decoys and temporary door barricades, with audible/visible feedback.
- [x] Final oath-breaking sequence and escape, preserving the unkillable Jailer.
- [x] Distinct soldier tactics, counterplay and clear attack warnings.
- [x] Deliberate weapon roles, quiet backstabs, consistent damage and impact feedback.
- [x] More expressive weapon animation and material-specific combat sound.
- [x] Authored outdoor composition, architecture details and chapter palettes.
- [x] Contextual onboarding, legible prompts, objective and map annotations.
- [x] Persistent options, keyboard rebinding, controller navigation and comfort controls.
- [x] Independent manual slots, autosave/checkpoints, story and arcade rules.
- [x] Regression coverage for progression, combat, corrupted saves and UI flows.
- [x] Controlled campaign traversal, release build, sanitizer checks and rendered/native QA.

New campaign rules are opt-in map data, so small custom maps and existing engine
regression fixtures remain useful. Required objectives must be reachable without
secrets or breakable walls; optional rewards must never be necessary to finish.
No new external runtime dependency is introduced.

## Chapter direction

1. **The Outer Ward**: reclaim the village gate winch; introduce noise, refuge,
   deliberate shots and a short winch operation under pressure. Return a grain
   tally to Matthias to receive the first useful supply reward.
2. **The Silent Abbey**: restore the drainage valves, then ring the abbey bell.
   Drainage changes the traversal surface; the bell creates a deliberate noise
   consequence. Return the stolen reliquary to Anselm.
3. **The Garden Ward**: defeat the commanding officer and take his seal to the
   garden gate mechanism. Recover the gardener's ledger for Pavel.
4. **Blackwood Graves**: follow an ordered set of burial inscriptions to release
   the keeper's seal. Returning a memorial to Tomas rewards careful exploration.
5. **The Last Bell**: break the two physical oath seals, sound the last bell and
   escape to the throne gate. Benedikt's optional errand provides preparation.

All chapter and quest state is explicit, versioned and saved. One-time rewards
cannot be farmed through repeated conversations or reloads. Reading and the
journal pause the world; menus do not advance enemies, timers or poison.

## Implemented combat and fieldcraft

Campaign soldiers have distinct front/rear/side walk sheets. The knight also
has a separate sword/shield combat sheet, including windup, charge, injury and
death. Guards retreat to crossbow distance. Captains flank and rally nearby
soldiers with a horn. Knights reduce frontal damage to one fifth while their
guard is raised, rotate toward a visible player at a bounded rate, commit to
a telegraphed charge, then recover with their shield lowered. Shield impacts
use the incoming bolt direction, even if the player has moved since firing.
Enemy arrows aim at the release position and can hit other enemies or barrels.

| Weapon / tool | Initial tuning and purpose |
| --- | --- |
| Dagger | 14 damage; fourfold rear damage against an unaware soldier; no gunshot alarm |
| Crossbow | 18 damage before range falloff; precise, economical shot |
| Repeating crossbow | 12 damage per bolt; three-shot burst; two quick shield impacts break guard |
| Knight | 0.8 s windup, 0.5 s committed charge, 0.75 s exposed recovery; one hit per charge |
| Decoy | Seven seconds of audible pulses; unseen pursuers investigate it; one active at a time |
| Door brace | Holds a closed ordinary wooden door for ten seconds; enemies cannot bypass its lock |
| Quiet relic | Running/splash noise radius 4.5 instead of 11 tiles for the Jailer |
| Precision relic | Wider acquisition tolerance for deliberate crossbow shots |
| Healing relic | Potions restore 35 instead of 25 health |
| Vigil relic | Bolts stagger the Jailer for 0.9 s longer |
| Ashen relic | Half venom damage |

Material impact sounds, short hit confirmation, blood/metal effects, delayed
reload clicks and recoil motion make a hit and its recovery readable. Weapon
motion and camera shake are independently adjustable. Existing animals,
barrels, water, stairs, traps, mirrors and breakable walls retain their roles.

## Persistence and integration

Save version 9 explicitly serializes campaign progress, interrupted operations,
relics, errands, supplies, brace/decoy timers, finale, soldier tactics, projectile
weapon source, noise radius and the current conversation page. A run identifier prevents death recovery from
loading an automatic slot belonging to a different run. Legacy versions 1–8
remain readable on compatible, unchanged maps. The expanded authored campaign
requires starting a new run when upgrading from the earlier maps.

Monks with errands present two pages: their original story and then the current
request or thanks. Use advances the page before ending the conversation; the
world remains paused on both pages. Focus loss and controller disconnection
enter the pause menu directly, including while the journal is open.

Fresh chapter retries discard that attempt's kill, treasure and secret counts
from the run's completion statistics. Earned Arcade score, lives and relics
remain; a restored Story checkpoint retains the counts from its saved attempt.

Three manual slots and two automatic slots are separate files. Autosaving waits
for adequate health and a safe situation; a dormant queued hunt can be saved in
a refuge. A safe last-bell interaction first saves the player's preparation.
Automatic slots are read-only in the slot browser. Options use staged bounded
parsing and atomic replacement, as do saved games. Failed loads preserve the
current game; failed writes preserve the old slot. UI and integration tests
use isolated user-data directories.

Map authors opt in with `@campaign 1`. Directives are:

```text
@mechanism x y winch|valve|bell|seal|grave|oath prerequisiteMask seconds NAME|completion hint
@drain x y width height prerequisiteMask
@errand x y monkIndex NAME~request~thanks
@erranddoor x y
@relic x y kind
@roof x y width height eave ridge axis slate
@facade x y normalX normalY glass|shutter|crest
```

Prerequisite masks only refer to earlier stations. Oath must be last and require
all earlier stations. Seal checks living captains; oath also requires three
keys. Errand doors must be existing concealed doors. `|` inside errand prose
starts a paragraph; `~` separates its three fields. Text is printable ASCII,
matching the current game font. Mechanism clearance is reserved during automatic
furnishing. Roofs are visual meshes over the existing one-floor/one-ceiling map;
there are no stacked rooms or changed collision floors.

## Review record — 2026-10-24

- Nineteen regression suites pass with development tools enabled and disabled.
- All nineteen also pass with AddressSanitizer and UndefinedBehaviorSanitizer.
  LeakSanitizer is unavailable on this macOS toolchain; leak detection is disabled
  for that run. Asset ownership and pointer cleanup have separate assertions.
- Thirty controlled full routes cover all six key orders on each of five maps,
  operate the required mechanisms, and reach the exit with normal movement and
  solid furniture/barrels. Combat damage is isolated in these traversal runs.
- Integration checks exercise all five real monk conversations and treasury
  rewards, interruption and save restoration, finale prerequisites and renewed
  pursuit, combat counterplay, tools, options, rebinding, journal, controller menu
  actions, slots, cross-run recovery rejection, malformed data and atomic writes.
- The artwork audit validates 614 image slots, alpha, distinct directional and
  weapon poses, mips and cleanup. Seven new generated PNG/BMP assets and exact
  prompts are indexed in [the artwork manifest](../assets/README.md).
- Offline audio checks exercise every synthesizer, five distinct two-stem scores,
  loop boundaries, actual mixing callback, gain controls, mute and refuge fades.
- Geometry checks cover pitched roof depth occlusion, both ridge directions and
  faces crossing the camera plane. Actual renderer outputs cover five districts,
  chapter views, mechanisms, notes at 150%, journal, options, bindings and slots.
- The native macOS release window was checked through menu, difficulty, prologue,
  chapter, journal, movement, firing, pause and saving; earlier UI review also
  covered options, text size, map and slot previews. This is a smoke review,
  not a complete manual playthrough of all encounters.
- The release contains all 28 required BMP files and five matching authored maps.
  Its complete artwork also loads from a working directory outside the project.
  Ten representative 640×400 views measured 3.18–6.11 ms per frame on this host
  (forty-frame means, debug tools disabled). These exclude simulation and display
  presentation. The production screenshot command below reproduces the measurement.

```sh
cmake --build build -j 6
ctest --test-dir build --output-on-failure
FORTRESS_USER_DATA="$PWD/build/test-user-data/production_systems" ./build/production_tests --screenshots
./build/world_tests --screenshots
FORTRESS_USER_DATA="$PWD/build/test-user-data/complete_artwork" ./build/visual_tests --screenshots
make release
./build/release/fortress
```

This delivery implements all ten proposed areas as a coherent playable build.
Combat pacing, scarcity and the final escape now need repeated sessions with
players unfamiliar with the maps. Automated completion and native smoke checks
establish functional coverage; they do not establish commercial readiness or
prove that difficulty and fun are already optimal.
