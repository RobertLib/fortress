// Authored exterior terrain, landmarks and peaceful inhabitants.
#ifndef FORTRESS_WORLD_H
#define FORTRESS_WORLD_H
#include "common.h"
#include <stddef.h>

#define WORLD_PROPS 256
#define WORLD_MONKS 8
#define WORLD_LANDMARKS 16

typedef struct WorldCell { uint8_t outside, ground, authored; } WorldCell;
typedef struct WorldProp { int x, y; DecoKind kind; } WorldProp;
typedef struct Monk { int x, y, note; char name[40], text[224]; } Monk;
typedef struct Landmark { int x, y; float radius; char name[48]; } Landmark;
typedef struct WorldLayout {
  WorldCell *cells; // NULL on legacy maps; indexed exactly like the level grid
  WorldProp props[WORLD_PROPS];
  int prop_count;
  Monk monks[WORLD_MONKS];
  int monk_count;
  Landmark landmarks[WORLD_LANDMARKS];
  int landmark_count;
  char objective[96], destination[64];
} WorldLayout;

struct Level;
bool world_configure(struct Level *lv, const char *text, char *err, size_t size);
bool world_outside(const struct Level *lv, int x, int y);
GroundKind world_ground(const struct Level *lv, int x, int y);
bool world_reserved(const struct Level *lv, int x, int y);
const Monk *world_monk_note(const struct Level *lv, int note);
const Landmark *world_landmark(const struct Level *lv, float x, float y);
float world_prop_radius(DecoKind kind);
float world_prop_scale(DecoKind kind);
#endif
