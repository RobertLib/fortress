# Fortress

A retro first-person action game set in a medieval fortress — a software
raycasting engine written in C on top of SDL3, with no other dependencies
and no asset files: all graphics, the font and the sounds are generated
procedurally at startup. Torchlit stone corridors fade into darkness,
defended by men-at-arms with crossbows.

## Building

You need a C11 compiler, CMake 3.16+ and [SDL 3](https://libsdl.org).

```sh
# macOS (Homebrew)
brew install cmake sdl3

make run            # configure, build and start the game
make run LEVEL=3    # start straight on floor 3
make                # just build (into build/)
make clean          # remove build/
```

The Makefile is a thin wrapper around CMake, which you can also drive
directly:

```sh
cmake -B build
cmake --build build
./build/fortress
```

Run the regression checks with `ctest --test-dir build --output-on-failure`.

On other platforms install SDL 3 through your package manager (or build it
from source) and point CMake at it, e.g. `-DCMAKE_PREFIX_PATH=/path/to/SDL3`.

The build copies the [levels/](levels/) folder next to the executable; the
game also looks for `levels/` in the directory it is started from.

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

### Save / Load

Press **F5** during play to save, and **F9** to load the saved position.
The pause menu also offers **SAVE GAME** and **LOAD GAME**; **LOAD GAME**
is available from the main menu, including after restarting the game.
F9 also works after death or game over.

There is **one save slot**, replaced each time you save. It preserves the
current floor, position, health, weapons, ammunition, keys, difficulty,
score and lives, along with enemies, pickups, doors, levers, traps, barrels,
arrows in flight and the explored map. Saving is available while alive on
an unfinished floor. Starting a new game leaves the saved slot intact.
**CONTINUE** resumes the run currently in memory; **LOAD GAME** restores
the last position written to disk.

Saves are written as `savegame.dat` in the per-user application data folder
returned by `SDL_GetPrefPath("Fortress", "Fortress")`, independently of the
working directory.

Failed saves keep the previous slot. Missing, damaged, unsupported or
map-incompatible saves display a message and leave the current run intact.

### Controls

| Key | Action |
|---|---|
| `W A S D` / arrows | move & turn |
| `Alt` + arrows | strafe |
| mouse (click the view to capture it) | turn + shoot |
| `Ctrl` / `J` | shoot |
| `E` / `Space` | open doors, pull levers, use the gate |
| `1` / `2` / `3` | dagger, crossbow, repeating crossbow |
| `Shift` | run |
| `Tab` | map |
| `M` | sound on/off |
| `F` | fullscreen |
| `F5` / `F9` | save / load game |
| `Esc` | pause |

### Debug

Three ways to jump straight to a chosen level:

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

### Barrels

Gunpowder barrels carry a red label with a pale flame. One bolt sets them
off: the blast hurts nearby enemies **and you**, loses strength with distance,
and can ignite other powder barrels. Solid walls and closed wooden doors
stop the blast; bars let it through. Ordinary, unmarked wooden barrels absorb
arrows without exploding. A dagger does not ignite gunpowder.

Both kinds block movement; an exploded barrel leaves walkable debris.

## Levels

Levels are text files in [levels/](levels/) — one character = one tile,
so they are easy to edit or extend with your own:

```
1-6  walls (texture variants)  B  barred wall (see/shoot through)
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
^    arrow trap                ~  mirror portal (paired in reading order)
```

Barred walls and barred doors block movement while closed, but allow both the
player and enemies to see and shoot through them. `R` opens like a normal door.

A secret wall (`Z`) disguises itself with the texture of the surrounding
masonry and hides a stash of valuables. Pulling the nearest lever (`L`) with
`E`/`Space` makes it grind aside for good — found secrets count toward the
end-of-floor statistics and are worth 500 points each. The lever handle
appears on exactly one face of its wall cell, picked automatically so it
always looks out on floor the player can reach; the cell's other faces
blend into the surrounding masonry.

An arrow trap (`^`) is a pressure plate: the nearest wall in a straight line
becomes an arrow slit that looses a volley when the plate is stepped on.
Mirror portals (`~`) pair up in reading order — step into the glass and its
twin draws you out on the far side.

Wall variants: rough stone masonry, weathered mossy stone, a
timber-framed wall, a dungeon wall with a barred window, old brick,
and a wall with a hanging banner.

Torches, furniture, fountains, fireplaces, candelabra, wall hangings,
portraits, cobwebs, cracks and niches are placed automatically, in a
stable layout derived from the map.

The `@name`, `@floor`, `@ceil` and `@start` headers set the floor name,
floor/ceiling colors and the initial view direction. Lines starting
with `;` are comments. The number of levels is set by `LEVEL_COUNT`
in [src/common.h](src/common.h).

## Under the Hood

- **Raycasting** (grid DDA) into a 640×400 software framebuffer with
  textured walls, side shading, sliding doors and fog — the world sinks
  into darkness with distance, as if lit by torches; SDL scales the frame
  to the window ([src/render.c](src/render.c))
- **Sprites** (enemies, items) as billboards clipped by the z-buffer,
  darkened with distance
- **Sounds** synthesized into sample buffers once at startup and mixed in
  the SDL audio callback — playback only starts a cheap mixer voice
  ([src/audio.c](src/audio.c))
- **Textures and pixel art** painted into images at startup with a small
  anti-aliased raster library ([src/assets.c](src/assets.c),
  [src/weapons.c](src/weapons.c), [src/gfx.c](src/gfx.c))
- **Font**: a procedural serif stroke font, rasterised per size on first
  use ([src/font.c](src/font.c))
- **AI**: patrol → spot → chase → stop & shoot, with pain staggers
  and death ([src/enemy.c](src/enemy.c))
- **Levels**: parsing and the automatic set dressing
  ([src/level.c](src/level.c))
