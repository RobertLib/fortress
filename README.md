# Fortress

A retro first-person action game set in a medieval fortress — a software
raycasting engine written in C on top of SDL3, with no other dependencies.
It combines a complete painted art set with a custom vector font and synthesized sound. Torchlit stone corridors fade into darkness. Behind
the remaining men-at-arms, something older drags its chains through the fortress.

## Building

You need a C11 compiler, CMake 3.16+ and [SDL 3](https://libsdl.org).

```sh
# macOS (Homebrew)
brew install cmake sdl3

make run            # configure, build and start the game
make run LEVEL=3    # start straight on floor 3
make                # just build (into build/)
make release        # production build (into build/release/)
make clean          # remove build directories
```

`make release` builds an optimized CMake `Release` binary without debug
tools or test executables. Run it with `./build/release/fortress`; its
`levels/` and `assets/` folders are copied alongside it. The `--level` / `-l` arguments
and title-screen number-key shortcuts are compiled out. Development builds
from `make` and `make run` keep those shortcuts. The separate build folders
let you switch between development and production without cleaning.
Use `RELEASE_DIR=/path/to/output` to change the production build directory.

The Makefile is a thin wrapper around CMake, which you can also drive
directly:

```sh
cmake -B build
cmake --build build
./build/fortress
```

For a production build directly with CMake:

```sh
cmake -S . -B build/release -DCMAKE_BUILD_TYPE=Release -DFORTRESS_DEBUG_TOOLS=OFF -DBUILD_TESTING=OFF
cmake --build build/release --config Release
```

Run the regression checks with `ctest --test-dir build --output-on-failure`.

On other platforms install SDL 3 through your package manager (or build it
from source) and point CMake at it, e.g. `-DCMAKE_PREFIX_PATH=/path/to/SDL3`.

The build copies [levels/](levels/) and [assets/](assets/) next to the executable;
the game also looks for both folders in the directory it is started from.
Keep both folders with the game when distributing it. Artwork is required: missing or
invalid atlases show a clear installation error instead of loading mismatched fallback art.

### Visual presentation

The title and menus share a painted fortress, drifting motes, dark iron panels
and restrained brass details. The same visual language carries through the
difficulty selection, controls, chapter introduction, pause screen and HUD.
Selection stays lit while navigating with a keyboard, mouse or gamepad.

The classic 640 × 400 presentation is preserved. All world textures and sprite
frames are prepared at 256 × 256, with supersampled 384 × 384 first-person
weapon views. The complete set covers soldiers, four animal families, the
Jailer, furniture, wall ornaments, pickups, traps, damaged masonry, explosions,
water, and all five floor and ceiling themes. Animal locomotion has three
painted poses from each of four directions; injury and death use dedicated art.
Eight painted explosion stages replace the old geometric effects.

Cool distance fog, warm torchlight, material shading and depth-tested embers
connect the artwork to the rooms. The HUD, automap, notes and every menu/state
screen use the same slate, iron and aged-brass palette. The story uses six
painted cinematic scenes with readable captions and gentle camera movement.

Run `./build/visual_tests --screenshots` from the repository root to render the
complete presentation and contact sheets into `build/art-*.bmp`. The test
checks all 525 asset slots, premultiplied alpha, distinct animation frames,
material mips and asset ownership. See [the coverage and verification guide](docs/art-direction.md)
and [artwork sources and reproduction](assets/README.md).

## The Game

Fight through 5 floors of the fortress. On each floor, find **3 keys**
that unlock the **GATE** to the next floor. You have 3 lives; every
40,000 points earns an extra life. After dying you retry the floor with
a crossbow and a basic supply of bolts.

Choose **EASY**, **NORMAL** or **HARD** after selecting **NEW GAME**. Use
the arrow keys or mouse, then Enter or a click to start; Esc or **BACK**
returns to the menu. Normal is selected by default and keeps the original
balance. Later runs remember your last choice while the game stays open.

| Difficulty | Enemy health | Damage taken | Starting bolts | Bolt pickups |
|---|---:|---:|---:|---:|
| Easy | 75% | 50% | 24 | 150% |
| Normal | 100% | 100% | 16 | 100% |
| Hard | 125% | 150% | 12 | 75% |

Values round to the nearest whole point, with a minimum of one. Damage
scaling includes enemy attacks, traps, explosions and spider venom.
Bolt pickups include dropped quivers and the repeating crossbow's supply.
The choice lasts for the whole run, including retries and later floors;
it appears on the floor introduction and pause screen. Cancelling the
selection leaves any run available through **CONTINUE** intact.

### Story comics

Starting a new game plays a three-panel animated prologue: betrayal,
broken chains and a stolen crossbow. Clearing the final floor plays an
epilogue of the last gate opening and the escape into the night, followed
by the final statistics. Story panels and the title use painted artwork in the same visual style.

Panels advance automatically with slow camera movement, rain and drifting dust. **Enter**, **Space**,
the **right arrow** or a **click** advances a panel; **Esc** or clicking
**SKIP** skips the sequence. On a gamepad use the bottom face button to
advance and the right face button to skip. Playback pauses when the window
loses focus. Continuing, loading a save, retrying a floor and debug level
warps go directly to the game without replaying the prologue.

### Save / Load

Press **F5** during play to save, and **F9** to load the saved position.
The pause menu also offers **SAVE GAME** and **LOAD GAME**; **LOAD GAME**
is available from the main menu, including after restarting the game.
F9 also works after death or game over.

There is **one save slot**, replaced each time you save. It preserves the
current floor, position, health, weapons, ammunition, keys, difficulty,
score and lives, along with enemies, pickups, doors, levers, traps, barrels,
damaged and broken walls, arrows in flight and the explored map. Saving is available while alive on
an unfinished floor. Starting a new game leaves the saved slot intact.
**CONTINUE** resumes the run currently in memory; **LOAD GAME** restores
the last position written to disk.

Saves are written as `savegame.dat` in the per-user application data folder
returned by `SDL_GetPrefPath("Fortress", "Fortress")`, independently of the
working directory.

Failed saves keep the previous slot. Missing, damaged, unsupported or
map-incompatible saves display a message and leave the current run intact.
Hauntings, extinguished lights, read notes, pending encounters and the Jailer's
position and pursuit state are saved too. An event that already happened stays
finished after loading. The rebuilt campaign maps require a new run when moving
from an older build; older saves on unchanged custom maps remain supported.

### Controls

| Key | Action |
|---|---|
| `W A S D` / arrows | move & turn |
| `Alt` + arrows | strafe |
| mouse (click the view to capture it) | turn + shoot |
| `Ctrl` / `J` | shoot |
| `E` / `Space` | open doors, pull levers, use the gate, read/close notes |
| `1` / `2` / `3` | dagger, crossbow, repeating crossbow |
| `Shift` | run |
| `Tab` | map |
| `M` | sound on/off |
| `F` | fullscreen |
| `F5` / `F9` | save / load game |
| `Esc` | pause |

### Gamepad

SDL-compatible gamepads work alongside the keyboard and mouse. Connect one
before starting or plug it in while the game is running. Menus and the
controls screen show gamepad hints when you use the controller, with face
button labels matched to its layout.

| Xbox / PlayStation control | Action |
|---|---|
| Left stick | move forward/back and strafe; tilt gently to walk slowly |
| Right stick | turn |
| `RT` / `R2` | shoot |
| `A` / Cross (bottom face button) | use doors, levers and the gate; read/close notes; confirm menus |
| `B` / Circle (right face button) | back in menus; resume from pause |
| `LB` / `RB` or `L1` / `R1` | previous / next weapon (skips weapons you do not own) |
| Hold `LT` / `L2` or press and hold the left stick | run |
| `Y` / Triangle, View / Back or the touchpad button | toggle map |
| Start / Menu / Options | pause / resume |
| D-pad up/down or left stick | select menu items |

Use **SAVE GAME** and **LOAD GAME** in the pause menu to save and load
without a keyboard. Both sticks have a dead zone to prevent drift; holding
a menu direction scrolls through the options. Disconnecting the active
gamepad or switching away from the window pauses play. Reconnect and resume
with the controller, or continue with the keyboard and mouse. Release the
fire trigger after a pause or load before shooting again.

### Debug

Development builds (`FORTRESS_DEBUG_TOOLS=ON`, the default) offer three
ways to jump straight to a chosen level. These are unavailable in
`make release` builds:

- `make run LEVEL=3`
- start the game with `--level 3` (or `-l 3`)
- press the level's number key (`1`–`5`) on the title screen

### Enemies

- **Guard** (leather armor, kettle hat) — patrols, weak
- **Knight** (plate armor, great helm) — tough, hits hard
- **Captain** (red tabard with a plume) — fast and accurate
- **Bat swarm** — flies over everything and swoops in to bite
- **War hound** — fast melee runner; one placed near a soldier heels to
  him as its handler, barks him awake when it spots you, and trots back
  to his side when it loses your trail
- **Rat** — small, quick and frail; darts about in fits and starts, and
  one squeal sets the whole pack on you in a zigzag scurry. Rats slip
  between everyone's legs, but a closed door stops them
- **Spider** — venomous: its bite keeps eating at your health for a few
  seconds afterwards (a healing potion purges the poison). One prowls the
  flagstones in long, still pauses; another lurks under the ceiling, drops
  on you on its thread as you pass, and climbs back up when it loses you.
  Hunting, it scuttles in bursts. Doors stop spiders too

Enemies patrol, and once they spot you (or hear a crossbow shot) they
raise the alarm and give chase — they stop to shoot, pull a knife or
swing a fist at close quarters, and can open doors.

### The haunted fortress

Each floor has its own authored hauntings, two handwritten story fragments
and two refuges marked by **white candles**. Approach a note and press the use
button to read it; use again to fold it. Reading pauses movement, enemies,
projectiles, poison and the world clock. Notes gradually reveal the keeper's
oath and the reason for your betrayal.

Distant knocking, dragging iron, fading footsteps, breath and wind punctuate
long stretches of silence. These sounds have positions in the world and are
muffled by walls. Ambient hauntings wait during nearby combat and in refuges,
so real attack warnings remain audible. Higher floors have fewer soldiers and
more evidence of what has taken their place.

Taking a key tolls a distant bell, dims nearby flames and can leave an explored
door open on the journey back. Some torches stay weak afterwards; white candles
keep their steady light. Figures vanish and empty armour changes position when
unobserved. These authored scenes happen once per floor attempt, not every time
you cross a trigger.

The **Jailer** is a rare pursuer announced by chains and a heavy footfall,
with several seconds of warning before he can hunt. Each key permits one
encounter, separated by a quiet interval. He follows sight and the noise of
running, splashing, crossbow fire and explosions, searches your last known
position, opens ordinary doors and uses stairs. He avoids pits, solid obstacles
and white-candle refuges. A closed door buys time; losing his sight and hearing
or reaching the candles lets the pursuit end. His swing has a visible windup
and can be dodged.

He cannot be killed. A crossbow bolt or powder blast staggers him for 2.4
seconds; the dagger buys only 0.75 seconds. He gives no score or ammunition and
does not count toward the kill statistics. The white candles protect against
the Jailer; other enemies and traps can still hurt you there.

### Barrels

Gunpowder barrels carry a red label with a pale flame. One bolt sets them
off: the blast hurts nearby enemies **and you**, loses strength with distance,
and can ignite other powder barrels. Solid walls and closed wooden doors
stop the blast; bars let it through. Ordinary, unmarked wooden barrels absorb
arrows without exploding. A dagger does not ignite gunpowder.

Both kinds block movement; an exploded barrel leaves walkable debris.

### Breakable walls

Pale masonry with deep, branching cracks can be broken with **three bolts**
from either crossbow, repeated dagger strikes at close range, or a nearby
gunpowder explosion. The cracks widen with each hit; collapsing stone makes
noise, throws up dust and leaves rubble you can walk over. The opening lets
enemies, arrows, sound and sight through too. The bolt that breaks a wall
stops there; fire again to shoot through the breach.

Each floor has two of these walls, opening optional shortcuts. All three
keys remain reachable without breaking them. Their damage and destruction
survive saving/loading; retrying a floor rebuilds them. A wall still absorbs
the blast that destroys it, protecting anything behind it from that explosion.

### Pits

Open pits have a pale stone rim around a dark shaft. Stepping into the
opening makes you fall and costs one life on every difficulty; the floor
can then be retried as after any other death. Walk around them — each floor
has one, and all three keys and the gate remain reachable without crossing
a pit. Grounded enemies avoid the holes; bats and spiders on their silk
threads can pass above them. Sight, sound and arrows pass over the opening.
Explored pits appear as dark, rust-bordered squares on the map.

### Pendulums

Heavy crescent blades hang from the ceiling and sweep back and forth every
3.2 seconds, with a metallic creak and a rush of air. Wait for the blade to
swing aside, then cross beneath the pivot, or walk around its reach. Contact
costs **30 health on Normal**, **15 on Easy** and **45 on Hard**. A single
contact cannot deal damage every frame. Each floor has a pendulum; explored
ones appear on the map with a copper mark showing the swing direction.
Saving preserves the swing timing and the brief protection after a hit.

### Water

Shallow pools and flowing channels run through all five floors. Blue-green
ripples shimmer in torchlight, and streams carry their ripples downstream.
Stone edging follows the shoreline; adjoining water tiles form one continuous
surface. You can safely wade across: water reduces movement speed to **62%**
for you and grounded enemies. Flying creatures pass above it at full speed.
Entering, leaving and walking through water makes splashes; nearby water
burbles softly, muffled by walls. Sight and arrows pass freely over the surface.
Explored water appears blue on the map, with a mark indicating the current.

### Architecture, stairs and elevation

All five floors have new authored layouts built around winding passages,
blind corners and separate branches for the three keys. Raised chapels,
stair galleries, sunken cistern bays, an octagonal burial circuit and a stepped
throne nave give them distinct architecture. Barred openings reveal places
reached later by another route; return loops and two breakable shortcuts per
floor reconnect familiar spaces. Doors fill wall openings with solid jambs.

Floor and ceiling heights are local to the architecture. Low linking passages
frame taller chambers and upper galleries. Sparse torchlight, repositioned
hauntings, readable fragments, candle refuges and the Jailer preserve the
horror pacing. All three keys and the gate are reachable without secrets,
mirror portals or breaking walls. See [the plans and verification rules](docs/level-design.md).

All five floors have static height differences: raised passages, stair flights
and sunken rooms. Walk or run up and down the stone steps normally; the camera
eases between treads. Grounded enemies use the same stairs. Abrupt ledges too
high for a step block movement from either side. There are no lifts or jumping.

Crossbows automatically aim up or down at visible enemies and barrels, keeping
the original horizontal mouse controls. Floors and ceilings stop sight and
arrows, and ledges can hide the lower part of an enemy. Close-range attacks
cannot reach across a large height difference. Explored stairs appear as pale
tread marks on the map; higher areas have lighter floor shading.

## Levels

Levels are text files in [levels/](levels/) — one character = one tile,
so they are easy to edit or extend with your own:

```
1-6  walls (texture variants)  B  barred wall (see/shoot through)
8    cracked, breakable wall
.    floor                     D  wooden door
R    barred door               X  gate (needs 3 keys)
P    player start              K  key
H    healing potion (+25)      F  bread (+10)
A    quiver of bolts           T  treasure (score)
Q    gunpowder barrel          q  ordinary barrel (does not explode)
M    repeating crossbow        G/S/O  guard / knight / captain
V/v  bat swarm (flying/roosting)  C/c  war hound (roaming/staying put)
Y/y  rat (scurrying/lurking)   W/w  spider (prowling/lurking under the ceiling)
L    lever on a wall           Z  secret wall
^    arrow trap                !  open pit (lethal)
=    shallow pool              >/<  stream flowing east/west
}/{  stream flowing south/north
&    pendulum (east-west)      %  pendulum (north-south)
~    mirror portal (paired in reading order)
```

Barred walls and barred doors block movement while closed, but allow both the
player and enemies to see and shoot through them. `R` opens like a normal door.

Use `8` for breakable interior masonry. It stays bare so cracks remain visible;
automatic furniture, torches and trap slits never replace it. An `8` on the
outer edge becomes ordinary stone to keep the map enclosed.

A secret wall (`Z`) disguises itself with the texture of the surrounding
masonry and hides a stash of valuables. Pulling the nearest lever (`L`) with
`E`/`Space` makes it grind aside for good — found secrets count toward the
end-of-floor statistics and are worth 500 points each. The lever handle
appears on exactly one face of its wall cell, picked automatically so it
always looks out on floor the player can reach; the cell's other faces
blend into the surrounding masonry.

An arrow trap (`^`) is a pressure plate: the nearest wall in a straight line
becomes an arrow slit that looses a volley when the plate is stepped on.
Use `!` for an open pit; keep a safe route around it when designing a level.
Use `=` for shallow pools, `>` / `<` for eastward / westward channels, and
`}` / `{` for southward / northward channels. Join tiles to make larger pools
or bend a stream; automatic furniture stays on dry floor. Water is safe to
cross and does not push the player. Its animation follows the saved world clock.
Use `&` or `%` for a continuously swinging ceiling blade. Its pivot is at
the tile centre; the blade swings east-west or north-south respectively.
Keep its swing and approaches clear. Automatic furniture leaves space around it.
Mirror portals (`~`) pair up in reading order — step into the glass and its
twin draws you out on the far side.

Use repeated `@height x y width height elevation` headers to set rectangular
floor regions. Coordinates start at `(0, 0)` in the top-left corner; dimensions
must fit the map. Heights are in tile units, from `-4` to `4`, in increments of
`0.125`. Unspecified cells stay at zero, and later rectangles override earlier
ones. For example, on a 25×19 map this makes four steps leading to a raised wing:

```text
@height 4 0 21 19 0.125
@height 5 0 20 19 0.25
@height 6 0 19 19 0.375
@height 7 0 18 19 0.5
```

Apply elevations to doors and wall cells as well as open floor, so door leaves,
secret passages, traps and wall decorations match their surroundings. Keep
adjacent walkable treads no more than `0.125` apart, and leave stair approaches
clear of placed barrels. Automatic solid furniture is removed from stair
flights and their landings. Each cell has one floor and one ceiling, giving
Doom-style 2.5D geometry. Elevations are static throughout play.

`@ceiling x y width height elevation` sets an independent, absolute ceiling
elevation in increments of `0.125`, from `-5` to `5`. Later rectangles override
earlier ones. Every specified ceiling must be at least one unit above its floor.
Unspecified ceilings follow their floor plus one, preserving existing custom
maps. For example, `@ceiling 4 4 9 9 3` makes a tall hall over a nine-tile square.
Neighbouring ceilings may differ sharply, forming lintels and the lower roofs
of side aisles. Ceilings occlude sight, sprites and projectiles at their actual
elevations, and edits invalidate saves for that map.

The campaign source plans are in [tools/author_campaign.py](tools/author_campaign.py).
They specify every room, corridor, stair flight and object without randomness.
Run `python3 tools/author_campaign.py` to export all five text maps, or pass a
floor number to export only that floor. Direct edits to the text maps are also
possible; exporting again replaces them from the source plans.

Run `./build/architecture_tests` to check door frames, maze structure and routes.
It walks all six orders of collecting the three keys using the actual movement,
door controls and pickups, with secret walls and breaches closed. Combat and
trap damage are isolated from this traversal test and covered by their own
regression suites. It also checks that each breach opens a physical shortcut.

Wall variants: rough stone masonry, weathered mossy stone, a
timber-framed wall, a dungeon wall with a barred window, old brick,
and a wall with a hanging banner.

Torches, furniture, fountains, fireplaces, candelabra, wall hangings,
portraits, cobwebs, cracks and niches are placed automatically, in a
stable layout derived from the map.

Floors and ceilings use perspective-correct, world-anchored textures with
torchlight, key glow and distance fog. Distant detail is filtered to reduce
shimmer. Each floor has its own materials:

| Floor | Ground | Ceiling |
|---|---|---|
| Guardhouse | Worn stone flagstones | Plaster with heavy timber beams |
| The Cellars | Damp, moss-edged paving | Masonry with stone ribs |
| Barracks | Oak planks with iron nails | Boards and timber joists |
| The Vault | Slate tiles with metal inlay | Recessed stone coffers |
| Warlord's Keep | Decorative stone with diamond inlays | Carved timber panels with metal bosses |

Horror content is enabled explicitly in a map header; custom maps without
`@horror` retain the original lighting and enemy roster. Example directives
(all positions are integer tile coordinates on dry, interior floor):

```text
@horror 1
@haven 1 3 1.15
@note 3 3 Keep to the white candles.|A bolt buys time. Break his line of sight.
@scene cell 3 1 5 1 0
@scene shadow 11 7 13 7 0
@scene knock 21 13 23 13 0
@scene armor 3 13 1 15 1 3 15
@trace blood 5 1
@trace scratch 3 3
@trace chains 1 17
```

`@horror` selects a chapter from 1 to 5. `@haven x y radius` places white
candles, with a radius from 0.6 to 3 tiles; walls block their protection.
`@note x y text` places a readable fragment (up to 223 ASCII characters;
`|` starts a paragraph). Automatic furniture leaves these locations clear.

`@scene kind trigger_x trigger_y source_x source_y keys [dest_x dest_y]`
fires when the player comes within 1.65 tiles of the trigger with at least
`keys` keys. Kinds are `cell` (a motionless guard who vanishes after the first
key, when unobserved), `shadow`, `mirror` (a briefer apparition), `knock` and
`armor`. Armour requires destination coordinates and moves only while both
positions are out of view. Scene figures are apparitions, not combatants.
`@trace blood|scratch|chains x y` places a world-anchored floor mark. A map can
contain up to 16 scenes, 12 notes, 4 refuges and 24 traces. These directives
participate in save compatibility checks.

The `@name`, `@floor`, `@ceil` and `@start` headers set the floor name,
floor/ceiling colors and the initial view direction. `@surfaces` selects
the material theme: `guardhouse` (the default), `cellars`, `barracks`,
`vault` or `keep`. Colors tint the chosen materials. Lines starting
with `;` are comments. The number of levels is set by `LEVEL_COUNT`
in [src/common.h](src/common.h).

## Under the Hood

- **Raycasting** (grid DDA) into a 640×400 software framebuffer with
  textured walls, floors and ceilings, static height sectors, stone stair risers,
  side shading, sliding doors and fog — the world sinks
  into darkness with distance, as if lit by torches; SDL scales the frame
  to the window ([src/render.c](src/render.c))
- **Sprites** (enemies, items) as billboards clipped by per-pixel geometry depth
  in vertical levels, so ledges and low ceilings hide only the occluded parts;
  darkened with distance
- **Sounds** synthesized into sample buffers once at startup and mixed in
  the SDL audio callback — playback only starts a cheap mixer voice
  ([src/audio.c](src/audio.c))
- **Artwork** loaded from painted atlases, with material variants, vector detail and animation assembled using a small
  anti-aliased raster library ([src/assets.c](src/assets.c),
  [src/weapons.c](src/weapons.c), [src/gfx.c](src/gfx.c))
- **Font**: a procedural serif stroke font, rasterised per size on first
  use ([src/font.c](src/font.c))
- **AI**: patrol → spot → chase → stop & shoot, with pain staggers
  and death ([src/enemy.c](src/enemy.c))
- **Horror**: authored scenes, candle refuges, positional ambience and a
  persistent pursuer with grid pathfinding ([src/horror.c](src/horror.c))
- **Levels**: parsing and the automatic set dressing
  ([src/level.c](src/level.c))
