// Text-file levels: parsing, trap/lever/portal wiring and the automatic set
// dressing (torches, furniture, hangings, niches...).

#ifndef FORTRESS_LEVEL_H
#define FORTRESS_LEVEL_H

#include <stddef.h>

#include "common.h"
#include "gfx.h"
#include "horror.h"

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

// The blade swings in a vertical plane along (dx, dy), about a ceiling pivot.
typedef struct PendulumSpawn {
  int x, y, dx, dy;
} PendulumSpawn;

#define PENDULUM_PERIOD 3.2f
#define PENDULUM_MAX_ANGLE 1.1f
#define PENDULUM_PIVOT 0.98f
#define PENDULUM_LENGTH 0.55f
#define PENDULUM_BLADE_WIDTH 0.18f // half-width, tangent to the arc
#define PENDULUM_BLADE_HEIGHT 0.14f // half-height, along the shaft

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
  HorrorLayout horror;
  char name[64];
  Color floor_color, ceil_color;
  SurfaceTheme surface_theme;
  int w, h;
  char *grid; // one character per cell, see level.c
  float *heights; // static floor elevations
  float *ceilings; // absolute ceilings; NAN cells follow their floor + 1
  bool has_heights;
  bool has_ceilings;
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
  PendulumSpawn *pendulums;
  int pendulum_count;
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
  int secret_count, key_count, treasure_count, pit_count, water_count;
} Level;

// Parses a level from text. On failure returns false with a message in err.
bool level_parse(Level *lv, const char *text, char *err, size_t err_size);
void level_free(Level *lv);

// Solid masonry cells (including the see-through bars "B").
bool level_is_wall(char c);
// Empty space at eye/arrow height: floor, shallow water or an open pit.
bool level_is_open(char c);
bool level_is_water(char c);
// '=' is still water; the other water tiles carry a cardinal current.
void level_water_flow(char c, int *dx, int *dy);

#define LEVEL_STEP 0.125f
#define LEVEL_MAX_HEIGHT 4.0f
float level_floor_at(const Level *lv, int x, int y);
float level_floor(const Level *lv, float x, float y);
float level_ceiling_at(const Level *lv, int x, int y);
float level_ceiling(const Level *lv, float x, float y);

#define WATER_SPEED 0.62f // wading speed relative to dry ground
#define WATER_RIM 0.055f

// The stone lip around each pit; the opening starts inside this border.
#define PIT_RIM 0.06f

#endif
