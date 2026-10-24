# Fortress visual coverage

The presentation uses cool limestone and slate shadows, worn oak, forged iron,
muted burgundy fabric and aged brass. Amber fire provides the warm accent.
Sprites use realistic painted materials and restrained lighting. Interface
ornaments remain vector artwork so small labels and controls stay legible.
The game retains its 640 × 400 raycasting presentation.

## Coverage

| Family | Implementation |
| --- | --- |
| Title, main menu, difficulty, controls, quit | Painted fortress, shared slate panels, brass rules, stable selection and matching mouse bounds |
| Loading and installation error | Matching title, panels and readable status text |
| Chapters, pause, death, floor complete, game over, victory | Shared typography, cards, spacing, controls and contextual statistics |
| Story | Six painted cinematic scenes, camera movement, weather and caption panel |
| World | 256 px limestone, moss, timber, brick, windows, banners, arrow slits, doors, bars, gate, levers and three masonry damage stages |
| Portal | Painted antique mirror with local animated distortion inside its frame |
| Floors and ceilings | Five themed material pairs, each with nine mip levels |
| Soldiers | Guard, knight and captain; eleven poses per family including firing, melee, injury and death |
| Animals | Hound, rat and spider: stand and two strides from four sides, plus injury and death; four bat flight frames and dedicated falling frames |
| Horror | Four Jailer poses, prisoner, shadow, readable parchment, notes, candles and floor traces |
| Decorations | Furniture, fountain, armor, statue, urn, candles, chains, fireplace, sword, shield, portraits, webs and cracks |
| Portraits | Three painted sitters, ten gaze variants each, fixed frames |
| Boxes | Textured front, side and lid for chest and wardrobe; matching shaded faces |
| Items | Keys, potion, bread, quivers, treasure and repeating crossbow |
| Weapons | Three painted first-person frames per weapon and separate equipment icons |
| Traps | Two pressure-plate states, directional bolts and 49 pendulum angle samples with solid silhouette sides |
| Effects | Eight painted explosion stages, six dust stages, rubble, flames, fountain motion, two water families and embers |
| HUD, automap, notes | Matching colors, panels and symbols; health, venom, lives, ammunition and three key indicators |

All runtime artwork is required. Startup reports a missing or invalid atlas;
it never silently substitutes the previous graphics. CMake refreshes BMP
assets even when only artwork changes.

## Review and checks

```sh
make
ctest --test-dir build --output-on-failure
./build/visual_tests --screenshots
./build/surface_tests --screenshots
make release
```

The artwork check visits all 524 image slots, verifies nonempty images and
premultiplied alpha, checks distinct locomotion and weapon frames, validates
mip sizes, renders all presentation states and checks shared-image cleanup.
The existing gameplay checks cover walls, water, pits, stairs, pendulums,
story, save/load, difficulty and gamepad controls against the new art.

Contact sheets are written to `build/art-enemy-poses.bmp`,
`build/art-world-inventory.bmp`, `build/art-effects-and-weapons.bmp` and
`build/art-directions-and-details.bmp`. Individual screenshots cover every
menu state, the story, each chapter, two views on every floor, the map,
notes and HUD status variants. These are actual software-renderer outputs.

The native window should also be checked through title → new game →
difficulty → story → chapter → play → map → pause. Release artwork lives
alongside `build/release/fortress` in `assets/` and `levels/`.

## Verified on 2026-10-23

- All 14 regression checks passed in the normal build and with AddressSanitizer
  and UndefinedBehaviorSanitizer enabled.
- All 525 artwork slots passed the alpha, animation, mip and ownership audit.
- Five representative floor views rendered in 2.08–2.70 ms on this machine.
- The production binary was opened in a native macOS window and checked through
  title, menu, difficulty, story, chapter, gameplay, firing, map and pause.
- All 20 distributed BMPs match the source files. The complete release artwork
  loads from another working directory. Missing and incorrectly sized atlases
  both fail with a named installation error.
