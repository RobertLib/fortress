// Shared game-wide enums, small math helpers and random numbers.

#ifndef FORTRESS_COMMON_H
#define FORTRESS_COMMON_H

#include <math.h>
#include <stdbool.h>
#include <stdint.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#define SCREEN_W 640
#define SCREEN_H 400
#define VIEW_H 320 // 3D viewport; the rest is the HUD bar

#define LEVEL_COUNT 5

typedef enum {
  SURFACE_GUARDHOUSE,
  SURFACE_CELLARS,
  SURFACE_BARRACKS,
  SURFACE_VAULT,
  SURFACE_KEEP,
  SURFACE_THEME_COUNT
} SurfaceTheme;

typedef enum {
  ENEMY_GUARD,
  ENEMY_KNIGHT,
  ENEMY_CAPTAIN,
  ENEMY_BAT,
  ENEMY_DOG,
  ENEMY_RAT,
  ENEMY_SPIDER,
  ENEMY_TYPE_COUNT
} EnemyType;

typedef enum {
  ITEM_KEY,
  ITEM_POTION,
  ITEM_BREAD,
  ITEM_BOLTS,
  ITEM_TREASURE,
  ITEM_ARBALEST,
  ITEM_BOLTS_DROP, // quiver dropped by a fallen enemy (smaller refill)
  ITEM_KIND_COUNT
} ItemKind;

typedef enum { WEAPON_DAGGER, WEAPON_CROSSBOW, WEAPON_ARBALEST, WEAPON_COUNT } WeaponKind;

typedef enum {
  DECO_FOUNTAIN,
  DECO_TABLE,
  DECO_CHAIR,
  DECO_WARDROBE,
  DECO_CHEST,
  DECO_FIREPLACE,
  DECO_ARMOR,
  DECO_CANDELABRA,
  DECO_CHAINS,
  DECO_STATUE,
  DECO_URN,
  DECO_CANDLES,
  DECO_KIND_COUNT
} DecoKind;

typedef enum { BOX_WARDROBE, BOX_CHEST, BOX_KIND_COUNT } BoxKind;

typedef enum {
  HANG_SWORD,
  HANG_SHIELD,
  HANG_CHAIN,
  HANG_PORTRAIT,
  HANG_COBWEB,
  HANG_CRACK,
  HANG_FIREPLACE,
  HANG_KIND_COUNT
} HangKind;

typedef enum { CREST_HEALTHY, CREST_OK, CREST_HURT, CREST_BAD, CREST_DEAD, CREST_COUNT } CrestState;

// ------------------------------------------------------------------ math

static inline float minf(float a, float b) { return a < b ? a : b; }
static inline float maxf(float a, float b) { return a > b ? a : b; }
static inline float clampf(float v, float lo, float hi) { return v < lo ? lo : v > hi ? hi : v; }
static inline int mini(int a, int b) { return a < b ? a : b; }
static inline int maxi(int a, int b) { return a > b ? a : b; }
static inline int clampi(int v, int lo, int hi) { return v < lo ? lo : v > hi ? hi : v; }
static inline float sq(float v) { return v * v; }
static inline float signf(float v) { return v > 0 ? 1.0f : v < 0 ? -1.0f : 0.0f; }

// round half up (toward +infinity), the way game rules round scores and damage
static inline double round_half_up(double v) { return floor(v + 0.5); }

// ------------------------------------------------------------------ random

// Deterministic LCG for the procedural art: the same seed always paints the
// same texture. Returns [0, 1).
typedef struct Lcg { uint32_t s; } Lcg;

static inline Lcg lcg(uint32_t seed) { Lcg r = {seed}; return r; }

static inline double lcg_next(Lcg *r) {
  r->s = r->s * 1664525u + 1013904223u;
  return r->s / 4294967296.0;
}

// General-purpose gameplay randomness, [0, 1).
void random_seed(uint64_t seed);
uint64_t random_state(void);
double frand(void);

#endif
