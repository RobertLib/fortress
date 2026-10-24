# Fortress visual coverage

The presentation uses cool limestone and slate shadows, worn oak, forged iron,
muted burgundy fabric and aged brass. Amber fire provides the warm accent.
Sprites use realistic painted materials and restrained lighting. Interface
ornaments remain vector artwork so small labels and controls stay legible.
The game retains its 640 × 400 raycasting presentation.

## Coverage

| Family | Implementation |
| --- | --- |
| Main menu, difficulty, controls, quit, options, bindings and slots | Painted fortress, shared slate panels, brass rules, stable selection and matching mouse bounds |
| Loading and installation error | Matching title, panels and readable status text |
| Chapters, pause, death, floor complete, game over, victory | Shared typography, cards, spacing, controls and contextual statistics |
| Story | Six painted cinematic scenes, camera movement, weather and caption panel |
| World | 256 px limestone, moss, timber, brick, windows, banners, arrow slits, doors, bars, gate, levers and three masonry damage stages |
| Portal | Painted antique mirror with local animated distortion inside its frame |
| Floors and ceilings | Five themed material pairs, each with nine mip levels |
| Outdoor ground and sky | Five local ground materials with mipmaps; directional night panorama, pitched roof meshes, arched windows, shutters, buttresses and depth-tested weather |
| Estate props | Painted oak, fir, roofed well, headstone, cross, bench, altar and peaceful monk; isolated alpha atlas |
| Soldiers | Guard, knight and captain; eleven combat poses plus twelve directional locomotion frames per family. Campaign knight has dedicated sword/shield combat art |
| Animals | Hound, rat and spider: stand and two strides from four sides, plus injury and death; four bat flight frames and dedicated falling frames |
| Horror | Four Jailer poses, prisoner, shadow, readable parchment, notes, candles and floor traces |
| Decorations | Furniture, fountain, armor, statue, urn, candles, chains, fireplace, sword, shield, portraits, webs and cracks |
| Portraits | Three painted sitters, ten gaze variants each, fixed frames |
| Boxes | Textured front, side and lid for chest and wardrobe; matching shaded faces |
| Fireplaces | Shallow stone surround, arched opening and solid reveals; recessed firebox with twelve animated frames and emissive flames |
| Items | Keys, potion, bread, quivers, treasure and repeating crossbow |
| Campaign | Three painted mechanism atlases; glints, objective markers, two-page journal, quest and relic feedback |
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

The artwork check visits all 614 image slots, verifies nonempty images and
premultiplied alpha, checks distinct locomotion and weapon frames, validates
mip sizes, renders all presentation states and checks shared-image cleanup.
The existing gameplay checks cover walls, water, pits, stairs, pendulums,
story, save/load, difficulty and gamepad controls against the new art.
`./build/world_tests --screenshots` adds views of the five outdoor districts,
their buildings, monks and dialogue, with separate roof/sky and terrain tests.
`./build/production_tests --screenshots`, with an isolated `FORTRESS_USER_DATA`
directory, adds options, bindings, slots, journal, large reading text and
interior/exterior campaign previews. It also reports forty-frame rendering
means at those ten viewpoints; these measure the CPU renderer, not end-to-end
display latency. New source PNGs and exact generation prompts are indexed in
[the asset manifest](../assets/README.md).

Contact sheets are written to `build/art-enemy-poses.bmp`,
`build/art-world-inventory.bmp`, `build/art-effects-and-weapons.bmp` and
`build/art-directions-and-details.bmp`. Individual screenshots cover every
menu state, the story, each chapter, two views on every floor, the map,
notes and HUD status variants. These are actual software-renderer outputs.
`build/art-fireplace-views.bmp` covers frontal, oblique and grazing views at
three animation phases. Checks cover all four wall orientations, raised floors,
shallow frame projection, an actual cavity behind the wall plane, stable geometry
during animation, a seamless loop, foreground wall and ledge occlusion, and the
absence of a duplicate camera-facing fireplace.

The native window should also be checked through main menu → new game →
difficulty → story → chapter → play → map → pause. Release artwork lives
alongside `build/release/fortress` in `assets/` and `levels/`.

Current verification results and practical limits are recorded in
[the production delivery record](production-upgrade.md). The native window
review complements the rendered screenshots and controlled traversal tests;
it does not replace player sessions for balancing encounters and pacing.
