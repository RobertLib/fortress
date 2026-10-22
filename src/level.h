// Text-file levels: parsing, trap/lever/portal wiring and the automatic set
// dressing (torches, furniture, hangings, niches...).

#ifndef FORTRESS_LEVEL_H
#define FORTRESS_LEVEL_H

#include <stddef.h>

#include "common.h"
#include "gfx.h"

#define LEVER_MAX_DOORS 16

typedef struct Item {
  float x, y;
  ItemKind kind;
} Item;

typedef struct EnemySpawn {
  float x, y;
  EnemyType type;
  bool patrol;
  int master; // hounds: index of the handler in the spawn list, or -1
} EnemySpawn;

typedef struct BarrelSpawn {
  float x, y;
  bool powder; // Q = gunpowder, q = an ordinary wooden barrel
} BarrelSpawn;

typedef struct DoorSpawn {
  int x, y;
  char kind; // 'D' door, 'R' barred door, 'Z' secret wall
  char tex; // secret walls: the masonry ('1'..'6') they disguise themselves in
} DoorSpawn;

typedef struct TrapSpawn {
  int x, y; // the pressure plate
  float dir_x, dir_y; // flight direction of the arrows
  float origin_x, origin_y; // just in front of the arrow slit
} TrapSpawn;

typedef struct Lever {
  int x, y;
  int doors[LEVER_MAX_DOORS]; // cell indices of the secret walls it opens
  int door_count;
  char tex; // masonry worn by the faces without the handle
  int dx, dy; // the one face that carries the handle
} Lever;

typedef struct Portal {
  int x, y;
  int dx, dy; // the floor cell arrivals step out onto (0, 0 when walled in)
  int pair; // index of the twin mirror, or -1 for solid glass
} Portal;

typedef struct Torch {
  double x, y, phase;
} Torch;

typedef struct Decoration {
  double x, y, radius, phase;
  DecoKind kind;
} Decoration;

typedef struct WallBox {
  int x, y, dx, dy; // wall cell and the face it stands against
  BoxKind kind;
} WallBox;

typedef struct Hanging {
  int x, y, dx, dy;
  HangKind kind;
  uint32_t seed;
} Hanging;

typedef struct Level {
  char name[64];
  Color floor_color, ceil_color;
  int w, h;
  char *grid; // one character per cell, see level.c
  Item *items;
  int item_count;
  EnemySpawn *enemies;
  int enemy_count;
  BarrelSpawn *barrels;
  int barrel_count;
  DoorSpawn *doors;
  int door_count;
  TrapSpawn *traps;
  int trap_count;
  Lever *levers;
  int lever_count;
  Portal *portals;
  int portal_count;
  Torch *torches;
  int torch_count;
  Torch *fire_lights; // hearths light the walls and floor exactly like torches
  int fire_light_count;
  Decoration *decorations;
  int decoration_count;
  WallBox *wall_boxes;
  int wall_box_count;
  Hanging *hangings;
  int hanging_count;
  float start_x, start_y, start_dx, start_dy;
  int secret_count, key_count, treasure_count;
} Level;

// Parses a level from text. On failure returns false with a message in err.
bool level_parse(Level *lv, const char *text, char *err, size_t err_size);
void level_free(Level *lv);

// Solid masonry cells (including the see-through bars "B").
bool level_is_wall(char c);

#endif
