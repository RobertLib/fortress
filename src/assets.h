// Painted wall materials, complete animated sprite families, weapon views and
// HUD emblems. Everything is prepared once at startup.

#ifndef FORTRESS_ASSETS_H
#define FORTRESS_ASSETS_H

#include "common.h"
#include "gfx.h"

#define TEX 256         // one consistent resolution for world art and sprite frames
#define WEAPON_VIEW 384 // supersampled POV artwork, reduced into the viewport
#define CREST_SIZE 48
#define PORTRAIT_COLS 5
#define PORTRAIT_ROWS 2 // row 0: level gaze, row 1: glancing down
#define PORTRAIT_SITTERS 3
#define EXPLOSION_FRAMES 8
#define WALL_DUST_FRAMES 6
#define WATER_FRAMES 16
#define FIREPLACE_FRAMES 12
#define SURFACE_MIPS 9 // 256x256 down to 1x1, to soften distant floor detail

typedef enum {
  WT_STONE,        // "1" rough masonry
  WT_MOSS,         // "2" weathered mossy stone
  WT_TIMBER,       // "3" timber-framed plaster
  WT_DUNGEON,      // "4" dungeon wall with a barred window
  WT_BRICK,        // "5" old brick
  WT_BANNER,       // "6" stone with a hanging banner
  WT_SLIT,         // "7" arrow slit (trap shooter)
  WT_BARS,         // "B" fixed iron bars (transparent)
  WT_DOOR,         // "D" oak door
  WT_BAR_DOOR,     // "R" barred door (transparent)
  WT_GATE,         // "X" the locked gate
  WT_LEVER,        // "L" lever, armed
  WT_LEVER_PULLED, // "l" lever, pulled
  WT_NICHE,        // "N" back wall of a niche
  WT_DUNGEON_BARE, // "4" without its window, for door and niche jambs
  WT_BREAKABLE,    // "8" cracked masonry, three stages of damage
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
  TRAP_ARROW_AWAY,   // tail-first
  TRAP_SPRITE_COUNT
} TrapSprite;

#define PENDULUM_FRAMES 49
#define PENDULUM_VIEW_WIDTH 1.6f

typedef struct BoxTextures {
  Image *front, *side, *front_dark, *side_dark;
  Image *lid; // only boxes lower than the eye have one
} BoxTextures;

typedef struct Assets {
  Image *warden[4], *haunt_shadow, *prisoner, *horror_note, *white_candles;
  Image *note_page;
  Image *horror_traces[3];
  Image *walls[WT_COUNT];
  Image *walls_dark[WT_COUNT]; // faces along the y axis
  Image *floors[SURFACE_THEME_COUNT][SURFACE_MIPS];
  Image *ceilings[SURFACE_THEME_COUNT][SURFACE_MIPS];
  EnemySprites enemies[ENEMY_TYPE_COUNT];
  Image *items[ITEM_KIND_COUNT]; // ITEM_BOLTS_DROP shares the quiver sprite
  Image *sparkle;
  Image *barrels[3]; // ordinary, gunpowder, charred debris
  Image *explosion[EXPLOSION_FRAMES];
  Image *wall_rubble, *wall_dust[WALL_DUST_FRAMES];
  Image *trap[TRAP_SPRITE_COUNT];
  Image *pendulum[PENDULUM_FRAMES]; // angle samples from left to right
  Image *weapons[WEAPON_COUNT][3]; // idle, just fired, recovering
  Image *weapon_icons[WEAPON_COUNT];
  Image *crests[CREST_COUNT];
  Image *torches[4];
  Image *fountains[4];
  Image *water[2][WATER_FRAMES]; // still ripples / flowing ripples
  // Decoration artwork; the fireplace cell is for the inventory, while
  // its wall attachment owns the solid geometry and animation.
  Image *decor[DECO_KIND_COUNT];
  Image *candle_flames[2][4]; // [0] candelabra, [1] niche candles; per frame
  Image *fireplaces[FIREPLACE_FRAMES]; // fixed stone surround, moving firebox
  BoxTextures boxes[BOX_KIND_COUNT];
  Image *hangings[HANG_KIND_COUNT]; // sword, shield, chain and fireplace
  Image *cobwebs[4];
  Image *cracks[4];
  Image *portraits[PORTRAIT_SITTERS][PORTRAIT_COLS * PORTRAIT_ROWS];
} Assets;

bool assets_build(Assets *a);
void assets_free(Assets *a);
const char *assets_error(void);

// weapons.c: the POV weapon for one frame (0 idle, 1 fired, 2 recovering),
// painted atlas artwork sampled into a WEAPON_VIEW x WEAPON_VIEW image.
Image *weapon_view_build(WeaponKind kind, int frame);

#endif
