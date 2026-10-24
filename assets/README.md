# Fortress artwork

The painted artwork was generated for this project with the built-in
`image_gen` tool. Original PNGs are retained in [source/](source/); exact
generation and edit prompts are recorded in [prompts.json](prompts.json).
The runtime uses the committed BMP files and needs no image service or PNG
dependency. Every atlas is required. Missing or invalid art produces an
installation error; the previous procedural art is not retained as a fallback.

## Runtime manifest

All square atlas cells are 256 × 256. Sprite atlases contain real alpha.

| File | Grid / size | Contents in row order |
| --- | --- | --- |
| `title.bmp` | 640 × 400 | Fortress title and menu painting |
| `limestone.bmp` | 256 × 256 | Seamless masonry base |
| `guard.bmp`, `knight.bmp`, `captain.bmp` | 3 × 4 each | Stand, two strides, aim, fire, pain, three death stages, two melee stages, spare idle |
| `guard-walk.bmp`, `knight-walk.bmp`, `captain-walk.bmp` | 3 × 4 each | Stand and two strides in front, rear, left and right rows |
| `knight-melee.bmp` | 3 × 4 | Sword/shield knight: same combat pose order; replaces crossbow poses on campaign maps |
| `animals.bmp` | 4 × 4 | Original hound, rat and spider directions; four bat flight frames in the last row |
| `hound_walk.bmp`, `rat_walk.bmp`, `spider_walk.bmp` | 3 × 4 each | Three locomotion poses in front, back, left and right rows |
| `combat.bmp` | 4 × 4 | Hound, rat, spider and bats in recoil, falling and dead rows; original equipment row. Rat and equipment are superseded by the dedicated sheets below |
| `rat_death.bmp` | 3 × 1 | Dedicated clean rat injury, falling and dead poses |
| `equipment.bmp` | 4 × 1 | Crossbow, repeating crossbow, dagger and bolt; HUD icons, pickup and projectile source |
| `props.bmp` | 4 × 4 | Table, chair, wardrobe, chest; armor, statue, urn, fountain; candelabra, chains, barrel, powder barrel; charred debris, shield, sword, fireplace |
| `outdoors.bmp` | 2 × 4 | Oak, fir; well, headstone; stone cross, bench; altar, living monk |
| `gate-winch.bmp`, `drain-valve.bmp`, `chapel-bell.bmp` | 1 × 1 each | Interactable chapter mechanisms |
| `items.bmp` | 4 × 2 | Key, potion, bread, quiver; treasure, note, candles, pressure plate |
| `horror.bmp` | 3 × 2 | Jailer idle, two strides, attack, prisoner, shadow |
| `fixtures.bmp` | 3 × 2 | Nobleman, noblewoman, bishop; torch, pendulum axe, parchment |
| `weapons.bmp` | 3 × 3 | Dagger, crossbow and repeating crossbow rows; idle, fired and returning columns |
| `materials.bmp` | 4 × 4 | Door, banner, lever up/down; gate, portal, window, slit; timber, three masonry damage stages; brick, wood floor, flagstones, coffered ceiling |
| `effects.bmp` | 4 × 3 | Eight explosion stages followed by four masonry dust stages |
| `story.bmp` | 2 × 3, cells 640 × 360 | Betrayal, broken chains, stolen crossbow, final gate, escape, freedom |

## Preparation

The seven production-upgrade PNG sources are in [source/](source/).
Their exact built-in `image_gen` prompts are retained in
[production-prompts.md](production-prompts.md) (three mechanisms),
[production-character-prompts.json](production-character-prompts.json)
(three directional soldier sheets) and
[production-knight-prompt.md](production-knight-prompt.md) (sword/shield combat).
Arched glass windows and shutters are deterministic material details assembled
from the existing painted stone and timber; they require no additional atlas.

Normal builds use the committed BMPs. To reproduce all runtime atlases from
the retained PNGs, run:

```sh
./tools/repack_art.sh
cmake --build build
./build/visual_tests --screenshots
```

The offline packer requires SDL 3.4 for PNG decoding; the game uses BMP
decoding available in SDL 3.2. It locates transparent cell gutters, normalizes
each family to a consistent baseline and scale, resizes premultiplied coverage
to avoid matte fringes, and verifies exact BMP alpha round trips. Story cells
are cropped to 16:9; material cells remain square. Packed PNG previews go to
`build/*-atlas.png`. CMake copies only runtime BMPs next to each executable,
including after an artwork-only change.

The outdoor family was generated and then edited with the built-in tool to
give every sprite an isolated cell. Its final source is `source/outdoors.png`;
both exact prompts and the final grid are recorded in
[outdoors-prompts.json](outdoors-prompts.json). Grass, earth, cobbles, gravel,
leaf litter and the directional night panorama are deterministic materials
in `src/materials.c` and need no extra runtime files.

## Runtime details

[src/assets.c](../src/assets.c) assembles the complete sprite inventory and
owns cleanup of shared frames. [src/materials.c](../src/materials.c) derives
the themed surfaces, mipmaps, metal bars, water and traces. Box fronts rectify
the painted furniture artwork onto actual world geometry. Portrait animation
keeps the antique frames fixed; flame and portal movement stays local to the
painted effect. Interface ornaments are rasterized vectors in
[src/ui.c](../src/ui.c). First-person weapons are supersampled at 384 × 384.

The loader converts sprite pixels to the renderer's premultiplied alpha
format once. No network call or generation happens during play. See
[visual coverage and review](../docs/art-direction.md) for the complete audit
and generated contact sheets.
