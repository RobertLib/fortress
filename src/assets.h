// Procedural pixel art: wall textures, item/enemy sprites, weapon views and
// HUD emblems. Everything is painted once at startup.

#ifndef FORTRESS_ASSETS_H
#define FORTRESS_ASSETS_H

#include "common.h"
#include "gfx.h"

#define TEX 64 // texture and billboard size
#define WEAPON_VIEW 192 // POV weapon image size
#define CREST_SIZE 48
#define PORTRAIT_COLS 5
#define PORTRAIT_ROWS 2 // row 0: level gaze, row 1: glancing down
#define PORTRAIT_SITTERS 3
#define EXPLOSION_FRAMES 8
#define WALL_DUST_FRAMES 6
#define WATER_FRAMES 16

typedef enum {
  WT_STONE, // "1" rough masonry
  WT_MOSS, // "2" weathered mossy stone
  WT_TIMBER, // "3" timber-framed plaster
  WT_DUNGEON, // "4" dungeon wall with a barred window
  WT_BRICK, // "5" old brick
  WT_BANNER, // "6" stone with a hanging banner
  WT_SLIT, // "7" arrow slit (trap shooter)
  WT_BARS, // "B" fixed iron bars (transparent)
  WT_DOOR, // "D" oak door
  WT_BAR_DOOR, // "R" barred door (transparent)
  WT_GATE, // "X" the locked gate
  WT_LEVER, // "L" lever, armed
  WT_LEVER_PULLED, // "l" lever, pulled
  WT_NICHE, // "N" back wall of a niche
  WT_DUNGEON_BARE, // "4" without its window, for door and niche jambs
  WT_BREAKABLE, // "8" cracked masonry, three stages of damage
  WT_BREAKABLE_DAMAGED,
  WT_BREAKABLE_CRUMBLING,
  WT_PORTAL, // "~" mirror portal, four animation frames
  WT_PORTAL_LAST = WT_PORTAL + 3,
  WT_COUNT
} WallTex;

typedef enum {
  POSE_STAND,
  POSE_WALK1,
  POSE_WALK2,
  POSE_AIM,
  POSE_FIRE,
  POSE_PAIN,
  POSE_DIE1,
  POSE_DIE2,
  POSE_DEAD,
  POSE_STRIKE1, // footmen only: knife/fist windup
  POSE_STRIKE2, // footmen only: the lunge
  POSE_COUNT
} Pose;

typedef enum { VIEW_FRONT, VIEW_BACK, VIEW_LEFT, VIEW_RIGHT, VIEW_COUNT } ViewDir;

typedef struct EnemySprites {
  Image *pose[POSE_COUNT];
  // hounds, rats and spiders only: stand / walk1 / walk2 seen from four sides
  Image *view[3][VIEW_COUNT];
} EnemySprites;

typedef enum {
  TRAP_PLATE,
  TRAP_PRESSED,
  TRAP_ARROW, // side-on, flying right
  TRAP_ARROW_LEFT,
  TRAP_ARROW_TOWARD, // point-first, at the viewer
  TRAP_ARROW_AWAY, // tail-first
  TRAP_SPRITE_COUNT
} TrapSprite;

#define PENDULUM_FRAMES 49
#define PENDULUM_VIEW_WIDTH 1.6f

typedef struct BoxTextures {
  Image *front, *side, *front_dark, *side_dark;
  Image *lid; // only boxes lower than the eye have one
} BoxTextures;

typedef struct Assets {
  Image *walls[WT_COUNT];
  Image *walls_dark[WT_COUNT]; // faces along the y axis
  EnemySprites enemies[ENEMY_TYPE_COUNT];
  Image *items[ITEM_KIND_COUNT]; // ITEM_BOLTS_DROP shares the quiver sprite
  Image *sparkle;
  Image *barrels[3]; // ordinary, gunpowder, charred debris
  Image *explosion[EXPLOSION_FRAMES];
  Image *wall_rubble, *wall_dust[WALL_DUST_FRAMES];
  Image *trap[TRAP_SPRITE_COUNT];
  Image *pendulum[PENDULUM_FRAMES]; // angle samples from left to right
  Image *pendulum_edge;
  Image *weapons[WEAPON_COUNT][3]; // idle, just fired, recovering
  Image *weapon_icons[WEAPON_COUNT];
  Image *crests[CREST_COUNT];
  Image *torches[4];
  Image *fountains[4];
  Image *water[2][WATER_FRAMES]; // still ripples / flowing ripples
  // free-standing billboards: table, chair, armor, chains, statue, urn,
  // candelabra and candles (the other kinds have none)
  Image *decor[DECO_KIND_COUNT];
  Image *candle_flames[2][4]; // [0] candelabra, [1] niche candles; per frame
  Image *fire_flames[4];
  BoxTextures boxes[BOX_KIND_COUNT];
  Image *hangings[HANG_KIND_COUNT]; // sword, shield, chain and fireplace
  Image *cobwebs[4];
  Image *cracks[4];
  Image *portraits[PORTRAIT_SITTERS][PORTRAIT_COLS * PORTRAIT_ROWS];
} Assets;

void assets_build(Assets *a);

// weapons.c: the POV weapon for one frame (0 idle, 1 fired, 2 recovering),
// a small 3D model rendered into a WEAPON_VIEW x WEAPON_VIEW image.
Image *weapon_view_build(WeaponKind kind, int frame);

#endif
