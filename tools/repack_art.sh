#!/bin/sh
# Rebuild the reviewed BMP atlases without changing the original generated PNGs.
set -eu
ART_ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
cd "$ART_ROOT"
mkdir -p build
cc -O2 $(pkg-config --cflags sdl3) tools/pack_art.c $(pkg-config --libs sdl3) -lm -o build/pack_art
./build/pack_art assets/source/title.png assets/title.bmp 1 1 title build/title-atlas.png
./build/pack_art assets/source/limestone.png assets/limestone.bmp 1 1 material build/limestone-atlas.png
for actor in guard knight captain; do
  ./build/pack_art "assets/source/$actor.png" "assets/$actor.bmp" 3 4 actor "build/$actor-atlas.png"
done
./build/pack_art assets/source/animals.png assets/animals.bmp 4 4 animals build/animals-atlas.png
./build/pack_art assets/source/props.png assets/props.bmp 4 4 props build/props-atlas.png
./build/pack_art assets/source/items.png assets/items.bmp 4 2 items build/items-atlas.png
./build/pack_art assets/source/horror.png assets/horror.bmp 3 2 props build/horror-atlas.png
./build/pack_art assets/source/fixtures.png assets/fixtures.bmp 3 2 fixtures build/fixtures-atlas.png
./build/pack_art assets/source/weapons.png assets/weapons.bmp 3 3 weapons build/weapons-atlas.png
./build/pack_art assets/source/story.png assets/story.bmp 2 3 story build/story-atlas.png
./build/pack_art assets/source/combat.png assets/combat.bmp 4 4 combat build/combat-atlas.png
./build/pack_art assets/source/materials.png assets/materials.bmp 4 4 material build/materials-atlas.png
./build/pack_art assets/source/effects.png assets/effects.bmp 4 3 effects build/effects-atlas.png
for animal in hound rat spider; do
  ./build/pack_art "assets/source/${animal}_walk.png" "assets/${animal}_walk.bmp" 3 4 "${animal}_walk" "build/${animal}-walk-atlas.png"
done
./build/pack_art assets/source/rat_death.png assets/rat_death.bmp 3 1 rat_death build/rat-death-atlas.png
./build/pack_art assets/source/equipment.png assets/equipment.bmp 4 1 props build/equipment-atlas.png
./build/pack_art assets/source/outdoors.png assets/outdoors.bmp 2 4 props build/outdoors-atlas.png
for mechanism in gate-winch drain-valve chapel-bell; do
  ./build/pack_art "assets/source/${mechanism}.png" "assets/${mechanism}.bmp" 1 1 props "build/${mechanism}.png"
done
for soldier in guard knight captain; do
  ./build/pack_art "assets/source/${soldier}-walk.png" "assets/${soldier}-walk.bmp" 3 4 soldier_walk "build/${soldier}-walk-atlas.png"
done
./build/pack_art assets/source/knight-melee.png assets/knight-melee.bmp 3 4 actor build/knight-melee-atlas.png
