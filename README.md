# Fortress

A retro first-person action game set in a medieval fortress — a raycasting
engine in pure JavaScript (canvas 2D), with no dependencies and no assets:
all graphics and sounds are generated procedurally at startup. Torchlit
stone corridors fade into darkness, defended by men-at-arms with crossbows.

## Running

Levels are loaded via `fetch`, so a local server is needed:

```sh
python3 -m http.server 8000
# then open http://localhost:8000
```

## The Game

Fight through 5 floors of the fortress. On each floor, find **3 keys**
that unlock the **GATE** to the next floor. You have 3 lives; every
40,000 points earns an extra life. After dying you retry the floor with
a crossbow and a basic supply of bolts.

### Controls

| Key | Action |
|---|---|
| `W A S D` / arrows | move & turn |
| mouse (click the view) | turn + shoot |
| `Ctrl` / `J` | shoot |
| `E` / `Space` | open doors, pull levers, use the gate |
| `1` / `2` / `3` | dagger, crossbow, repeating crossbow |
| `Shift` | run |
| `Tab` | map |
| `M` | sound on/off |
| `Esc` | pause |

### Enemies

- **Guard** (leather armor, kettle hat) — patrols, weak
- **Knight** (plate armor, great helm) — tough, hits hard
- **Captain** (red tabard with a plume) — fast and accurate

Enemies patrol, and once they spot you (or hear a crossbow shot) they
raise the alarm and give chase — they stop to shoot and can open doors.

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
M    repeating crossbow        G/S/O  guard / knight / captain
L    lever on a wall           Z  secret wall
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

Wall variants: rough stone masonry, weathered mossy stone, a
timber-framed wall, a dungeon wall with a barred window, old brick,
and a wall with a hanging banner.

The `@name`, `@floor`, `@ceil` and `@start` headers set the floor name,
floor/ceiling colors and the initial view direction. Lines starting
with `;` are comments. The number of levels is set by `LEVEL_COUNT`
in [script.js](script.js).

## Under the Hood

- **Raycasting** (grid DDA) with textured walls, side shading, sliding
  doors and fog — the world sinks into darkness with distance, as if
  lit by torches ([js/engine.js](js/engine.js))
- **Sprites** (enemies, items) as billboards clipped by the z-buffer,
  darkened with distance
- **Sounds** synthesized into `AudioBuffer`s once at startup and cached —
  playback only spawns a cheap `BufferSource` ([js/audio.js](js/audio.js))
- **Textures and pixel art** drawn into offscreen canvases at startup
  ([js/textures.js](js/textures.js))
- **AI**: patrol → spot → chase → stop & shoot, with pain staggers
  and death ([js/enemy.js](js/enemy.js))
