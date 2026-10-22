// Procedural pixel art: wall textures, enemy and item sprites, decorations,
// HUD emblems. Everything is painted once at startup from seeded random
// numbers, so the same seed always paints the same texture. The POV weapon
// models live in weapons.c.

#include "assets.h"

#include <string.h>

#define COUNT(a) ((int)(sizeof(a) / sizeof((a)[0])))

#define STONE_BASE "#6b6157" // default masonry tone
#define DUNGEON_BASE "#565049" // the darker stone of the dungeon walls

// ------------------------------------------------------------------ painting

// A color with every channel scaled by f, floored and capped at 255. Kept in
// double precision so seeded shade factors always floor the same way.
static Color shade(const char *hex, double f) {
  Color c = color_parse(hex);
  c.r = (float)fmin(255.0, floor(c.r * f));
  c.g = (float)fmin(255.0, floor(c.g * f));
  c.b = (float)fmin(255.0, floor(c.b * f));
  return c;
}

static Color faded(const char *color, double alpha) { return color_alpha(color_parse(color), (float)alpha); }

// floor(rand * n): a random integer in [0, n)
static int rnd_int(Lcg *r, double n) { return (int)floor(lcg_next(r) * n); }

// Rectangle in texels.
static void rect_c(Image *g, double x, double y, double w, double h, Color c) {
  gfx_fill_rect(g, (float)x, (float)y, (float)w, (float)h, c);
}

static void rect(Image *g, double x, double y, double w, double h, const char *color) {
  rect_c(g, x, y, w, h, color_parse(color));
}

// Sprites are laid out on a logical 16x16 grid scaled x4 into the 64x64
// image; fractional cells blend into their neighbours.
static void px_c(Image *g, double x, double y, double w, double h, Color c) {
  rect_c(g, x * 4, y * 4, w * 4, h * 4, c);
}

static void px(Image *g, double x, double y, double w, double h, const char *color) {
  px_c(g, x, y, w, h, color_parse(color));
}

static void clear(Image *g, double x, double y, double w, double h) {
  gfx_clear_rect(g, (float)x, (float)y, (float)w, (float)h);
}

// Scattered one- or two-texel flecks of a color at low opacity.
static void speckle(Image *g, Lcg *r, const char *color, int count, double alpha) {
  Color c = faded(color, alpha);
  for (int i = 0; i < count; i++) {
    int x = rnd_int(r, TEX);
    int y = rnd_int(r, TEX);
    int w = 1 + rnd_int(r, 2);
    rect_c(g, x, y, w, 1, c);
  }
}

static Image *mirrored(Image *src) {
  Image *m = image_mirror(src);
  image_free(src);
  return m;
}

// ------------------------------------------------------------------ walls

// Rough masonry with irregular course heights and block widths.
static Image *stone_texture(uint32_t seed, const char *base, bool moss) {
  Image *c = image_new(TEX, TEX);
  Lcg r = lcg(seed);
  rect(c, 0, 0, TEX, TEX, "#2b2622"); // mortar
  int y = 0;
  while (y < TEX) {
    int rh = 10 + rnd_int(&r, 6);
    int x = -2 - rnd_int(&r, 8);
    while (x < TEX) {
      int bw = 12 + rnd_int(&r, 12);
      rect_c(c, x + 1, y + 1, bw - 2, rh - 2, shade(base, 0.75 + lcg_next(&r) * 0.4));
      rect(c, x + 1, y + 1, bw - 2, 2, "rgba(255,255,255,0.07)");
      rect(c, x + 1, y + rh - 2, bw - 2, 1, "rgba(0,0,0,0.2)");
      x += bw;
    }
    y += rh;
  }
  speckle(c, &r, "#000", 110, 0.16);
  speckle(c, &r, "#fff", 30, 0.05);
  if (moss) {
    for (int i = 0; i < 60; i++) {
      Color m = color_alpha(shade("#4a6a2a", 0.6 + lcg_next(&r) * 0.6), 0.55f);
      int mx = rnd_int(&r, TEX);
      int my = (int)floor(TEX * 0.25 + lcg_next(&r) * TEX * 0.75);
      int mw = 2 + rnd_int(&r, 5);
      int mh = 1 + rnd_int(&r, 3);
      rect_c(c, mx, my, mw, mh, m);
    }
  }
  return c;
}

static Image *breakable_texture(int damage) {
  Image *c = stone_texture(88, "#837463", false);
  // Wide, branching fissures distinguish weak masonry from decorative cracks.
  static const float cracks[][8] = {
    {27, 0, 34, 17, 25, 35, 32, 64},
    {34, 17, 48, 23, 43, 39, 64, 46},
    {25, 35, 12, 42, 16, 54, 0, 60},
    {0, 15, 13, 23, 18, 17, 34, 17},
    {43, 39, 37, 49, 49, 57, 46, 64},
    {25, 35, 19, 29, 9, 31, 0, 26},
    {48, 23, 52, 13, 45, 7, 50, 0},
  };
  int count = 3 + damage * 2;
  for (int k = 0; k < count; k++) {
    for (int j = 0; j < 3; j++) {
      const float *p = cracks[k] + j * 2;
      gfx_line(c, p[0] + 1, p[1], p[2] + 1, p[3], 3 + damage, color_parse("#ad9679"));
      gfx_line(c, p[0], p[1], p[2], p[3], 1.8f + damage, color_parse("#211b17"));
    }
  }
  if (damage) {
    Lcg r = lcg(891);
    for (int i = 0; i < damage * 10; i++) {
      double x = 17 + lcg_next(&r) * 28, y = 4 + lcg_next(&r) * 56;
      rect(c, x, y, 2 + damage, 2, "#302720");
      rect(c, x, y + 2, 2 + damage, 1, "#b49c7d");
    }
  }
  return c;
}

static void beam(Image *g, double x, double y, double w, double h) {
  rect(g, x, y, w, h, "#46331e");
  rect(g, x, y, w, 1, "rgba(255,255,255,0.08)");
  rect(g, x, y + h - 1, w, 1, "rgba(0,0,0,0.3)");
}

// Timber-framed wall: aged plaster panels between dark oak beams.
static Image *timber_texture(uint32_t seed) {
  Image *c = image_new(TEX, TEX);
  Lcg r = lcg(seed);
  rect(c, 0, 0, TEX, TEX, "#9c8f74"); // plaster
  for (int i = 0; i < 90; i++) {
    Color k = color_alpha(shade("#7a6d54", 0.7 + lcg_next(&r) * 0.5), 0.2f);
    int x = rnd_int(&r, TEX);
    int y = rnd_int(&r, TEX);
    int w = 2 + rnd_int(&r, 4);
    int h = 1 + rnd_int(&r, 2);
    rect_c(c, x, y, w, h, k);
  }
  beam(c, 0, 0, TEX, 6);
  beam(c, 0, 58, TEX, 6);
  beam(c, 0, 0, 6, TEX);
  beam(c, 58, 0, 6, TEX);
  // diagonal brace
  for (int i = 0; i <= 12; i++) beam(c, 4 + i * 4, 54 - i * 4, 6, 5);
  return c;
}

// Dungeon wall: dark stone with a barred window opening.
static Image *dungeon_texture(uint32_t seed) {
  static const int bars[] = {21, 28, 35, 42};
  Image *c = stone_texture(seed, DUNGEON_BASE, false);
  rect(c, 18, 12, 28, 26, "#0d0b08");
  rect(c, 16, 38, 32, 3, "#847a6c"); // sill
  rect(c, 16, 9, 32, 3, "#847a6c");
  for (int i = 0; i < COUNT(bars); i++) rect(c, bars[i], 12, 2, 26, "#3a3f45");
  for (int i = 0; i < COUNT(bars); i++) rect(c, bars[i], 12, 1, 26, "rgba(255,255,255,0.15)");
  return c;
}

// Old fired brick, thin courses.
static Image *brick_texture(const char *base, const char *mortar, uint32_t seed) {
  Image *c = image_new(TEX, TEX);
  Lcg r = lcg(seed);
  rect(c, 0, 0, TEX, TEX, mortar);
  const int bh = 6;
  for (int row = 0; row * bh < TEX; row++) {
    int off = row % 2 ? 10 : 0;
    for (int col = -1; col < 5; col++) {
      int x = col * 21 + off;
      rect_c(c, x + 1, row * bh + 1, 19, bh - 2, shade(base, 0.72 + lcg_next(&r) * 0.45));
    }
  }
  speckle(c, &r, "#000", 90, 0.15);
  speckle(c, &r, "#ffcf9a", 30, 0.06);
  return c;
}

// Stone with a hanging swallow-tailed banner, a flavor wall.
static Image *banner_texture(uint32_t seed) {
  Image *c = stone_texture(seed, STONE_BASE, false);
  rect(c, 16, 2, 32, 3, "#4a3018"); // rod
  rect(c, 22, 4, 20, 42, "#8c1a1a");
  // swallow tails
  rect(c, 22, 46, 7, 9, "#8c1a1a");
  rect(c, 35, 46, 7, 9, "#8c1a1a");
  rect(c, 22, 4, 20, 3, "#6d1010");
  rect(c, 22, 43, 20, 3, "#6d1010");
  // gold emblem: fleur
  rect(c, 30, 12, 4, 20, "#e9c93c");
  rect(c, 26, 18, 12, 4, "#e9c93c");
  rect(c, 28, 26, 8, 3, "#e9c93c");
  rect(c, 30, 12, 2, 3, "#fff6b0");
  rect(c, 22, 4, 2, 51, "#00000044");
  return c;
}

// Stone wall pierced by a cross-shaped arrow loophole: the trap shooter.
static Image *arrow_slit_texture(uint32_t seed) {
  Image *c = stone_texture(seed, STONE_BASE, false);
  // dressed-stone surround
  rect(c, 24, 6, 16, 52, "#78705f");
  rect(c, 24, 6, 16, 2, "rgba(255,255,255,0.08)");
  rect(c, 24, 56, 16, 2, "rgba(0,0,0,0.25)");
  // the loophole itself
  rect(c, 30, 10, 4, 44, "#070605");
  rect(c, 25, 28, 14, 5, "#070605");
  // worn edges catching the torchlight
  rect(c, 29, 10, 1, 44, "rgba(255,255,255,0.12)");
  rect(c, 34, 10, 1, 44, "rgba(255,255,255,0.12)");
  return c;
}

// Back wall of a niche: the renderer carves the cell's face back to half
// depth door-style, and this masonry lines the recess: the same stone in
// shadow, gloom pooling under the vault and in the corners the torchlight
// never reaches.
static Image *niche_texture(uint32_t seed) {
  Image *c = stone_texture(seed, "#57504a", false);
  rect(c, 0, 0, TEX, 8, "rgba(0,0,0,0.38)");
  rect(c, 0, 8, TEX, 6, "rgba(0,0,0,0.2)");
  rect(c, 0, 0, 6, TEX, "rgba(0,0,0,0.3)");
  rect(c, TEX - 6, 0, 6, TEX, "rgba(0,0,0,0.3)");
  rect(c, 6, 0, 5, TEX, "rgba(0,0,0,0.15)");
  rect(c, TEX - 11, 0, 5, TEX, "rgba(0,0,0,0.15)");
  rect(c, 0, TEX - 4, TEX, 4, "rgba(0,0,0,0.22)"); // the floor of the recess in shadow too
  return c;
}

// Wall-mounted lever on dressed stone: an iron backplate whose wooden
// handle points up while idle and hangs down once pulled.
static Image *lever_texture(bool pulled) {
  static const int rivets[][2] = {{27, 18}, {35, 18}, {27, 44}, {35, 44}};
  Image *c = stone_texture(111, STONE_BASE, false);
  // dressed-stone panel behind the mechanism
  rect(c, 20, 12, 24, 40, "#78705f");
  rect(c, 20, 12, 24, 2, "rgba(255,255,255,0.08)");
  rect(c, 20, 50, 24, 2, "rgba(0,0,0,0.25)");
  // iron backplate with corner rivets and the guide slot
  rect(c, 25, 16, 14, 32, "#2f3338");
  rect(c, 25, 16, 14, 2, "#454b52");
  for (int i = 0; i < COUNT(rivets); i++) rect(c, rivets[i][0], rivets[i][1], 2, 2, "#7c828e");
  rect(c, 30, 20, 4, 24, "#101214");
  // pivot boss in the middle of the slot
  rect(c, 29, 30, 6, 5, "#565c66");
  rect(c, 30, 31, 2, 2, "#8d949c");
  // the handle: wooden shaft with a brass knob, up = armed, down = pulled
  int dir = pulled ? 1 : -1;
  rect(c, 30, pulled ? 34 : 14, 4, 18, "#5a3a20");
  rect(c, 30, pulled ? 34 : 14, 1, 18, "#7a5230");
  rect(c, 28, 32 + dir * 19, 8, 6, "#c9a24a");
  rect(c, 29, 33 + dir * 19, 3, 2, "#e9c93c");
  return c;
}

// Heavy oak door bound with iron.
static Image *door_texture(void) {
  static const int bands[] = {9, 46};
  Image *c = image_new(TEX, TEX);
  Lcg r = lcg(777);
  rect(c, 0, 0, TEX, TEX, "#4a3018");
  for (int x = 2; x < 62; x += 10) {
    rect_c(c, x, 2, 9, 60, shade("#5a3c1e", 0.85 + lcg_next(&r) * 0.3));
    rect(c, x, 2, 1, 60, "rgba(0,0,0,0.4)");
    for (int i = 0; i < 4; i++) {
      int gx = x + 1 + rnd_int(&r, 7);
      int gy = rnd_int(&r, 58) + 3;
      double gh = 4 + lcg_next(&r) * 8;
      rect(c, gx, gy, 1, gh, "rgba(40,20,5,0.5)");
    }
  }
  // iron bands with rivets
  for (int b = 0; b < COUNT(bands); b++) {
    int y = bands[b];
    rect(c, 2, y, 60, 8, "#3a3f45");
    rect(c, 2, y, 60, 2, "rgba(255,255,255,0.12)");
    for (int x = 7; x < 60; x += 12) rect(c, x, y + 3, 2, 2, "#7c828e");
  }
  // ring handle
  rect(c, 41, 27, 10, 10, "#23262b");
  rect(c, 43, 29, 6, 6, "#0f1114");
  rect(c, 41, 27, 10, 2, "#7c828e");
  // frame shadow
  rect(c, 0, 0, 2, TEX, "rgba(0,0,0,0.45)");
  rect(c, 62, 0, 2, TEX, "rgba(0,0,0,0.45)");
  rect(c, 0, 0, TEX, 2, "rgba(0,0,0,0.45)");
  return c;
}

// Iron bars with a transparent opening. The renderer composites this surface
// over whatever the ray hits farther down the corridor.
static Image *bars_texture(bool door) {
  static const int uprights[] = {9, 20, 31, 42, 53};
  static const int rails[] = {19, 39};
  Image *c = image_new(TEX, TEX);

  // A dark outline gives every bar some weight, followed by a narrow highlight
  // on the torch-facing edge. Door bars have a complete moving iron frame;
  // fixed bars are seated in heavier stone-colored top and bottom rails.
  const char *frame = door ? "#25292d" : "#4f4a43";
  const char *iron = "#30353a";
  const char *edge = "rgba(190,200,205,0.32)";
  const char *shadow = "rgba(0,0,0,0.5)";

  rect(c, 0, 0, TEX, 6, shadow);
  rect(c, 0, TEX - 7, TEX, 7, shadow);
  if (door) {
    rect(c, 0, 0, 6, TEX, shadow);
    rect(c, TEX - 7, 0, 7, TEX, shadow);
  }
  rect(c, 0, 0, TEX, 4, frame);
  rect(c, 0, TEX - 5, TEX, 5, frame);
  if (door) {
    rect(c, 0, 0, 4, TEX, frame);
    rect(c, TEX - 5, 0, 5, TEX, frame);
  }

  for (int i = 0; i < COUNT(uprights); i++) {
    int x = uprights[i];
    rect(c, x - 1, 3, 5, 57, shadow);
    rect(c, x, 3, 3, 56, iron);
    rect(c, x, 4, 1, 54, edge);
    // pointed lower tips sell the portcullis silhouette
    rect(c, x + 1, 59, 1, 3, iron);
  }
  for (int i = 0; i < COUNT(rails); i++) {
    int y = rails[i];
    rect(c, 2, y - 1, 60, 5, shadow);
    rect(c, 2, y, 60, 3, iron);
    rect(c, 3, y, 58, 1, edge);
  }

  if (door) {
    // latch plate and ring handle
    rect(c, 47, 27, 8, 11, "#1c2023");
    rect(c, 49, 29, 5, 2, "#747b80");
    rect(c, 52, 31, 2, 5, "#747b80");
    rect(c, 49, 35, 5, 2, "#747b80");
  }
  return c;
}

// The locked gate: stone arch, half-raised portcullis, warm light beyond.
static Image *gate_texture(void) {
  static const int rails[] = {16, 30, 44};
  Image *c = stone_texture(99, "#5d564e", false);
  // dark passage with arched top
  rect(c, 10, 14, 44, 50, "#0c0a08");
  rect(c, 14, 10, 36, 4, "#0c0a08");
  rect(c, 18, 7, 28, 3, "#0c0a08");
  // warm glow deep inside
  rect(c, 14, 30, 36, 34, "rgba(200,110,30,0.22)");
  rect(c, 20, 42, 24, 22, "rgba(255,170,60,0.28)");
  rect(c, 26, 52, 12, 12, "rgba(255,220,120,0.3)");
  // portcullis
  for (int x = 13; x < 52; x += 8) rect(c, x, 7, 3, 49, "#2f3338");
  for (int i = 0; i < COUNT(rails); i++) rect(c, 11, rails[i], 42, 3, "#2f3338");
  for (int x = 13; x < 52; x += 8) rect(c, x, 7, 1, 49, "rgba(255,255,255,0.14)");
  // spiked tips
  for (int x = 13; x < 52; x += 8) rect(c, x + 1, 56, 1, 3, "#494f56");
  // gold keystone
  rect(c, 29, 1, 6, 5, "#c9a24a");
  rect(c, 30, 2, 4, 3, "#e9c93c");
  return c;
}

// Mirror portal: a gilded frame set into the masonry around a glass of
// swirling violet light. Two spiral arms sit 180 degrees apart and advance 45
// degrees per frame, so the four frames loop seamlessly. Opaque edge to edge
// like every wall texture: the renderer fogs the whole column.
static Image *portal_texture(int frame) {
  static const int bosses[][2] = {{7, 5}, {53, 5}, {7, 55}, {53, 55}};
  static const char *const shades[] = {"#2a1650", "#45247e", "#6a3fae", "#9a6ad2", "#c9a8ee"};
  Image *c = stone_texture(88, "#5d564e", false);

  // gilded frame: lit along the top and left, in shadow at the base
  rect(c, 6, 4, 52, 56, "#7a5c22");
  rect(c, 6, 4, 52, 3, "#c9a24a");
  rect(c, 6, 4, 3, 56, "#c9a24a");
  rect(c, 6, 57, 52, 3, "#4a3812");
  rect(c, 55, 4, 3, 56, "#4a3812");
  for (int i = 0; i < COUNT(bosses); i++) rect(c, bosses[i][0], bosses[i][1], 4, 4, "#e9c93c"); // corner bosses

  // the glass: a near-black void behind the swirl
  rect(c, 11, 9, 42, 46, "#0b0714");

  // spiral arms wind inward, brightening toward the heart
  double rot = frame * M_PI / 4;
  for (int arm = 0; arm < 2; arm++) {
    for (double r = 19; r > 2; r -= 0.7) {
      double a = rot + arm * M_PI + (19 - r) * 0.3;
      int x = (int)floor(32 + cos(a) * r);
      int y = (int)floor(32 + sin(a) * r * 1.1);
      if (x < 12 || x > 50 || y < 10 || y > 52) continue;
      rect(c, x, y, 2, 2, shades[mini(COUNT(shades) - 1, (int)floor((19 - r) / 4))]);
    }
  }
  // hot core with a pale heart
  rect(c, 30, 30, 4, 4, "#e8dcff");
  rect(c, 31, 31, 2, 2, "#ffffff");
  // stray motes adrift in the glass, twinkling from frame to frame
  Lcg rng = lcg(880 + frame * 7);
  for (int i = 0; i < 10; i++) {
    int x = 12 + rnd_int(&rng, 39);
    int y = 10 + rnd_int(&rng, 43);
    rect(c, x, y, 1, 1, "#9a6ad2");
  }
  return c;
}

// ------------------------------------------------------------------ soldiers

typedef enum { HELM_KETTLE, HELM_GREAT, HELM_PLUME } HelmStyle;

typedef struct SoldierPalette {
  const char *uniform, *dark, *trim, *skin, *helmet;
  HelmStyle style;
  bool knife; // close-quarters weapon in the strike poses: knife, else fist
} SoldierPalette;

static const SoldierPalette PAL_GUARD = {"#6e5638", "#463621", "#a3823f", "#dba377", "#8d949c", HELM_KETTLE, false};
static const SoldierPalette PAL_KNIGHT = {"#8d959f", "#565e66", "#c9a24a", "#dba377", "#9aa2ac", HELM_GREAT, true};
static const SoldierPalette PAL_CAPTAIN = {"#8c2020", "#5a1414", "#c9a24a", "#dba377", "#8d949c", HELM_PLUME, true};

// Medieval man-at-arms billboard, one per pose. The helmet style tells the
// ranks apart: kettle hat (guard), great helm (knight), plumed (captain).
static Image *soldier_sprite(const SoldierPalette *pal, Pose pose) {
  Image *c = image_new(TEX, TEX);
  bool great = pal->style == HELM_GREAT;
  const char *hand = great ? pal->helmet : pal->skin; // gauntlets or bare hands

  if (pose == POSE_DEAD || pose == POSE_DIE2) {
    // lying on the floor
    double y = pose == POSE_DIE2 ? 11 : 13;
    px(c, 2, y, 12, 2, pal->uniform);
    px(c, 1, y, 2, 2, great ? pal->helmet : pal->skin); // head
    px(c, 1, y - 1, 2, 1, pal->helmet);
    px(c, 12, y + 1, 3, 1, pal->dark);
    px(c, 4, y + 1, 6, 1, "#7c1616"); // blood
    if (pose == POSE_DEAD) px(c, 3, 15, 9, 1, "#5c0f0f");
    return c;
  }
  if (pose == POSE_DIE1) {
    // collapsing to knees
    if (great) {
      px(c, 6, 3, 4, 4, pal->helmet);
    } else {
      px(c, 6, 4, 4, 3, pal->skin);
      px(c, 6, 3, 4, 2, pal->helmet);
    }
    px(c, 5, 7, 6, 5, pal->uniform);
    px(c, 4, 8, 1, 3, pal->uniform);
    px(c, 11, 8, 1, 3, pal->uniform);
    px(c, 5, 12, 2, 3, pal->dark);
    px(c, 9, 12, 2, 3, pal->dark);
    px(c, 6, 8, 4, 2, "#7c1616");
    return c;
  }

  double bx = pose == POSE_PAIN ? 1 : 0; // body x offset: pain leans aside

  // legs
  if (pose == POSE_WALK1) {
    px(c, 5, 11, 2, 4, pal->dark);
    px(c, 9, 12, 2, 3, pal->dark);
    px(c, 4, 15, 3, 1, "#221a12");
    px(c, 9, 15, 3, 1, "#221a12");
  } else if (pose == POSE_WALK2) {
    px(c, 5, 12, 2, 3, pal->dark);
    px(c, 9, 11, 2, 4, pal->dark);
    px(c, 4, 15, 3, 1, "#221a12");
    px(c, 9, 15, 3, 1, "#221a12");
  } else {
    px(c, 5 + bx, 11, 2, 4, pal->dark);
    px(c, 9 + bx, 11, 2, 4, pal->dark);
    px(c, 4 + bx, 15, 3, 1, "#221a12");
    px(c, 9 + bx, 15, 3, 1, "#221a12");
  }

  // torso (tabard / jerkin / breastplate)
  px(c, 5 + bx, 6, 6, 5, pal->uniform);
  px(c, 6 + bx, 6, 4, 1, pal->trim); // collar
  px(c, 7 + bx, 8, 2, 2, pal->trim); // belt buckle / emblem
  if (great) {
    // pauldrons
    px(c, 4 + bx, 6, 2, 2, pal->helmet);
    px(c, 10 + bx, 6, 2, 2, pal->helmet);
  }

  // head
  if (great) {
    px(c, 6 + bx, 1, 4, 4, pal->helmet);
    px(c, 5 + bx, 2, 1, 3, pal->helmet);
    px(c, 10 + bx, 2, 1, 3, pal->helmet);
    px(c, 6 + bx, 3, 4, 1, "#15181c"); // visor slit
    px(c, 6 + bx, 1, 4, 1, "#c3c9d1");
  } else {
    px(c, 6 + bx, 2, 4, 3, pose == POSE_PAIN ? "#d98a6a" : pal->skin);
    px(c, 7 + bx, 3, 1, 1, "#1a1a1a"); // eyes
    px(c, 9 + bx, 3, 1, 1, "#1a1a1a");
    if (pal->style == HELM_KETTLE) {
      px(c, 6 + bx, 0, 4, 2, pal->helmet);
      px_c(c, 5 + bx, 1.5, 6, 1, shade(pal->helmet, 0.75)); // brim
    } else {
      // plume
      px(c, 6 + bx, 1, 4, 2, pal->helmet);
      px(c, 5 + bx, 2, 1, 1, pal->helmet);
      px(c, 10 + bx, 2, 1, 1, pal->helmet);
      px(c, 10 + bx, 0, 1, 2, "#c03030");
      px(c, 11 + bx, 1, 1, 2, "#8c1a1a");
    }
  }

  // arms + crossbow
  if (pose == POSE_AIM || pose == POSE_FIRE) {
    // both arms forward, crossbow levelled at the viewer
    px(c, 4, 7, 2, 2, pal->uniform);
    px(c, 10, 7, 2, 2, pal->uniform);
    px(c, 5, 8, 6, 1, hand);
    px(c, 4, 7, 8, 1, "#4a3018"); // bow limbs
    px(c, 3, 7, 1, 1, "#33220f");
    px(c, 12, 7, 1, 1, "#33220f");
    px(c, 7, 7.5, 2, 2, "#2f2013"); // stock head-on
    if (pose == POSE_FIRE) {
      px(c, 7, 5.5, 2, 1.5, "#fff6b0"); // loosed bolt
      px(c, 7.5, 4, 1, 1.5, "#ffd23c");
    }
  } else if (pose == POSE_STRIKE1 || pose == POSE_STRIKE2) {
    // left arm stays down, guarding
    px(c, 4, 6, 1, 4, pal->uniform);
    px(c, 4, 10, 1, 1, hand);
    if (pose == POSE_STRIKE1) {
      // right arm cocked high beside the helmet
      px(c, 11, 4, 1, 3, pal->uniform);
      px(c, 11, 3, 1, 1, hand);
      if (pal->knife) {
        px(c, 11, 1, 1, 2, "#c3c9d1"); // blade raised skyward
        px(c, 11, 0.5, 1, 0.5, "#eef2f6"); // point catching the light
      } else {
        px(c, 10.5, 2, 2, 1.5, hand); // clenched fist
      }
    } else {
      // lunging straight at the viewer
      px(c, 10, 6, 2, 2, pal->uniform); // shoulder driving forward
      if (pal->knife) {
        px(c, 8.5, 6.5, 2, 2, hand); // knife hand looming close
        px(c, 6.5, 5.5, 2.5, 2, "#dfe4ea"); // blade flashing head-on
        px(c, 7, 6, 1, 1, "#ffffff"); // glint
      } else {
        px(c, 7.5, 5.5, 3, 3, hand); // fist filling the view
        px_c(c, 8, 6, 1.5, 1.5, shade(hand, 0.85)); // knuckles
      }
    }
  } else {
    px(c, 4 + bx, 6, 1, 4, pal->uniform);
    px(c, 11 + bx, 6, 1, 4, pal->uniform);
    px(c, 4 + bx, 10, 1, 1, hand);
    px(c, 11 + bx, 10, 1, 1, hand);
    // crossbow carried at the side
    px(c, 3 + bx, 9, 4, 1, "#4a3018");
    px(c, 2 + bx, 8, 1, 3, "#7c828e");
  }
  return c;
}

// ------------------------------------------------------------------ bats

// A swarm of bats drawn into a single billboard. One little bat is a body
// with red eyes and a pair of membrane wings caught either mid-upstroke or
// mid-downstroke; the swarm poses scatter several of them across the tile.
static void mini_bat(Image *g, double x, double y, int flap) {
  const char *wing = "#4c3a28";
  const char *wing_tip = "#33271a";
  const char *body = "#241a10";
  if (flap == 0) {
    // wings raised
    px(g, x - 2.1, y - 1.1, 1.8, 0.7, wing);
    px(g, x + 0.8, y - 1.1, 1.8, 0.7, wing);
    px(g, x - 2.8, y - 1.9, 1, 0.9, wing_tip);
    px(g, x + 2.3, y - 1.9, 1, 0.9, wing_tip);
  } else {
    // wings swept down and out
    px(g, x - 2.6, y + 0.1, 2.2, 0.7, wing);
    px(g, x + 0.9, y + 0.1, 2.2, 0.7, wing);
    px(g, x - 3.1, y + 0.7, 1, 0.8, wing_tip);
    px(g, x + 2.6, y + 0.7, 1, 0.8, wing_tip);
  }
  px(g, x - 0.6, y - 0.7, 1.7, 1.7, body);
  px(g, x - 0.5, y - 1.1, 0.4, 0.5, body); // ears
  px(g, x + 0.6, y - 1.1, 0.4, 0.5, body);
  px(g, x - 0.3, y - 0.4, 0.4, 0.4, "#e04040"); // eyes
  px(g, x + 0.4, y - 0.4, 0.4, 0.4, "#e04040");
}

// Swarm billboard. The poses match the soldier set so enemy code can stay
// generic; aim/fire never trigger for bats and just alias the flaps.
static Image *bat_swarm_sprite(Pose pose) {
  static const double flock[][2] = {{4.5, 4}, {10.5, 3}, {7.5, 7}, {12, 8.5}, {4, 9.5}, {9, 11}};
  Image *c = image_new(TEX, TEX);

  if (pose == POSE_STAND) {
    // roosting: a huddle of bats hanging under the ceiling, eyes aglow
    static const double hang[] = {4.8, 6.8, 8.8, 10.8};
    px(c, 4, 0, 8.5, 2.6, "#181008");
    for (int i = 0; i < COUNT(hang); i++) {
      double hx = hang[i];
      double hy = i % 2 ? 1.4 : 0.8;
      px(c, hx - 0.4, hy, 1.9, 1.2, "#241a10"); // folded wings
      px(c, hx, hy, 1.1, 2.4, "#2e2214"); // hanging body
      px(c, hx + 0.05, hy + 2.4, 1, 0.9, "#241a10"); // head at the bottom
      px(c, hx + 0.1, hy + 2.6, 0.35, 0.35, "#e04040");
      px(c, hx + 0.6, hy + 2.6, 0.35, 0.35, "#e04040");
    }
    return c;
  }
  if (pose == POSE_DEAD) {
    // a few limp bats on the flagstones
    static const double fallen[][2] = {{4, 2.2}, {8.5, 1.8}, {11.5, 2}};
    for (int i = 0; i < COUNT(fallen); i++) {
      double dx = fallen[i][0], wsp = fallen[i][1];
      px(c, dx - wsp / 2, 14.9, wsp * 2, 0.6, "#241a10");
      px(c, dx - 0.4, 14.5, 1.2, 0.9, "#181008");
    }
    return c;
  }
  if (pose == POSE_DIE1 || pose == POSE_DIE2) {
    // the swarm tumbles: wings folded, dropping toward the floor
    double drop = pose == POSE_DIE1 ? 3 : 6.5;
    for (int i = 0; i < COUNT(flock); i++) {
      double bx = flock[i][0], by = flock[i][1];
      double y = fmin(14, by + drop + (i % 3));
      px(c, bx - 1.3, y, 2.6, 0.8, "#33271a");
      px(c, bx - 0.5, y - 0.5, 1.2, 1.4, "#241a10");
    }
    return c;
  }

  double spread = pose == POSE_PAIN ? 1.35 : 1;
  bool alt = pose == POSE_WALK2 || pose == POSE_FIRE;
  for (int i = 0; i < COUNT(flock); i++) {
    double bx = flock[i][0], by = flock[i][1];
    double x = 8 + (bx - 8) * spread;
    double y = 7.5 + (by - 7.5) * spread + (alt ? (i % 2 ? 0.7 : -0.5) : 0);
    mini_bat(c, x, y, (i + (alt ? 1 : 0)) % 2);
  }
  return c;
}

// ------------------------------------------------------------------ hounds

#define DOG_FUR "#5d4326"
#define DOG_DARK "#3b2a16"
#define DOG_BELLY "#7d5f3a"
#define DOG_TONGUE "#c04040"

// War hound billboards. Unlike the soldiery (always drawn face-on), the
// hound reads wrong without a heading: stand/walk poses therefore exist in
// four views (front, back and both profiles) and the enemy code picks one
// from the dog's heading in camera space, the way the arrows do. The poses
// still match the soldier set: fire doubles as the bite (jaws head-on at the
// viewer), aim is never entered by the dog AI. dog_sprite() draws the
// right-facing profile plus the shared pain/death poses.
static Image *dog_sprite(Pose pose) {
  Image *c = image_new(TEX, TEX);

  if (pose == POSE_DEAD || pose == POSE_DIE2) {
    // stretched out on its side
    px(c, 3, 13.6, 10, 1.6, DOG_FUR);
    px(c, 1.6, 13.9, 1.8, 1.3, DOG_DARK); // head flat on the flagstones
    px(c, 13, 13.2, 2.2, 0.7, DOG_DARK); // tail
    px(c, 4.5, 15.1, 6.5, 0.9, DOG_DARK); // legs folded together
    if (pose == POSE_DEAD) px(c, 5, 15.5, 5.5, 0.5, "#5c0f0f");
    return c;
  }
  if (pose == POSE_DIE1) {
    // legs buckling, muzzle dropping
    px(c, 3.5, 12, 8.5, 2.6, DOG_FUR);
    px(c, 2, 12.8, 2.2, 2.2, DOG_DARK); // head sagging
    px(c, 5, 14.4, 1.3, 1.6, DOG_DARK);
    px(c, 9.5, 14.4, 1.3, 1.6, DOG_DARK);
    px(c, 12, 11.6, 2, 0.8, DOG_DARK); // tail down
    return c;
  }

  if (pose == POSE_FIRE) {
    // the bite, head-on: ears flat, jaws wide, teeth bared
    px(c, 5, 6, 6, 5, DOG_FUR); // head
    px(c, 3.6, 5.4, 1.6, 1.8, DOG_DARK); // ears swept flat
    px(c, 10.8, 5.4, 1.6, 1.8, DOG_DARK);
    px(c, 6.1, 7.4, 1, 1, "#e8d84a"); // eyes ablaze
    px(c, 8.9, 7.4, 1, 1, "#e8d84a");
    px(c, 5.4, 10.4, 5.2, 3.6, DOG_DARK); // open maw
    px(c, 5.9, 10.7, 4.2, 1.2, "#7c1616"); // gullet
    px(c, 5.9, 11.9, 4.2, 1.6, DOG_TONGUE);
    for (int i = 0; i < 4; i++) {
      px(c, 5.9 + i * 1.15, 10.4, 0.6, 0.9, "#e8e4d8"); // fangs top & bottom
      px(c, 5.9 + i * 1.15, 13.2, 0.6, 0.8, "#e8e4d8");
    }
    px(c, 4.2, 13.8, 7.6, 1.6, DOG_FUR); // shoulders below
    px(c, 4.6, 15.2, 2, 0.8, DOG_DARK); // forepaws
    px(c, 9.4, 15.2, 2, 0.8, DOG_DARK);
    return c;
  }

  // side profile: stand / walk1 / walk2 / pain / aim
  double y0 = pose == POSE_PAIN ? 10.8 : 10; // back line; pain cowers lower
  px(c, 2, y0 - 0.6, 1.4, 0.8, DOG_DARK); // tail raised
  px(c, 2.7, y0 + 0.2, 1, 0.9, DOG_DARK);
  px(c, 3.4, y0, 8, 3, DOG_FUR); // body
  px(c, 4, y0 + 2.3, 7, 0.9, DOG_BELLY);
  px(c, 10.8, y0 - 2, 2.6, 2.9, DOG_FUR); // head
  px(c, 13.2, y0 - 1.3, 2, 1.2, DOG_FUR); // muzzle
  px(c, 14.9, y0 - 1.3, 0.6, 0.7, "#16120c"); // nose
  px(c, 10.9, y0 - 2.8, 0.9, 1.1, DOG_DARK); // ear pricked up
  px(c, 12.3, y0 - 1.6, 0.6, 0.6, "#141414"); // eye
  if (pose == POSE_PAIN) px(c, 13.4, y0 - 0.1, 1.4, 0.6, DOG_TONGUE); // yelping

  if (pose == POSE_WALK1) {
    // full gallop stretch: hind legs swept back, forelegs reaching
    px(c, 2.4, y0 + 2.9, 1.7, 1.2, DOG_FUR);
    px(c, 1.4, y0 + 3.7, 1.2, 1, DOG_DARK);
    px(c, 11.2, y0 + 2.9, 1.7, 1.2, DOG_FUR);
    px(c, 12.6, y0 + 3.7, 1.2, 1, DOG_DARK);
  } else if (pose == POSE_WALK2) {
    // legs gathered under the body
    px(c, 5.2, y0 + 2.8, 1.3, 2.7, DOG_FUR);
    px(c, 8.8, y0 + 2.8, 1.3, 2.7, DOG_FUR);
    px(c, 5, y0 + 5, 1.6, 0.7, DOG_DARK);
    px(c, 8.6, y0 + 5, 1.6, 0.7, DOG_DARK);
  } else {
    // standing square on all fours
    px(c, 4.2, y0 + 3, 1.1, 2.8, DOG_FUR);
    px(c, 5.9, y0 + 3.2, 1, 2.6, DOG_DARK);
    px(c, 9.2, y0 + 3, 1.1, 2.8, DOG_FUR);
    px(c, 10.9, y0 + 3.2, 1, 2.6, DOG_DARK);
  }
  return c;
}

// The hound head-on: pricked ears, chest between the forelegs. Gallop
// frames alternate the legs and bob the whole body.
static Image *dog_front_sprite(Pose pose) {
  Image *c = image_new(TEX, TEX);
  double bob = pose == POSE_WALK2 ? 0.4 : 0;

  px(c, 4.6, 11 + bob, 1, 2.6, DOG_DARK); // haunches peeking past the shoulders
  px(c, 10.4, 11 + bob, 1, 2.6, DOG_DARK);
  px(c, 5.4, 9.8 + bob, 5.2, 4, DOG_FUR); // chest & shoulders
  px(c, 6.6, 11.6 + bob, 2.8, 2.2, DOG_BELLY); // chest blaze
  px(c, 5.8, 5.6 + bob, 1, 1.5, DOG_DARK); // pricked ears
  px(c, 9.2, 5.6 + bob, 1, 1.5, DOG_DARK);
  px(c, 6, 6.6 + bob, 4, 3.2, DOG_FUR); // head
  px(c, 6.7, 7.5 + bob, 0.8, 0.8, "#141414"); // eyes
  px(c, 8.5, 7.5 + bob, 0.8, 0.8, "#141414");
  px(c, 7.1, 8.5 + bob, 1.8, 1.5, DOG_DARK); // muzzle
  px(c, 7.5, 8.5 + bob, 1, 0.7, "#16120c"); // nose

  if (pose == POSE_WALK1) {
    px(c, 5.8, 13.4, 1.3, 2.5, DOG_FUR); // near leg planted
    px(c, 8.9, 13.4, 1.3, 1.7, DOG_DARK); // off leg lifted
  } else if (pose == POSE_WALK2) {
    px(c, 8.9, 13.4, 1.3, 2.5, DOG_FUR);
    px(c, 5.8, 13.4, 1.3, 1.7, DOG_DARK);
  } else {
    px(c, 5.8, 13.6, 1.3, 2.2, DOG_FUR);
    px(c, 8.9, 13.6, 1.3, 2.2, DOG_FUR);
    px(c, 5.6, 15.3, 1.6, 0.6, DOG_DARK); // paws
    px(c, 8.7, 15.3, 1.6, 0.6, DOG_DARK);
  }
  return c;
}

// The hound from behind: rump and hind legs, ears past the head, the tail
// in front of everything and swaying with the gallop.
static Image *dog_back_sprite(Pose pose) {
  Image *c = image_new(TEX, TEX);
  double bob = pose == POSE_WALK2 ? 0.4 : 0;
  double sway = pose == POSE_WALK1 ? -0.5 : pose == POSE_WALK2 ? 0.5 : 0;

  px(c, 6, 5.8 + bob, 1, 1.4, DOG_DARK); // ears from behind
  px(c, 8.8, 5.8 + bob, 1, 1.4, DOG_DARK);
  px(c, 6.2, 6.8 + bob, 3.6, 3.1, DOG_FUR); // back of the head, no face
  px(c, 5.2, 9.8 + bob, 5.6, 4, DOG_FUR); // rump
  px(c, 5.6, 12.4 + bob, 4.8, 1.4, DOG_DARK); // shadow under it

  if (pose == POSE_WALK1) {
    px(c, 5.7, 13.6, 1.3, 2.3, DOG_FUR);
    px(c, 8.8, 13.6, 1.3, 1.6, DOG_DARK); // off leg tucked
  } else if (pose == POSE_WALK2) {
    px(c, 8.8, 13.6, 1.3, 2.3, DOG_FUR);
    px(c, 5.7, 13.6, 1.3, 1.6, DOG_DARK);
  } else {
    px(c, 5.7, 13.8, 1.3, 2.1, DOG_FUR);
    px(c, 8.8, 13.8, 1.3, 2.1, DOG_FUR);
    px(c, 5.5, 15.3, 1.6, 0.6, DOG_DARK);
    px(c, 8.6, 15.3, 1.6, 0.6, DOG_DARK);
  }
  // tail last: nearest the viewer, raised over the rump
  px(c, 8.3 + sway, 7.4 + bob, 0.9, 2.8, DOG_DARK);
  px(c, 8.1 + sway, 6.7 + bob, 1.3, 0.9, DOG_DARK);
  return c;
}

// ------------------------------------------------------------------ rats

#define RAT_FUR "#5b5048"
#define RAT_DARK "#3a3430"
#define RAT_BELLY "#867a6c"
#define RAT_PINK "#b98078"
#define RAT_TAIL "#9a7470"
#define RAT_EYE "#e04040"
#define RAT_TOOTH "#e0c86a"
#define RAT_WHISKER "#c8beb0"

static void whisker(Image *g, double x, double y, double w) { px_c(g, x, y, w, 0.12, faded(RAT_WHISKER, 0.7)); }

// Rat billboards, set up the way the hound's are: rat_sprite() draws the
// right-facing profile plus the shared pain/death poses, fire doubles as
// the bite (reared up at the viewer, incisors bared), and the front and
// back views come from their own painters. The rat hugs the floor, so it
// is painted into the bottom rows of the tile.
static Image *rat_sprite(Pose pose) {
  Image *c = image_new(TEX, TEX);

  if (pose == POSE_DEAD || pose == POSE_DIE2) {
    // on its back, paws curled up, the tail limp along the flagstones
    if (pose == POSE_DEAD) px(c, 5, 15.6, 6, 0.4, "#5c0f0f");
    px(c, 0.6, 15.3, 3.8, 0.5, RAT_TAIL);
    px(c, 4, 14, 8, 1.8, RAT_FUR);
    px(c, 4.6, 13.6, 6.6, 0.8, RAT_BELLY);
    px(c, 11.8, 14.3, 2.6, 1.5, RAT_FUR); // head lolling
    px(c, 14.2, 14.9, 0.6, 0.5, RAT_PINK);
    for (int i = 0; i < 4; i++) px(c, 5.4 + i * 1.5, 12.7 + (i % 2) * 0.3, 0.5, 1, RAT_PINK);
    return c;
  }
  if (pose == POSE_DIE1) {
    // rolling over, legs kicking
    px(c, 1.2, 14.6, 2.8, 0.5, RAT_TAIL);
    px(c, 4, 12.8, 8, 2.8, RAT_FUR);
    px(c, 4.6, 12.4, 6.4, 0.8, RAT_BELLY);
    px(c, 11.8, 13.2, 2.6, 2.1, RAT_FUR);
    px(c, 14.2, 13.8, 0.6, 0.6, RAT_PINK);
    px(c, 5.4, 11.2, 0.6, 1.4, RAT_PINK);
    px(c, 9.6, 11, 0.6, 1.6, RAT_PINK);
    return c;
  }

  if (pose == POSE_FIRE) {
    // the bite, head-on: reared up, forepaws spread, incisors bared
    px(c, 4.6, 12.2, 6.8, 3.2, RAT_FUR); // body behind the head
    px(c, 4, 8.2, 1.9, 1.9, RAT_DARK); // round ears
    px(c, 10.1, 8.2, 1.9, 1.9, RAT_DARK);
    px(c, 4.4, 8.6, 1.1, 1.1, RAT_PINK);
    px(c, 10.5, 8.6, 1.1, 1.1, RAT_PINK);
    px(c, 5.4, 9, 5.2, 3.6, RAT_FUR); // head
    px(c, 6.2, 9.9, 0.8, 0.8, RAT_EYE);
    px(c, 9, 9.9, 0.8, 0.8, RAT_EYE);
    px(c, 7.4, 10.8, 1.2, 0.7, RAT_PINK); // nose
    px(c, 6.6, 11.6, 2.8, 2.6, "#3a1414"); // gaping mouth
    px(c, 7.3, 11.5, 0.6, 1.3, RAT_TOOTH); // long yellow incisors
    px(c, 8.1, 11.5, 0.6, 1.3, RAT_TOOTH);
    px(c, 7.4, 13.3, 0.5, 0.9, RAT_TOOTH);
    px(c, 8.1, 13.3, 0.5, 0.9, RAT_TOOTH);
    whisker(c, 3, 11.1, 3.2);
    whisker(c, 3.3, 11.7, 2.9);
    whisker(c, 9.8, 11.1, 3.2);
    whisker(c, 9.8, 11.7, 2.9);
    px(c, 3.8, 13.4, 1.8, 1, RAT_PINK); // forepaws thrown out
    px(c, 10.4, 13.4, 1.8, 1, RAT_PINK);
    px(c, 5.2, 15.3, 1.6, 0.7, RAT_PINK); // hind feet
    px(c, 9.2, 15.3, 1.6, 0.7, RAT_PINK);
    return c;
  }

  // side profile: stand / walk1 / walk2 / pain / aim
  double y0 = pose == POSE_PAIN ? 11.7 : pose == POSE_WALK2 ? 10.7 : 11.2; // back line; walk2 arches it
  if (pose == POSE_WALK1) {
    // stretched out at full scurry, the tail streaming level behind
    px(c, 0.3, y0 + 2.4, 1.8, 0.45, RAT_TAIL);
    px(c, 2, y0 + 2.2, 2.2, 0.5, RAT_TAIL);
  } else {
    // trailing along the floor
    px(c, 0.4, 15.2, 1.4, 0.4, RAT_TAIL);
    px(c, 1.4, 14.8, 1.6, 0.45, RAT_TAIL);
    px(c, 2.8, y0 + 3.1, 1.6, 0.5, RAT_TAIL);
  }
  px(c, 4, y0 + 0.9, 7, 3.3, RAT_FUR); // body
  px(c, 5, y0, 5, 1, RAT_FUR); // hunched back
  px(c, 4.6, y0 + 3.5, 5.8, 0.8, RAT_BELLY);
  px(c, 10.4, y0 + 1, 2.6, 2.6, RAT_FUR); // head tapering into the snout
  px(c, 12.8, y0 + 1.7, 1.6, 1.6, RAT_FUR);
  px(c, 14.3, y0 + 2.2, 0.9, 1, RAT_FUR);
  px(c, 15, y0 + 2.3, 0.6, 0.6, RAT_PINK); // nose
  px(c, 10.6, y0 - 0.2, 1.4, 1.5, RAT_DARK); // round ear
  px(c, 10.9, y0 + 0.1, 0.8, 0.9, RAT_PINK);
  px(c, 12.3, y0 + 1.7, 0.6, 0.6, RAT_EYE);
  whisker(c, 13.6, y0 + 3, 2.2);
  whisker(c, 13.8, y0 + 3.4, 2);
  if (pose == POSE_PAIN) {
    // squealing, teeth showing
    px(c, 13.9, y0 + 3, 1.2, 0.6, "#3a1414");
    px(c, 14.3, y0 + 2.9, 0.4, 0.5, RAT_TOOTH);
  }

  double feet_y = fmin(y0 + 4.2, 15.4);
  if (pose == POSE_WALK1) {
    // forepaws reaching, hind feet kicked back
    px(c, 12.2, feet_y, 1.5, 0.6, RAT_PINK);
    px(c, 2.8, feet_y, 1.6, 0.6, RAT_PINK);
  } else if (pose == POSE_WALK2) {
    // all four gathered under the arched back
    px(c, 7, y0 + 4.3, 1.4, 1, RAT_DARK);
    px(c, 6.8, 15.4, 1.6, 0.6, RAT_PINK);
    px(c, 9, y0 + 4.3, 1, 1, RAT_DARK);
    px(c, 8.9, 15.4, 1.3, 0.6, RAT_PINK);
  } else {
    px(c, 4.4, feet_y, 1.8, 0.6, RAT_PINK); // hind foot
    px(c, 10.8, y0 + 3.6, 0.8, 0.8, RAT_DARK); // foreleg
    px(c, 10.7, feet_y, 1.2, 0.6, RAT_PINK);
  }
  return c;
}

// The rat head-on: round ears, beady eyes, whiskers fanning from the snout.
// Scurry frames alternate the forepaws and bob the whole body.
static Image *rat_front_sprite(Pose pose) {
  Image *c = image_new(TEX, TEX);
  double bob = pose == POSE_WALK2 ? 0.4 : 0;

  px(c, 4.9, 11.6 + bob, 6.2, 3.6, RAT_FUR); // shoulders and flanks
  px(c, 4.9, 10.2 + bob, 1.7, 1.7, RAT_DARK); // ears
  px(c, 9.4, 10.2 + bob, 1.7, 1.7, RAT_DARK);
  px(c, 5.3, 10.6 + bob, 0.9, 0.9, RAT_PINK);
  px(c, 9.8, 10.6 + bob, 0.9, 0.9, RAT_PINK);
  px(c, 6, 11 + bob, 4, 2.8, RAT_FUR); // head
  px(c, 6.6, 12.8 + bob, 2.8, 1.6, RAT_BELLY); // pale snout
  px(c, 6.6, 11.8 + bob, 0.7, 0.7, RAT_EYE);
  px(c, 8.7, 11.8 + bob, 0.7, 0.7, RAT_EYE);
  px(c, 7.5, 13.5 + bob, 1, 0.7, RAT_PINK); // nose
  whisker(c, 4.2, 13.4 + bob, 2.6);
  whisker(c, 9.2, 13.4 + bob, 2.6);

  if (pose == POSE_WALK1) {
    px(c, 5.6, 15.2, 1.3, 0.8, RAT_PINK); // near paw planted
    px(c, 9.1, 14.6, 1.3, 0.7, RAT_PINK); // off paw lifted
  } else if (pose == POSE_WALK2) {
    px(c, 9.1, 15.2, 1.3, 0.8, RAT_PINK);
    px(c, 5.6, 14.6, 1.3, 0.7, RAT_PINK);
  } else {
    px(c, 5.6, 15.3, 1.3, 0.7, RAT_PINK);
    px(c, 9.1, 15.3, 1.3, 0.7, RAT_PINK);
  }
  return c;
}

// The rat from behind: ears over a hunched rump, pink soles, and the long
// tail snaking out toward the viewer in front of everything.
static Image *rat_back_sprite(Pose pose) {
  Image *c = image_new(TEX, TEX);
  double bob = pose == POSE_WALK2 ? 0.4 : 0;
  double sway = pose == POSE_WALK1 ? -0.6 : pose == POSE_WALK2 ? 0.6 : 0;

  px(c, 5.4, 10.6 + bob, 1.6, 1.6, RAT_DARK); // ears from behind
  px(c, 9, 10.6 + bob, 1.6, 1.6, RAT_DARK);
  px(c, 6.2, 11 + bob, 3.6, 2, RAT_FUR); // back of the head
  px(c, 4.8, 12 + bob, 6.4, 3.4, RAT_FUR); // rump
  px(c, 5.2, 14.6 + bob, 5.6, 0.8, RAT_DARK); // shadow under the haunches

  if (pose == POSE_WALK1) {
    px(c, 5.4, 15.3, 1.4, 0.7, RAT_PINK);
    px(c, 9.2, 14.8, 1.4, 0.6, RAT_PINK); // off foot kicked up
  } else if (pose == POSE_WALK2) {
    px(c, 9.2, 15.3, 1.4, 0.7, RAT_PINK);
    px(c, 5.4, 14.8, 1.4, 0.6, RAT_PINK);
  } else {
    px(c, 5.4, 15.3, 1.4, 0.7, RAT_PINK);
    px(c, 9.2, 15.3, 1.4, 0.7, RAT_PINK);
  }
  // tail last: nearest the viewer
  px(c, 7.6, 14 + bob, 0.8, 1.1, RAT_TAIL);
  px(c, 7.6 + sway * 0.5, 15, 0.7, 0.5, RAT_TAIL);
  px(c, 7.6 + sway, 15.5, 0.7, 0.5, RAT_TAIL);
  return c;
}

// ------------------------------------------------------------------ spiders

#define SPIDER_BODY "#2f2721"
#define SPIDER_DARK "#1c1714"
#define SPIDER_LEG "#3c3129"
#define SPIDER_BAND "#6e5a40"
#define SPIDER_MARK "#b0985e"
#define SPIDER_EYE "#e04040"
#define SPIDER_GLINT "#f4e6d0"
#define SPIDER_FANG "#c9b98c"
#define SPIDER_VENOM "#9ad04a"
#define SPIDER_ICHOR "#33461f"
#define SPIDER_SILK "#d8d3c6"

// A jointed leg on the 16x16 grid: body -> knee -> foot.
typedef struct {
  double bx, by, kx, ky, fx, fy;
} Leg;

// Near-side legs are lit, with a pale band at the knee; the far side's
// stay in shadow behind the body.
static void spider_leg(Image *g, Leg l, bool near) {
  Color c = color_parse(near ? SPIDER_LEG : SPIDER_DARK);
  gfx_line(g, (float)(l.bx * 4), (float)(l.by * 4), (float)(l.kx * 4), (float)(l.ky * 4), 2.2f, c);
  gfx_line(g, (float)(l.kx * 4), (float)(l.ky * 4), (float)(l.fx * 4), (float)(l.fy * 4), 1.6f, c);
  px(g, l.kx - 0.3, l.ky - 0.3, 0.6, 0.6, near ? SPIDER_BAND : SPIDER_DARK);
}

// the same leg on the other side of the tile's middle
static Leg mirror_leg(Leg l) { return (Leg){16 - l.bx, l.by, 16 - l.kx, l.ky, 16 - l.fx, l.fy}; }

// the same leg moved over (a far-side leg peeking out behind a near one)
static Leg shift_leg(Leg l, double dx, double dy) {
  return (Leg){l.bx, l.by + dy, l.kx + dx, l.ky + dy, l.fx + dx, l.fy + dy};
}

// A leg mid-stride: swung forward (s > 0) or back and lifted (s < 0).
static Leg stride_leg(Leg l, double s) {
  l.kx += s * 0.6;
  l.fx += s * 1.2;
  if (s < 0) {
    l.ky -= 0.3;
    l.fy -= 0.8;
  }
  return l;
}

// Spider billboards, set up the way the hound's are: spider_sprite() draws
// the right-facing profile plus the shared pain/death poses, fire doubles
// as the bite (reared up at the viewer, fangs dripping) and aim as the
// spider hanging head-down on its dropline; the front and back views come
// from their own painters. Its legs reach the floor at the bottom of the
// tile.
static Image *spider_sprite(Pose pose) {
  Image *c = image_new(TEX, TEX);

  if (pose == POSE_DEAD || pose == POSE_DIE2) {
    // on its back, a row of legs curled up tight over the belly
    static const Leg curled[4] = {{6.4, 13.6, 5.4, 11.6, 6.2, 10.6},
                                  {7.6, 13.6, 6.9, 11.2, 7.8, 10.4},
                                  {8.8, 13.6, 8.6, 11.2, 9.6, 10.6},
                                  {10, 13.8, 10.6, 11.6, 11.2, 12.4}};
    if (pose == POSE_DEAD) px(c, 3.4, 15.5, 8.8, 0.5, SPIDER_ICHOR);
    for (int i = 0; i < 4; i++) spider_leg(c, shift_leg(curled[i], 0.6, -0.3), false);
    px(c, 3, 13.4, 5.4, 2.2, SPIDER_BODY); // abdomen
    px(c, 3.5, 13, 4.4, 0.5, SPIDER_BODY);
    px(c, 8.2, 13.6, 3, 1.9, SPIDER_BODY);
    px(c, 3.6, 13.1, 4.2, 0.35, SPIDER_LEG); // the paler belly turned up
    for (int i = 0; i < 4; i++) spider_leg(c, curled[i], true);
    return c;
  }
  if (pose == POSE_DIE1) {
    // rolling over, legs flailing
    static const Leg flail[4] = {{9, 12.6, 10.6, 9.6, 12.8, 10.6},
                                 {8.8, 12.6, 8.4, 9.2, 9.6, 8},
                                 {8.4, 13, 6.4, 10.4, 5.2, 9.4},
                                 {9.8, 13.2, 12.4, 12, 14.2, 13.8}};
    for (int i = 0; i < 4; i++) spider_leg(c, mirror_leg(flail[3 - i]), false);
    px(c, 3, 12.4, 5.2, 2.8, SPIDER_BODY);
    px(c, 3.5, 12, 4.2, 0.5, SPIDER_BODY);
    px(c, 8, 12.4, 3, 2.1, SPIDER_BODY);
    px(c, 3.9, 12.6, 2, 0.45, SPIDER_MARK);
    for (int i = 0; i < 4; i++) spider_leg(c, flail[i], true);
    return c;
  }

  if (pose == POSE_FIRE) {
    // the bite, head-on: reared up tall on the hind legs (so the fangs still
    // show at the player's feet) with the forelegs raised high, fangs spread
    // wide and dripping venom
    static const Leg reared[4] = {{6.4, 7.8, 3.4, 4.6, 4.8, 0.8},
                                  {6, 8.6, 2.6, 6.8, 0.8, 12.4},
                                  {6, 9.4, 3, 9.6, 1.6, 15.6},
                                  {6.4, 10, 4, 11.4, 3.6, 15.8}};
    px(c, 5.2, 10.6, 5.6, 2.6, SPIDER_DARK); // abdomen glimpsed beneath
    for (int i = 0; i < 4; i++) {
      spider_leg(c, reared[i], true);
      spider_leg(c, mirror_leg(reared[i]), true);
    }
    px(c, 5.6, 6.6, 4.8, 4, SPIDER_BODY);
    px(c, 6, 6.2, 4, 0.5, SPIDER_BODY);
    px(c, 6.8, 7.2, 0.9, 0.9, SPIDER_EYE); // eight eyes: two big, six beads
    px(c, 8.3, 7.2, 0.9, 0.9, SPIDER_EYE);
    px(c, 6, 7.8, 0.5, 0.5, SPIDER_EYE);
    px(c, 9.5, 7.8, 0.5, 0.5, SPIDER_EYE);
    px(c, 7.1, 6.5, 0.45, 0.45, SPIDER_EYE);
    px(c, 8.45, 6.5, 0.45, 0.45, SPIDER_EYE);
    px(c, 6.15, 7.3, 0.35, 0.35, SPIDER_EYE);
    px(c, 9.5, 7.3, 0.35, 0.35, SPIDER_EYE);
    px(c, 7, 7.3, 0.3, 0.3, SPIDER_GLINT);
    px(c, 8.5, 7.3, 0.3, 0.3, SPIDER_GLINT);
    px(c, 6.2, 8.8, 1.5, 1.8, SPIDER_DARK); // chelicerae
    px(c, 8.3, 8.8, 1.5, 1.8, SPIDER_DARK);
    px(c, 6.4, 10.5, 0.6, 1.2, SPIDER_FANG);
    px(c, 9, 10.5, 0.6, 1.2, SPIDER_FANG);
    px(c, 6.9, 11.5, 0.4, 0.4, SPIDER_FANG); // fang tips hooking inward
    px(c, 8.7, 11.5, 0.4, 0.4, SPIDER_FANG);
    px(c, 6.95, 12.2, 0.35, 0.6, SPIDER_VENOM);
    px(c, 8.75, 12.5, 0.35, 0.5, SPIDER_VENOM);
    px(c, 5.1, 8.2, 0.5, 1.6, SPIDER_LEG); // feelers raised to either side
    px(c, 10.4, 8.2, 0.5, 1.6, SPIDER_LEG);
    return c;
  }

  if (pose == POSE_AIM) {
    // hanging head-down on its dropline, legs splayed; the renderer runs the
    // thread on up to the ceiling from the top of the tile
    static const Leg splayed[4] = {{7, 8.8, 4.4, 5.6, 3.6, 3.2}, // hind legs reach up
                                   {6.8, 9.2, 3.6, 7.6, 2.2, 6.2},
                                   {6.8, 9.8, 3.6, 10, 2, 11.6},
                                   {7, 10.2, 4.6, 11.4, 4, 14}}; // forelegs reach down
    px_c(c, 7.85, 0, 0.3, 4.4, faded(SPIDER_SILK, 0.55));
    for (int i = 0; i < 4; i++) {
      spider_leg(c, splayed[i], true);
      spider_leg(c, mirror_leg(splayed[i]), true);
    }
    px(c, 7.7, 4, 0.6, 0.4, SPIDER_DARK); // spinnerets, where the thread pays out
    px(c, 5.8, 4.6, 4.4, 4.2, SPIDER_BODY); // abdomen up top
    px(c, 6.3, 4.2, 3.4, 0.5, SPIDER_BODY);
    px(c, 6.3, 8.7, 3.4, 0.4, SPIDER_BODY);
    px(c, 6.9, 6.2, 2.2, 0.45, SPIDER_MARK); // the pale cross
    px(c, 7.78, 5.3, 0.45, 2.2, SPIDER_MARK);
    px(c, 6.6, 9, 2.8, 2, SPIDER_BODY); // head below
    px(c, 7, 10.5, 0.6, 0.5, SPIDER_EYE);
    px(c, 8.4, 10.5, 0.6, 0.5, SPIDER_EYE);
    px(c, 7, 11, 0.8, 0.9, SPIDER_DARK);
    px(c, 8.2, 11, 0.8, 0.9, SPIDER_DARK);
    px(c, 7.25, 11.9, 0.4, 0.6, SPIDER_FANG);
    px(c, 8.35, 11.9, 0.4, 0.6, SPIDER_FANG);
    return c;
  }

  // side profile: stand / walk1 / walk2 / pain. The legs step in two
  // alternating sets; pain cowers low with the legs drawn in.
  static const Leg profile[4] = {{10.2, 12, 12.6, 8.8, 15.2, 15.8}, // forelegs reach ahead
                                 {9.4, 12.2, 11, 8.4, 12.6, 15.8},
                                 {8.6, 12.2, 7.6, 8.6, 5.8, 15.8},
                                 {8, 12, 5.4, 9, 1.6, 15.8}}; // hind legs trail behind
  bool pain = pose == POSE_PAIN;
  double stride = pose == POSE_WALK1 ? 1 : pose == POSE_WALK2 ? -1 : 0;
  double dy = pain ? 0.6 : pose == POSE_WALK2 ? -0.25 : 0;
  Leg legs[4];
  for (int i = 0; i < 4; i++) {
    Leg l = profile[i];
    if (pain) {
      l.by += dy;
      l.ky += 1.2;
      l.kx += (8.6 - l.kx) * 0.2;
      l.fx += (8.6 - l.fx) * 0.25;
    }
    legs[i] = l;
  }
  // the far side's legs, a step out of phase and a little behind
  for (int i = 0; i < 4; i++) {
    Leg far = stride_leg(legs[i], i % 2 ? stride : -stride);
    spider_leg(c, shift_leg(far, 0.6, -0.3), false);
  }
  px(c, 2.4, 10 + dy, 5.4, 3.4, SPIDER_BODY); // abdomen, a big sack slung behind
  px(c, 3, 9.4 + dy, 4.2, 0.6, SPIDER_BODY);
  px(c, 3, 13.4 + dy, 4.2, 0.5, SPIDER_BODY);
  px(c, 1.9, 10.6 + dy, 0.5, 2.2, SPIDER_BODY);
  px(c, 1.5, 12.2 + dy, 0.5, 0.5, SPIDER_DARK); // spinnerets
  px(c, 3.9, 10.2 + dy, 2, 0.45, SPIDER_MARK); // the pale cross on its back
  px(c, 4.7, 9.7 + dy, 0.45, 1.6, SPIDER_MARK);
  px(c, 7.6, 10.8 + dy, 3.2, 2.2, SPIDER_BODY); // head and thorax
  px(c, 8, 10.4 + dy, 2.4, 0.5, SPIDER_BODY);
  px(c, 10.6, 11.6 + dy, 1, 1.4, SPIDER_DARK); // chelicerae
  px(c, 11.2, 11.4 + dy, 1.6, 0.45, SPIDER_LEG); // feeler
  px(c, 12.5, 11.6 + dy, 0.45, 1, SPIDER_LEG);
  if (pain) {
    // fangs flung wide
    px(c, 11.4, 12.6 + dy, 0.45, 0.9, SPIDER_FANG);
    px(c, 10.6, 13 + dy, 0.45, 0.8, SPIDER_FANG);
  } else {
    px(c, 11, 13 + dy, 0.45, 0.8, SPIDER_FANG);
  }
  for (int i = 0; i < 4; i++) spider_leg(c, stride_leg(legs[i], i % 2 ? -stride : stride), true);
  // the eyes last, glinting on the brow between the near legs
  px(c, 9.9, 10.6 + dy, 0.5, 0.5, SPIDER_EYE);
  px(c, 10.5, 11 + dy, 0.4, 0.4, SPIDER_EYE);
  px(c, 9.3, 10.5 + dy, 0.35, 0.35, SPIDER_EYE);
  return c;
}

// The spider head-on: its eyes catching the light under the bulk of the
// abdomen, legs fanned out to both sides. Scuttle frames lift alternate
// legs and bob the body.
static const Leg SPIDER_FAN[4] = {{6.6, 12.4, 4.8, 10.4, 4.6, 15.8},
                                  {6.4, 12.2, 3.6, 9.4, 2.8, 15.6},
                                  {6.4, 11.8, 2.6, 9, 1.2, 15.2},
                                  {6.6, 11.4, 2.2, 9.2, 0.4, 14.4}};

// One leg of the fan: a criss-cross set of four is lifted on walk1, the
// other set on walk2.
static Leg fan_leg(int i, bool right, Pose pose) {
  Leg l = SPIDER_FAN[i];
  bool first_set = (i % 2 == 1) == right;
  bool up = pose == POSE_WALK1 ? first_set : pose == POSE_WALK2 && !first_set;
  if (up) {
    l.ky -= 0.4;
    l.fy -= 0.8;
  }
  return right ? mirror_leg(l) : l;
}

static Image *spider_front_sprite(Pose pose) {
  Image *c = image_new(TEX, TEX);
  double bob = pose == POSE_WALK2 ? 0.3 : 0;

  px(c, 5.6, 9 + bob, 4.8, 2.8, SPIDER_BODY); // abdomen looming behind
  px(c, 6.1, 8.5 + bob, 3.8, 0.6, SPIDER_BODY);
  px(c, 7, 8.9 + bob, 2, 0.4, SPIDER_MARK);
  for (int i = 3; i >= 0; i--) {
    spider_leg(c, fan_leg(i, false, pose), true);
    spider_leg(c, fan_leg(i, true, pose), true);
  }
  px(c, 6.2, 11 + bob, 3.6, 2.6, SPIDER_BODY); // head
  px(c, 7, 11.4 + bob, 0.75, 0.75, SPIDER_EYE);
  px(c, 8.25, 11.4 + bob, 0.75, 0.75, SPIDER_EYE);
  px(c, 6.5, 11.9 + bob, 0.4, 0.4, SPIDER_EYE);
  px(c, 9.1, 11.9 + bob, 0.4, 0.4, SPIDER_EYE);
  px(c, 7.15, 11.5 + bob, 0.25, 0.25, SPIDER_GLINT);
  px(c, 8.4, 11.5 + bob, 0.25, 0.25, SPIDER_GLINT);
  px(c, 6.9, 12.6 + bob, 1, 1.3, SPIDER_DARK); // chelicerae
  px(c, 8.1, 12.6 + bob, 1, 1.3, SPIDER_DARK);
  px(c, 7.1, 13.8 + bob, 0.4, 0.6, SPIDER_FANG);
  px(c, 8.5, 13.8 + bob, 0.4, 0.6, SPIDER_FANG);
  px(c, 6.2, 12.8 + bob, 0.5, 1.4, SPIDER_LEG); // feelers
  px(c, 9.3, 12.8 + bob, 0.5, 1.4, SPIDER_LEG);
  return c;
}

// The spider from behind: the swollen abdomen with its pale cross hides
// the head, the legs fanned out past it.
static Image *spider_back_sprite(Pose pose) {
  Image *c = image_new(TEX, TEX);
  double bob = pose == POSE_WALK2 ? 0.3 : 0;

  px(c, 6.4, 10.6 + bob, 3.2, 1.6, SPIDER_BODY); // head beyond
  for (int i = 3; i >= 0; i--) {
    spider_leg(c, fan_leg(i, false, pose), true);
    spider_leg(c, fan_leg(i, true, pose), true);
  }
  px(c, 5.2, 10.6 + bob, 5.6, 4.2, SPIDER_BODY); // abdomen
  px(c, 5.7, 10.1 + bob, 4.6, 0.5, SPIDER_BODY);
  px(c, 5.7, 14.8 + bob, 4.6, 0.4, SPIDER_BODY);
  px(c, 6.9, 11.6 + bob, 2.2, 0.5, SPIDER_MARK);
  px(c, 7.75, 10.8 + bob, 0.5, 2.6, SPIDER_MARK);
  px(c, 7.6, 14.4 + bob, 0.8, 0.6, SPIDER_DARK); // spinnerets
  return c;
}

// ------------------------------------------------------------------ items

static Image *item_sprite(ItemKind kind) {
  Image *c = image_new(TEX, TEX);
  switch (kind) {
  case ITEM_KEY:
    px(c, 6, 5, 4, 4, "#e9c93c");
    px(c, 7, 6, 2, 2, "#8f7a1c");
    px(c, 7, 9, 2, 5, "#e9c93c");
    px(c, 9, 11, 2, 1, "#e9c93c");
    px(c, 9, 13, 2, 1, "#e9c93c");
    px(c, 6, 5, 1, 1, "#fff6b0");
    break;
  case ITEM_POTION:
    px(c, 7, 5, 2, 2, "#8a6a3a"); // cork
    px(c, 7, 7, 2, 1, "#cfe2e8"); // neck
    px(c, 5, 8, 6, 5, "#c02030"); // round body
    px(c, 4, 9, 8, 3, "#c02030");
    px(c, 6, 13, 4, 1, "#8a1622");
    px(c, 6, 9, 1, 2, "#ff9a9a"); // glint
    break;
  case ITEM_BREAD:
    px(c, 4, 9, 8, 4, "#a06a30");
    px(c, 5, 8, 6, 1, "#c98f4e");
    px(c, 4, 13, 8, 1, "#7c4e1e");
    px(c, 6, 9, 1, 2, "#7c4e1e"); // slashes
    px(c, 9, 9, 1, 2, "#7c4e1e");
    break;
  case ITEM_BOLTS:
  case ITEM_BOLTS_DROP:
    // quiver of crossbow bolts
    px(c, 6, 8, 4, 6, "#5a3a1e");
    px(c, 6, 8, 4, 1, "#7a5230");
    px(c, 6, 13, 4, 1, "#3c2612");
    px(c, 6.5, 5, 1, 3, "#8a6a3a"); // shafts
    px(c, 8.5, 4.5, 1, 3.5, "#8a6a3a");
    px(c, 6.5, 4, 1, 1, "#c9d4d8"); // steel tips
    px(c, 8.5, 3.5, 1, 1, "#c9d4d8");
    break;
  case ITEM_TREASURE:
    // golden chalice
    px(c, 5, 6, 6, 3, "#e9c93c");
    px(c, 6, 9, 4, 1, "#d0a930");
    px(c, 7, 10, 2, 2, "#d0a930");
    px(c, 5, 12, 6, 1, "#e9c93c");
    px(c, 7, 7, 2, 1, "#c02040"); // gem
    px(c, 5, 6, 2, 1, "#fff6b0");
    break;
  case ITEM_ARBALEST:
    // repeating crossbow lying flat
    px(c, 3, 8, 10, 1, "#4a3018"); // limbs
    px(c, 3, 7, 1, 1, "#33220f");
    px(c, 12, 7, 1, 1, "#33220f");
    px(c, 3, 9, 10, 0.8, "#b8c4c8"); // string
    px(c, 6, 9.8, 4, 2, "#5a3a1e"); // magazine
    px(c, 7, 11.8, 2, 2.5, "#3a2a16"); // stock
    break;
  case ITEM_KIND_COUNT:
    break;
  }
  return c;
}

// A four-pointed glint the renderer flashes over a key every few seconds,
// so a distant key catches the eye through the murk.
static Image *sparkle_sprite(void) {
  Image *c = image_new(TEX, TEX);
  px(c, 7.6, 3, 0.8, 10, "rgba(255,214,92,0.5)"); // faint long rays
  px(c, 3, 7.6, 10, 0.8, "rgba(255,214,92,0.5)");
  px(c, 7.2, 5.5, 1.6, 5, "#ffd75c"); // bright short rays
  px(c, 5.5, 7.2, 5, 1.6, "#ffd75c");
  px(c, 6.9, 6.9, 2.2, 2.2, "#ffefad"); // hot core
  px(c, 7.4, 7.4, 1.2, 1.2, "#ffffff");
  return c;
}

// Trap paraphernalia: the floor plate (armed / pressed flat) and the arrow
// that flies out of the slit, drawn at chest height. The arrow comes in four
// views picked by its flight direction relative to the camera: side-on
// left/right, point-first (flying at the viewer) and tail-first.
static Image *trap_sprite(TrapSprite kind) {
  if (kind == TRAP_ARROW_LEFT) return mirrored(trap_sprite(TRAP_ARROW));
  Image *c = image_new(TEX, TEX);
  if (kind == TRAP_ARROW) {
    px(c, 2, 7, 10, 0.8, "#6a4a26"); // shaft
    px(c, 2, 7, 10, 0.3, "#8a6a3a"); // highlight
    px(c, 12, 6.6, 2.2, 1.6, "#c9d4d8"); // steel head
    px(c, 1, 6.2, 1.6, 1, "#8c2020"); // fletching
    px(c, 1, 7.6, 1.6, 1, "#8c2020");
  } else if (kind == TRAP_ARROW_TOWARD) {
    // point-first: dark socket, bright steel tip, fletching peeking behind
    px(c, 6.6, 5.9, 0.9, 0.9, "#8c2020");
    px(c, 8.5, 5.9, 0.9, 0.9, "#8c2020");
    px(c, 6.6, 7.4, 0.9, 0.9, "#8c2020");
    px(c, 8.5, 7.4, 0.9, 0.9, "#8c2020");
    px(c, 7, 6.4, 2, 1.6, "#5c666c"); // socket ring
    px(c, 7.4, 6.7, 1.2, 1, "#e8f0f4"); // gleaming point
  } else if (kind == TRAP_ARROW_AWAY) {
    // tail-first: red fletching cross around the shaft's end
    px(c, 6, 6.9, 4, 1.2, "#8c2020");
    px(c, 7.4, 5.5, 1.2, 4, "#8c2020");
    px(c, 7.4, 6.9, 1.2, 1.2, "#3a2a16"); // shaft butt
  } else {
    bool pressed = kind == TRAP_PRESSED;
    px(c, 3, pressed ? 14.2 : 13.2, 10, pressed ? 0.8 : 1.8, pressed ? "#57503f" : "#726a52");
    px(c, 3, 15, 10, 0.8, pressed ? "#3e382c" : "#514a39"); // front edge
    px(c, 3, 15.7, 10, 0.3, "#181209"); // ground shadow
    // iron studs at the corners
    rect(c, 14, (pressed ? 14.4 : 13.6) * 4, 3, 3, "#23262b");
    rect(c, 48, (pressed ? 14.4 : 13.6) * 4, 3, 3, "#23262b");
  }
  return c;
}

// Flat pixel-art emblems for the HUD arms panel: the 3D POV renders make
// poor icons, so these stay 2D like the rest of the HUD.
static Image *weapon_icon(WeaponKind kind) {
  Image *c = image_new(64, 64);
  const char *s = "#c9bd9c"; // string
  if (kind == WEAPON_DAGGER) {
    px(c, 7.2, 0.6, 1.6, 1, "#eef4f6"); // tip
    px(c, 7.2, 1.4, 1.6, 5.8, "#c9d4d8"); // blade
    px(c, 7.2, 1.4, 0.6, 5.8, "#eef4f6");
    px(c, 5, 7.2, 6, 1.3, "#c9a24a"); // guard
    px(c, 6.9, 8.5, 2.2, 4.2, "#3a2a16"); // wrapped grip
    px(c, 6.9, 9.6, 2.2, 0.6, "#5a4326");
    px(c, 6.9, 11, 2.2, 0.6, "#5a4326");
    px(c, 6.6, 12.7, 2.8, 1.5, "#c9a24a"); // pommel
  } else if (kind == WEAPON_CROSSBOW) {
    px(c, 1.5, 3.4, 13, 1.6, "#3c2818"); // bow
    px(c, 0.8, 2.6, 1.4, 1.8, "#2c1c10"); // upswept tips
    px(c, 13.8, 2.6, 1.4, 1.8, "#2c1c10");
    px(c, 1.5, 3.4, 13, 0.5, "#9aa2ac"); // steel lath
    // string drawn back to the nut
    px(c, 1.2, 4.6, 2.4, 0.5, s);
    px(c, 3.6, 5.6, 2.2, 0.5, s);
    px(c, 5.8, 6.6, 1.8, 0.5, s);
    px(c, 12.4, 4.6, 2.4, 0.5, s);
    px(c, 10.2, 5.6, 2.2, 0.5, s);
    px(c, 8.4, 6.6, 1.8, 0.5, s);
    px(c, 7.2, 3, 1.6, 9.5, "#5a3a20"); // stock
    px(c, 7.2, 3, 0.6, 9.5, "#6f4a28");
    px(c, 6.8, 12.5, 2.4, 1.5, "#4a3018"); // butt
  } else {
    px(c, 1, 4.2, 14, 1.9, "#3c2818"); // heavier bow
    px(c, 0.3, 3.3, 1.5, 2, "#2c1c10");
    px(c, 14.2, 3.3, 1.5, 2, "#2c1c10");
    px(c, 1, 4.2, 14, 0.5, "#9aa2ac");
    px(c, 0.8, 5.7, 2.6, 0.5, s);
    px(c, 3.4, 6.7, 2.4, 0.5, s);
    px(c, 5.8, 7.7, 2, 0.5, s);
    px(c, 12.6, 5.7, 2.6, 0.5, s);
    px(c, 10.2, 6.7, 2.4, 0.5, s);
    px(c, 8.2, 7.7, 2, 0.5, s);
    px(c, 6, 1, 4, 3.2, "#7a5230"); // magazine above the bow
    px(c, 6, 1, 4, 0.7, "#8d6238");
    px(c, 6.9, 4.2, 2.2, 8.3, "#5a3a20"); // stock
    px(c, 6.9, 4.2, 0.7, 8.3, "#6f4a28");
    px(c, 6.4, 12.5, 3.2, 1.8, "#4a3018"); // butt
  }
  return c;
}

// ------------------------------------------------------------------ torches, fountain

// A compact wall-mounted torch. Four slightly different silhouettes are
// cycled by the renderer; the chunky shapes preserve the game's pixel-art
// style even when the sprite is scaled up at close range.
static Image *torch_sprite(int frame) {
  static const int sways[] = {-2, 1, 2, -1};
  static const int tips[] = {1, -2, 0, -1};
  static const int wraps[] = {31, 35, 39};
  Image *c = image_new(TEX, TEX);
  int sway = sways[frame];
  int tip = tips[frame];

  // iron wall bracket and its two studs
  rect(c, 26, 33, 17, 5, "rgba(0,0,0,0.45)");
  rect(c, 39, 27, 5, 14, "rgba(0,0,0,0.45)");
  rect(c, 25, 31, 17, 4, "#34373a");
  rect(c, 39, 27, 4, 12, "#34373a");
  rect(c, 40, 28, 1, 10, "#858078");
  rect(c, 40, 29, 2, 2, "#858078");
  rect(c, 40, 35, 2, 2, "#858078");

  // tarred wooden shaft and wrapped grip below the flame
  rect(c, 27, 28, 8, 29, "#25160d");
  rect(c, 28, 29, 5, 27, "#68401f");
  rect(c, 29, 30, 2, 25, "#9a6630");
  for (int i = 0; i < COUNT(wraps); i++) rect(c, 26, wraps[i], 10, 2, "#332014");

  // outer red flame, orange body and a hot yellow-white core
  rect(c, 22 + sway, 9 + tip, 18, 20 - tip, "rgba(120,25,4,0.65)");
  rect(c, 25 + sway, 5 + tip, 12, 8, "rgba(120,25,4,0.65)");
  rect(c, 28 + sway, 2 + tip, 6, 6, "rgba(120,25,4,0.65)");
  rect(c, 24 + sway, 10 + tip, 14, 17 - tip, "#e94b0c");
  rect(c, 27 + sway, 6 + tip, 9, 10, "#e94b0c");
  rect(c, 27 + sway, 13 + tip, 9, 13 - tip, "#ff9f16");
  rect(c, 29 + sway, 9 + tip, 5, 8, "#ff9f16");
  rect(c, 29 + sway, 17 + tip, 5, 8 - tip, "#ffe36a");
  rect(c, 30 + sway, 20 + tip, 3, 4, "#fff3b0");
  return c;
}

// Courtyard fountain: an octagonal stone basin with a carved pedestal and an
// upper bowl. The jet and the spill animate over four frames like the
// torches, so the water never stands still.
static Image *fountain_sprite(int frame) {
  static const double rings[] = {4, 6, 8, 10};
  static const double joints[] = {4.8, 8, 11.2};
  static const double rises[] = {0, -0.5, -0.9, -0.5};
  static const double sways[] = {0, 0.3, 0, -0.3};
  static const double spills[] = {5.3, 10.4};
  Image *c = image_new(TEX, TEX);

  // pool water first: the stone rim then overlaps its front edge
  px(c, 2.4, 10.4, 11.2, 1.4, "#2e5a7a");
  double ring = rings[frame];
  px(c, 8 - ring / 2, 10.7, ring, 0.35, "#4a86b0"); // ripple drifting outward
  px(c, 6.4, 10.45, 3.2, 0.3, "#9fd4ea"); // churn where the spill lands

  // octagonal basin
  px(c, 1.6, 11.4, 12.8, 3.4, "#5c554b");
  px(c, 2.4, 14.8, 11.2, 0.7, "#37322b"); // plinth in shadow
  px(c, 1.6, 11.4, 12.8, 0.5, "#8a8074"); // lit rim
  px(c, 1.6, 11.4, 0.7, 3.4, "#6b6157"); // corner catching the light
  px(c, 13.7, 11.4, 0.7, 3.4, "#443e36");
  for (int i = 0; i < COUNT(joints); i++) px(c, joints[i], 12.4, 0.35, 2.4, "#443e36"); // panel joints

  // pedestal and upper bowl, brimming over
  px(c, 7, 6.2, 2, 4.6, "#5c554b");
  px(c, 7, 6.2, 0.6, 4.6, "#7a7268");
  px(c, 4.9, 5.2, 6.2, 1.2, "#6b6157");
  px(c, 4.9, 5.2, 6.2, 0.4, "#8a8074");
  px(c, 5.6, 4.9, 4.8, 0.5, "#3a6f94");

  // the jet rises, feathers at the tip and falls back into the bowl
  double rise = rises[frame];
  double sway = sways[frame];
  px(c, 7.6 + sway, 2.2 + rise, 0.8, 2.9 - rise, "#4a86b0");
  px(c, 7.8 + sway, 1.6 + rise, 0.4, 1.4, "#9fd4ea");
  px(c, 7.9, 4.6, 0.35, 0.7, "#9fd4ea"); // splash where it lands

  // twin streams spilling from the bowl, broken into falling drops
  for (int s = 0; s < COUNT(spills); s++) {
    for (int i = 0; i < 4; i++) {
      double y = 6.6 + i * 1.1 + ((frame + i) % 2) * 0.45;
      px(c, spills[s], y, 0.4, 0.8, i % 2 == (frame & 1) ? "#9fd4ea" : "#4a86b0");
    }
  }
  return c;
}

// ------------------------------------------------------------------ furniture

static Image *barrel_sprite(int kind) {
  Image *c = image_new(TEX, TEX);
  if (kind == 2) {
    // Scorched staves and a bent hoop, low enough to walk over.
    px(c, 2, 14.8, 12, 1.2, "#211a15");
    px(c, 3, 14, 4, 0.7, "#493222");
    px(c, 8, 14.5, 4.5, 0.8, "#38271c");
    px(c, 5, 13.3, 1, 2.2, "#5a3b22");
    px(c, 10.5, 13.8, 0.7, 2, "#241c17");
    px(c, 3.5, 15, 8, 0.4, "#56504a");
    return c;
  }
  // Bulging oak staves; the edges turn away into shadow.
  px(c, 4, 3, 8, 13, "#352310");
  px(c, 3, 4.5, 10, 10, "#57371c");
  px(c, 2.8, 7, 10.4, 5, "#674320");
  for (int i = 0; i < 5; i++) {
    double x = 3.5 + i * 1.8;
    px(c, x, 4, 1.3, 11.4, i < 2 ? "#936334" : i < 4 ? "#79502a" : "#51331c");
    px(c, x + 0.3, 6 + i % 3, 0.2, 5, "#ae7941");
  }
  px(c, 4, 2.8, 8, 1.5, "#b0844c");
  px(c, 4.5, 3.2, 7, 0.7, "#79532e");
  px(c, 7.6, 3, 0.3, 1.1, "#4b301a");
  for (int i = 0; i < 2; i++) {
    double y = 5 + i * 7.4;
    px(c, 3, y, 10, 1.4, "#34383b");
    px(c, 3.3, y, 9.1, 0.35, "#858681");
    px(c, 4.2, y + 0.5, 0.4, 0.4, "#b0aaa0");
    px(c, 10.8, y + 0.5, 0.4, 0.4, "#777b79");
  }
  if (kind == 1) {
    // A faded vermilion label with a pale powder-flame warning.
    px(c, 5.3, 7.2, 5.2, 4.2, "#862f21");
    px(c, 5.6, 7.4, 4.5, 0.3, "#b64c2c");
    px(c, 7.5, 8, 0.9, 2.5, "#edc57c");
    px(c, 6.7, 9.4, 2.5, 1.2, "#edc57c");
    px(c, 8.4, 8.7, 0.6, 1.3, "#edc57c");
    px(c, 7.5, 10, 0.9, 0.8, "#ae4828");
  }
  return c;
}

static Image *wall_rubble_sprite(void) {
  Image *c = image_new(TEX, TEX);
  Lcg r = lcg(1881);
  px_c(c, 0.5, 14.4, 15, 1.6, faded("#221c17", 0.7));
  for (int i = 0; i < 22; i++) {
    double x = 1 + lcg_next(&r) * 12, y = 12.5 + lcg_next(&r) * 2.5;
    double w = 0.7 + lcg_next(&r) * 2.3, h = 0.4 + lcg_next(&r) * 1.3;
    px(c, x, y, w, h, i % 2 ? "#6b5e50" : "#87745f");
    px(c, x, y, w, 0.25, "#b29b7e");
    px(c, x + w - 0.3, y + 0.25, 0.3, h - 0.25, "#42392f");
  }
  return c;
}

static Image *wall_dust_sprite(int frame) {
  Image *c = image_new(TEX, TEX);
  Lcg r = lcg(1882);
  double t = (double)frame / (WALL_DUST_FRAMES - 1);
  for (int i = 0; i < 24; i++) {
    double x = 8 + (lcg_next(&r) - 0.5) * (5 + t * 10);
    double y = 10 + (lcg_next(&r) - 0.5) * 7 - t * 3;
    double size = 1 + lcg_next(&r) * 2 + t;
    px_c(c, x - size / 2, y - size / 2, size, size,
         faded(i % 2 ? "#ac957a" : "#776653", (0.7 - t * 0.6) * 0.65));
  }
  for (int i = 0; i < 12; i++) {
    double vx = (lcg_next(&r) - 0.5) * 11;
    double x = 8 + vx * t, y = 7 + lcg_next(&r) * 3 - t * 3 + t * t * 10;
    px_c(c, x, y, 0.4 + lcg_next(&r) * 0.6, 0.5, faded("#b19c7f", 1 - t));
  }
  return c;
}

static Image *explosion_sprite(int frame) {
  Image *c = image_new(TEX, TEX);
  Lcg r = lcg(1781);
  double t = (double)frame / (EXPLOSION_FRAMES - 1);
  // Chunky puffs grow out of a white-hot core and cool to drifting smoke.
  for (int i = 0; i < 22; i++) {
    double angle = lcg_next(&r) * M_PI * 2;
    double speed = 1.2 + lcg_next(&r) * 3.2;
    double size = 1.2 + lcg_next(&r) * 2;
    double x = 8 + cos(angle) * speed * (0.3 + t);
    double y = 9 + sin(angle) * speed * (0.3 + t) - t * 3;
    const char *color = t > 0.55 ? (i % 2 ? "#524b43" : "#746555")
                        : i % 3 == 0 ? "#d64b1d" : i % 3 == 1 ? "#ff9e27" : "#ffe28b";
    px_c(c, x - size / 2, y - size / 2, size, size, faded(color, 1 - t * 0.65));
  }
  if (frame < 3) {
    double size = 3 - frame * 0.6;
    px(c, 8 - size / 2, 9 - size / 2, size, size, "#fff5ce");
  }
  for (int i = 0; i < 12; i++) {
    double angle = lcg_next(&r) * M_PI * 2;
    double dist = 2 + t * (3 + lcg_next(&r) * 3);
    double x = 8 + cos(angle) * dist, y = 9 + sin(angle) * dist + t * t * 2;
    px_c(c, x, y, 0.35, 0.65, faded(i % 2 ? "#d99a48" : "#4d2e1a", 1 - t * 0.6));
  }
  return c;
}

// Furniture for the larger halls: a trestle table set with a candle and a
// pewter mug, and a plain oak chair. Both stand on the floor line so the
// renderer can drop them in like any other ground sprite.
static Image *table_sprite(void) {
  Image *c = image_new(TEX, TEX);
  // trestle legs with a lit edge, tied by a stretcher beam
  px(c, 2.2, 9.2, 1.6, 6.8, "#33220f");
  px(c, 12.2, 9.2, 1.6, 6.8, "#33220f");
  px(c, 2.2, 9.2, 0.6, 6.8, "#5a3a20");
  px(c, 12.2, 9.2, 0.6, 6.8, "#5a3a20");
  px(c, 3.8, 12.6, 8.4, 1.1, "#2a1c0c");
  // thick oak slab, top catching the torchlight
  px(c, 0.7, 7.4, 14.6, 2, "#6a4426");
  px(c, 0.7, 7.4, 14.6, 0.7, "#8a5c30");
  px(c, 0.7, 9, 14.6, 0.4, "#241708"); // shadow under the lip
  // candle in an iron holder
  px(c, 4.2, 7, 2, 0.5, "#2f3338");
  px(c, 4.7, 5.2, 1, 1.9, "#e8e0c8");
  px(c, 4.95, 4.3, 0.5, 0.9, "#ffd23c");
  px(c, 5.05, 4, 0.3, 0.5, "#fff6b0");
  // pewter mug
  px(c, 9.4, 5.9, 2, 1.7, "#565c66");
  px(c, 9.4, 5.9, 2, 0.4, "#8d949c");
  px(c, 11.4, 6.3, 0.6, 1, "#454b52");
  return c;
}

static Image *chair_sprite(void) {
  Image *c = image_new(TEX, TEX);
  // stiles: back legs running up into the backrest
  px(c, 4.6, 2.6, 1.2, 13.4, "#4a3018");
  px(c, 10.2, 2.6, 1.2, 13.4, "#4a3018");
  px(c, 4.6, 2.6, 0.4, 13.4, "#6a4426");
  // backrest slats
  px(c, 5.8, 3.4, 4.4, 1.1, "#5a3a20");
  px(c, 5.8, 5.6, 4.4, 1.1, "#5a3a20");
  // seat board
  px(c, 3.8, 8.6, 8.4, 1.6, "#6a4426");
  px(c, 3.8, 8.6, 8.4, 0.6, "#8a5c30");
  // front legs
  px(c, 4, 10.2, 1, 5.8, "#33220f");
  px(c, 11, 10.2, 1, 5.8, "#33220f");
  return c;
}

// Oak wardrobe faces, drawn by the renderer as a shallow box standing flush
// against a wall (front face plus two short sides, never a billboard).
// The renderer fogs the whole column rect, so both textures must be fully
// opaque edge to edge.
static void wardrobe_door(Image *g, double x) {
  // door leaf with recessed panels
  px(g, x, 1.6, 6.6, 13, "#5a3a20");
  px(g, x, 1.6, 6.6, 0.4, "#8a5c30"); // lit top rail
  px(g, x, 1.6, 0.4, 13, "#6a4426"); // lit outer stile
  px(g, x + 6.2, 1.6, 0.4, 13, "#33220f");
  px(g, x + 1.1, 2.9, 4.4, 4.6, "#3a2814");
  px(g, x + 1.1, 2.9, 4.4, 0.4, "#241708");
  px(g, x + 1.1, 7.2, 4.4, 0.3, "#7c5228");
  px(g, x + 1.1, 8.8, 4.4, 4.6, "#3a2814");
  px(g, x + 1.1, 8.8, 4.4, 0.4, "#241708");
  px(g, x + 1.1, 13.1, 4.4, 0.3, "#7c5228");
}

static Image *wardrobe_front_texture(void) {
  static const double hinges[] = {3.4, 11.8};
  Image *c = image_new(TEX, TEX);
  Lcg r = lcg(97531);
  px(c, 0, 0, 16, 16, "#3a2612"); // carcass
  // cornice and plinth
  px(c, 0, 0, 16, 1.4, "#241708");
  px(c, 0.3, 0.2, 15.4, 0.4, "#6a4426");
  px(c, 0, 14.6, 16, 1.4, "#241708");
  px(c, 0, 14.6, 16, 0.3, "#0f0903");
  // door leaves with recessed panels
  wardrobe_door(c, 0.9);
  wardrobe_door(c, 8.5);
  px(c, 7.7, 1.6, 0.6, 13, "#241708"); // gap between the leaves
  // strap hinges on the outer stiles
  for (int i = 0; i < COUNT(hinges); i++) {
    double y = hinges[i];
    px(c, 0.9, y, 1.8, 0.7, "#2f3338");
    px(c, 13.3, y, 1.8, 0.7, "#2f3338");
    px(c, 0.9, y, 1.8, 0.25, "#565c66");
    px(c, 13.3, y, 1.8, 0.25, "#565c66");
  }
  // iron pull rings either side of the gap
  px(c, 6.7, 7.5, 0.8, 1.5, "#2f3338");
  px(c, 8.5, 7.5, 0.8, 1.5, "#2f3338");
  px(c, 6.85, 7.65, 0.5, 0.5, "#8d949c");
  px(c, 8.65, 7.65, 0.5, 0.5, "#8d949c");
  speckle(c, &r, "#000", 70, 0.12);
  speckle(c, &r, "#c98d4c", 18, 0.05);
  return c;
}

static Image *wardrobe_side_texture(void) {
  static const double seams[] = {4, 8, 12};
  Image *c = image_new(TEX, TEX);
  Lcg r = lcg(24680);
  px(c, 0, 0, 16, 16, "#422b15"); // plain panelled flank
  for (int i = 0; i < COUNT(seams); i++) px(c, seams[i], 0, 0.35, 16, "#2a1c0c"); // plank seams
  px(c, 0, 0, 16, 1.4, "#241708"); // cornice
  px(c, 0.3, 0.2, 15.4, 0.4, "#5a3a20");
  px(c, 0, 14.6, 16, 1.4, "#241708"); // plinth
  // wood grain
  for (int i = 0; i < 44; i++) {
    Color k = color_alpha(shade("#6a4426", 0.6 + lcg_next(&r) * 0.7), 0.18f);
    int x = rnd_int(&r, TEX);
    int y = rnd_int(&r, TEX);
    int h = 2 + rnd_int(&r, 6);
    rect_c(c, x, y, 1, h, k);
  }
  speckle(c, &r, "#000", 50, 0.14);
  return c;
}

// Iron-bound treasure chest. Low enough to be looked down upon, so besides
// the front and side faces it also has a lid texture (u runs along the wall).
static Image *chest_front_texture(void) {
  static const double seams[] = {5.6, 10.8};
  static const double straps[] = {2.2, 12.6};
  Image *c = image_new(TEX, TEX);
  Lcg r = lcg(13579);
  px(c, 0, 0, 16, 16, "#4a3018"); // oak body
  for (int i = 0; i < COUNT(seams); i++) px(c, 0, seams[i], 16, 0.35, "#2a1c0c"); // plank seams
  px(c, 0, 0, 16, 0.8, "#6a4426"); // lit lid edge
  px(c, 0, 4.2, 16, 0.7, "#241708"); // lid seam
  px(c, 0, 4.9, 16, 0.3, "#7c5228");
  px(c, 0, 15, 16, 1, "#241708"); // base shadow
  // iron straps
  for (int i = 0; i < COUNT(straps); i++) {
    px(c, straps[i], 0, 1.2, 16, "#2f3338");
    px(c, straps[i], 0, 0.4, 16, "#565c66");
  }
  // lock plate with a keyhole
  px(c, 6.6, 3.2, 2.8, 3.8, "#2f3338");
  px(c, 6.6, 3.2, 2.8, 0.4, "#565c66");
  px(c, 7.6, 4.4, 0.8, 0.9, "#0f0903");
  px(c, 7.8, 5.2, 0.4, 1.1, "#0f0903");
  speckle(c, &r, "#000", 55, 0.12);
  speckle(c, &r, "#c98d4c", 14, 0.05);
  return c;
}

static Image *chest_side_texture(void) {
  static const double seams[] = {5.6, 10.8};
  Image *c = image_new(TEX, TEX);
  Lcg r = lcg(86420);
  px(c, 0, 0, 16, 16, "#422b15");
  for (int i = 0; i < COUNT(seams); i++) px(c, 0, seams[i], 16, 0.35, "#2a1c0c"); // planks
  px(c, 0, 0, 16, 0.8, "#5a3a20");
  px(c, 0, 4.2, 16, 0.7, "#241708"); // lid seam carries around
  px(c, 0, 15, 16, 1, "#241708");
  // single strap wrapping the flank
  px(c, 7.2, 0, 1.4, 16, "#2f3338");
  px(c, 7.2, 0, 0.5, 16, "#565c66");
  for (int i = 0; i < 30; i++) {
    Color k = color_alpha(shade("#6a4426", 0.6 + lcg_next(&r) * 0.7), 0.18f);
    int x = rnd_int(&r, TEX);
    int y = rnd_int(&r, TEX);
    int w = 2 + rnd_int(&r, 5);
    rect_c(c, x, y, w, 1, k);
  }
  speckle(c, &r, "#000", 45, 0.14);
  return c;
}

static Image *chest_lid_texture(void) {
  // plain flat colour: the lid's screen mapping is only approximate, and a
  // single tone can never show the distortion
  Image *c = image_new(TEX, TEX);
  px(c, 0, 0, 16, 16, "#553a1e");
  return c;
}

// ------------------------------------------------------------------ trophies

// Trophies for the walls (a long sword, a heater shield) and a suit of
// armour standing against the masonry. Drawn by the renderer as single flat
// quads with sprite-style distance shading, so they may keep transparency.
static Image *sword_sprite(void) {
  static const double wraps[] = {2.3, 3.3};
  static const double angles[] = {-0.55, 0.55};
  // one upright blade, composed twice as a crossed pair of trophies
  Image *blade = image_new(TEX, TEX);
  px(blade, 7, 0.6, 2, 1.2, "#c9a24a"); // pommel
  px(blade, 7.4, 1.8, 1.2, 2.6, "#4a3018"); // wrapped grip
  for (int i = 0; i < COUNT(wraps); i++) px(blade, 7.4, wraps[i], 1.2, 0.4, "#2a1c0c");
  px(blade, 5, 4.4, 6, 1.1, "#3a3f45"); // crossguard
  px(blade, 5, 4.4, 6, 0.4, "#565c66");
  px(blade, 7.2, 5.5, 1.6, 8.8, "#8d949c"); // blade, point down
  px(blade, 7.9, 5.5, 0.4, 8.8, "#c9ced4"); // fuller catching light
  px(blade, 7.5, 14.3, 1, 0.8, "#8d949c");
  px(blade, 7.7, 15.1, 0.6, 0.5, "#c9ced4"); // tip
  Image *c = image_new(TEX, TEX);
  for (int i = 0; i < COUNT(angles); i++) gfx_blit_rotated(c, blade, 32, 32, (float)angles[i]);
  image_free(blade);
  return c;
}

static Image *shield_sprite(void) {
  Image *c = image_new(TEX, TEX);
  // steel-rimmed heater shield, tapering to a point
  px(c, 3, 1.5, 10, 8, "#565c66");
  px(c, 4, 9.5, 8, 1.5, "#565c66");
  px(c, 5, 11, 6, 1.5, "#565c66");
  px(c, 6, 12.5, 4, 1, "#565c66");
  px(c, 7, 13.5, 2, 1, "#565c66");
  px(c, 3.8, 2.3, 8.4, 6.6, "#7a1f1f"); // red field
  px(c, 4.8, 8.9, 6.4, 1.6, "#7a1f1f");
  px(c, 5.8, 10.5, 4.4, 1.4, "#7a1f1f");
  px(c, 6.8, 11.9, 2.4, 1, "#7a1f1f");
  px(c, 7.2, 3, 1.6, 7, "#e9c93c"); // gold cross
  px(c, 5, 4.6, 6, 1.6, "#e9c93c");
  px(c, 3.8, 2.3, 1.6, 0.8, "#a84040"); // polished glint
  return c;
}

static Image *armor_sprite(void) {
  Image *c = image_new(TEX, TEX);
  px(c, 5.4, 14.9, 5.2, 1.1, "#241708"); // stand base
  px(c, 6.6, 0.9, 2.8, 2.4, "#565c66"); // helm
  px(c, 6.6, 0.9, 2.8, 0.5, "#8d949c");
  px(c, 6.9, 1.9, 2.2, 0.5, "#0d0b08"); // visor slit
  px(c, 4.9, 3.3, 6.2, 1.2, "#454b52"); // pauldrons
  px(c, 4.9, 3.3, 6.2, 0.4, "#6d747e");
  px(c, 5.8, 4.5, 4.4, 4.1, "#565c66"); // cuirass
  px(c, 5.8, 4.5, 1, 4.1, "#8d949c");
  px(c, 4.7, 4.5, 1, 3.8, "#454b52"); // arms hanging
  px(c, 10.3, 4.5, 1, 3.8, "#454b52");
  px(c, 4.6, 8.3, 1.2, 1.2, "#3a3f45"); // gauntlets
  px(c, 10.2, 8.3, 1.2, 1.2, "#3a3f45");
  px(c, 5.6, 8.6, 4.8, 1.6, "#3a3f45"); // faulds
  px(c, 5.6, 8.6, 4.8, 0.4, "#565c66");
  px(c, 6.1, 10.2, 1.3, 4.7, "#454b52"); // legs
  px(c, 8.6, 10.2, 1.3, 4.7, "#454b52");
  px(c, 6.1, 10.2, 0.4, 4.7, "#6d747e");
  px(c, 5.8, 14, 1.9, 0.9, "#3a3f45"); // sabatons
  px(c, 8.4, 14, 1.9, 0.9, "#3a3f45");
  return c;
}

// Display pieces for the wall niches: a weathered stone figure and a clay
// amphora, each on its own plinth. Billboards like the armour: the recess
// opening clips them per column through the z-buffer.
static Image *statue_sprite(void) {
  Image *c = image_new(TEX, TEX);
  const char *stone = "#8a857c";
  const char *lit = "#a8a39a";
  const char *dark = "#5f5b53";
  // plinth
  px(c, 4.6, 12.2, 6.8, 0.9, lit);
  px(c, 5, 13.1, 6, 2.3, stone);
  px(c, 5, 13.1, 0.8, 2.3, lit);
  px(c, 4.6, 15.1, 6.8, 0.9, dark);
  // robe falling to the plinth, flaring at the hem
  px(c, 6.2, 5.4, 3.6, 6.8, stone);
  px(c, 5.8, 10.6, 4.4, 1.6, stone);
  px(c, 6.2, 5.4, 0.9, 6.8, lit); // fold catching the torchlight
  px(c, 9, 5.8, 0.8, 6.4, dark); // fold in shadow
  px(c, 7.6, 6.6, 0.4, 5.6, dark); // crease down the middle
  // arms folded across the chest
  px(c, 6, 6.8, 4, 1.1, stone);
  px(c, 6, 6.8, 4, 0.4, lit);
  // head with a hood's shadow across the face
  px(c, 7, 3.2, 2.2, 2.2, stone);
  px(c, 6.7, 3, 2.8, 0.7, lit);
  px(c, 7.2, 4.1, 1.8, 0.7, dark);
  // chips and stains of age
  px(c, 6.6, 9.2, 0.5, 0.5, dark);
  px(c, 8.4, 12.6, 0.6, 0.4, dark);
  return c;
}

static Image *urn_sprite(void) {
  Image *c = image_new(TEX, TEX);
  const char *clay = "#9c5a30";
  const char *lit = "#c07a44";
  const char *dark = "#6e3d1e";
  // low plinth
  px(c, 4.8, 13.6, 6.4, 0.8, "#8a857c");
  px(c, 5.2, 14.4, 5.6, 1.2, "#5f5b53");
  // foot and belly of the amphora
  px(c, 7, 12.4, 2, 1.2, dark);
  px(c, 6, 11.6, 4, 0.9, clay);
  px(c, 5.2, 8, 5.6, 3.7, clay);
  px(c, 5.6, 7.2, 4.8, 0.9, clay);
  px(c, 5.2, 8, 1, 3.7, lit); // glaze catching the light
  px(c, 9.6, 8.2, 1, 3.4, dark);
  // painted band around the shoulder
  px(c, 5.6, 8.6, 4.8, 0.7, "#3a2a16");
  // neck and flared rim
  px(c, 7, 6, 2, 1.3, clay);
  px(c, 6.4, 5.2, 3.2, 0.9, lit);
  // handles arching from rim to shoulder
  px(c, 4.6, 6.4, 0.8, 2.2, dark);
  px(c, 5.2, 6, 1.2, 0.7, dark);
  px(c, 10.6, 6.4, 0.8, 2.2, dark);
  px(c, 9.6, 6, 1.2, 0.7, dark);
  return c;
}

// ------------------------------------------------------------------ candles, hearth

// Candles for the corners the torches never reach. The stands are drawn
// without their flames: the renderer lays a fullbright flame sprite (built in
// the same 16-unit grid, so it registers pixel-perfect at any scale) over the
// shaded stand, and the little fires keep glowing through the distance fog
// the way the key sparkle does.

#define WAX "#e8e0c8"
#define WAX_LIT "#f7f1dd"
#define WAX_DARK "#b8ae92"

// wrought-iron standing candelabrum: tripod foot, turned stem, a cross arm
// carrying two sconces and a third candle riding the stem top
static Image *candelabra_sprite(void) {
  Image *c = image_new(TEX, TEX);
  const char *iron = "#34373a";
  const char *lit = "#6a6f75";
  const char *dark = "#1d1f21";
  // tripod feet splaying from the stem
  px(c, 5.4, 15.2, 5.2, 0.8, dark);
  px(c, 6.2, 14.3, 3.6, 1, iron);
  px(c, 6.2, 14.3, 3.6, 0.3, lit);
  // stem with a turned knop at the waist
  px(c, 7.6, 4.7, 0.8, 9.8, iron);
  px(c, 7.6, 4.7, 0.3, 9.8, lit);
  px(c, 7.1, 10.2, 1.8, 1, iron);
  px(c, 7.1, 10.2, 1.8, 0.3, lit);
  // cross arm and the risers up to the outer sconces
  px(c, 4.1, 7, 7.8, 0.7, iron);
  px(c, 4.1, 7, 7.8, 0.25, lit);
  px(c, 4.65, 6, 0.5, 1, iron);
  px(c, 10.85, 6, 0.5, 1, iron);
  // drip pans, each catching a rim of light
  px(c, 4, 5.5, 1.8, 0.6, iron);
  px(c, 4, 5.5, 1.8, 0.22, lit);
  px(c, 10.4, 5.5, 1.8, 0.6, iron);
  px(c, 10.4, 5.5, 1.8, 0.22, lit);
  px(c, 7.1, 4.2, 1.8, 0.6, iron);
  px(c, 7.1, 4.2, 1.8, 0.22, lit);
  // candles, burnt to different heights, wax running over the pans
  px(c, 4.45, 3.6, 0.9, 1.9, WAX);
  px(c, 4.45, 3.6, 0.3, 1.9, WAX_LIT);
  px(c, 5.05, 4, 0.3, 1.5, WAX_DARK);
  px(c, 4.25, 4.9, 0.35, 1, WAX);
  px(c, 7.55, 2.3, 0.9, 1.9, WAX);
  px(c, 7.55, 2.3, 0.3, 1.9, WAX_LIT);
  px(c, 8.15, 2.7, 0.3, 1.5, WAX_DARK);
  px(c, 10.85, 3.9, 0.9, 1.6, WAX);
  px(c, 10.85, 3.9, 0.3, 1.6, WAX_LIT);
  px(c, 11.45, 4.3, 0.3, 1.2, WAX_DARK);
  px(c, 11.7, 5, 0.35, 0.9, WAX);
  return c;
}

// where each candelabrum flame sits, in the sprite's 16-unit grid
static const double CANDELABRA_FLAMES[][2] = {{4.9, 3.6}, {8, 2.3}, {11.3, 3.9}};

// a cluster of half-melted votive candles on a stone slab, for the niches
static Image *candle_cluster_sprite(void) {
  // stubs of different heights: x, top, width
  static const double stubs[][3] = {{4.9, 10.6, 1.3}, {6.6, 9.4, 1.5}, {8.5, 11.2, 1.2}, {10.1, 10, 1.4}};
  Image *c = image_new(TEX, TEX);
  // stone slab like the urn's plinth
  px(c, 3.8, 13.8, 8.4, 0.8, "#8a857c");
  px(c, 4.2, 14.6, 7.6, 1, "#5f5b53");
  // each stub with a lit edge and a shaded side
  for (int i = 0; i < COUNT(stubs); i++) {
    double x = stubs[i][0], top = stubs[i][1], w = stubs[i][2];
    px(c, x, top, w, 13.8 - top, WAX);
    px(c, x, top, 0.35, 13.8 - top, WAX_LIT);
    px(c, x + w - 0.3, top + 0.3, 0.3, 13.5 - top, WAX_DARK);
    px(c, x - 0.15, top + 0.4, 0.35, 1.1, WAX); // wax run down the side
  }
  // pooled wax spilling over the slab edge
  px(c, 5.6, 13.5, 4.6, 0.4, WAX);
  px(c, 7.2, 14.6, 0.5, 0.8, WAX_DARK);
  return c;
}

static const double CANDLE_CLUSTER_FLAMES[][2] = {{5.55, 10.6}, {7.35, 9.4}, {9.1, 11.2}, {10.8, 10}};

// the flames alone, one image per animation frame; neighbouring candles
// flicker out of step so the group never pulses in unison
static Image *candle_flames_sprite(int frame, const double (*spots)[2], int count) {
  static const double sways[] = {-0.1, 0.1, 0.15, -0.05};
  static const double tips[] = {0.15, -0.25, 0, -0.1};
  Image *c = image_new(TEX, TEX);
  for (int i = 0; i < count; i++) {
    double x = spots[i][0], y = spots[i][1];
    int f = (frame + i) & 3;
    double sway = sways[f];
    double tip = tips[f];
    px(c, x - 0.35 + sway, y - 1.05 + tip, 0.7, 1.05 - tip, "#ff9f16");
    px(c, x - 0.2 + sway, y - 0.7, 0.4, 0.7, "#ffe36a");
    px(c, x - 0.15 + sway * 0.5, y - 0.35, 0.3, 0.35, "#fff6b0");
  }
  return c;
}

// A stone fireplace set into the masonry: mantel shelf, jambs, and a
// firebox with logs stacked on iron dogs. Rendered like the wall hangings,
// one flat quad on the wall face spanning the lower part of the wall, so
// it hugs the masonry from every angle instead of turning with the camera.
// Drawn cold: the renderer lays the animated blaze over the same quad
// fullbright, the trick the candle stands use.
static Image *fireplace_sprite(void) {
  Image *c = image_new(TEX, TEX);
  const char *stone = "#6b6157";
  const char *lit = "#8a8074";
  const char *dark = "#443e36";
  const char *mortar = "#37322b";
  // mantel shelf spanning the full breast, top edge catching the light
  px(c, 0.8, 0, 14.4, 1.8, stone);
  px(c, 0.8, 0, 14.4, 0.6, lit);
  px(c, 0.8, 1.2, 14.4, 0.6, dark);
  // jambs framing the firebox
  px(c, 1.6, 1.8, 2.6, 12.4, stone);
  px(c, 1.6, 1.8, 0.7, 12.4, lit);
  px(c, 11.8, 1.8, 2.6, 12.4, stone);
  px(c, 13.7, 1.8, 0.7, 12.4, dark);
  px(c, 1.6, 5.8, 2.6, 0.4, mortar);
  px(c, 1.6, 10, 2.6, 0.4, mortar);
  px(c, 11.8, 5.8, 2.6, 0.4, mortar);
  px(c, 11.8, 10, 2.6, 0.4, mortar);
  // firebox: sooted back wall, blackest at the throat
  px(c, 4.2, 1.8, 7.6, 12.4, "#100c09");
  px(c, 4.2, 1.8, 7.6, 1.6, "#030202");
  // iron fire dogs carrying the logs
  px(c, 5, 11, 0.7, 3.2, "#1d1f21");
  px(c, 10.3, 11, 0.7, 3.2, "#1d1f21");
  px(c, 4.7, 10.5, 1.3, 0.6, "#34373a");
  px(c, 10, 10.5, 1.3, 0.6, "#34373a");
  // logs stacked across the dogs, sawn ends pale
  px(c, 4.6, 12.2, 6.8, 1.4, "#3c2417");
  px(c, 4.6, 12.2, 6.8, 0.45, "#68401f");
  px(c, 5.3, 11, 5.4, 1.3, "#4a2c1a");
  px(c, 5.3, 11, 5.4, 0.4, "#7a4c24");
  px(c, 4.6, 12.2, 0.8, 1.4, "#9a6630");
  px(c, 10.6, 12.2, 0.8, 1.4, "#9a6630");
  // hearthstone slab proud of the floor
  px(c, 0.8, 14.2, 14.4, 1.8, "#5f5b53");
  px(c, 0.8, 14.2, 14.4, 0.6, "#8a857c");
  return c;
}

// the blaze alone, one image per frame; three tongues of flame licking up
// between the logs out of step, over an ember bed that breathes with them
static Image *fireplace_flames_sprite(int frame) {
  static const double glows[] = {0.85, 1, 0.9, 0.75};
  static const double tongues_x[] = {5.7, 7.9, 10};
  static const double tongues_h[] = {3.6, 4.8, 3.1};
  static const double sways[] = {-0.3, 0.3, 0.45, -0.2};
  static const double flicker[] = {0.5, -0.6, 0.15, -0.25};
  Image *c = image_new(TEX, TEX);
  double glow = glows[frame];
  px_c(c, 4.8, 13.3, 6.4, 0.9, faded("#e94b0c", 0.8 * glow));
  px_c(c, 6, 13.5, 4, 0.7, faded("#ff9f16", 0.8 * glow));
  const double base = 11.6;
  for (int i = 0; i < 3; i++) {
    double x = tongues_x[i];
    int f = (frame + i) & 3;
    double sway = sways[f];
    double h = tongues_h[i] + flicker[f];
    px(c, x - 1 + sway * 0.4, base - h, 2, h, "rgba(120,25,4,0.65)");
    px(c, x - 0.65 + sway * 0.7, base - h * 0.8, 1.3, h * 0.8, "#e94b0c");
    px(c, x - 0.4 + sway, base - h * 0.55, 0.8, h * 0.55, "#ff9f16");
    px(c, x - 0.25 + sway, base - h * 0.3, 0.5, h * 0.3, "#ffe36a");
    px(c, x - 0.15 + sway * 0.5, base - h * 0.14, 0.3, h * 0.14, "#fff6b0");
  }
  return c;
}

// ------------------------------------------------------------------ chains

// Iron chains for the gloomier corners: a pair hung on the masonry ending
// in a shackle and a ring, and a chain bolted to the ceiling with a heavy
// hook. Links alternate face-on and edge-on; the face-on holes are cleared
// so the wall (or the dark) shows through.
static void chain_strand(Image *g, double x, double y0, double y1, double sway) {
  int i = 0;
  for (double y = y0; y < y1; y += 1.2, i++) {
    double cx = x + (sway != 0 ? sin(i * 1.9) * sway : 0);
    if (i & 1) {
      px(g, cx - 0.2, y, 0.4, 1.5, "#31363c"); // edge-on link
      px(g, cx - 0.2, y, 0.4, 0.5, "#6d747e");
    } else {
      px(g, cx - 0.55, y, 1.1, 1.6, "#3a3f45"); // face-on link
      px(g, cx - 0.55, y, 0.35, 1.6, "#565c66");
      clear(g, (cx - 0.2) * 4, (y + 0.45) * 4, 0.4 * 4, 0.75 * 4);
    }
  }
}

static Image *chain_wall_sprite(void) {
  static const double mounts[] = {4.5, 11};
  Image *c = image_new(TEX, TEX);
  // ring plates bolted to the stone
  for (int i = 0; i < COUNT(mounts); i++) {
    double mx = mounts[i];
    px(c, mx - 1, 0.6, 2, 1, "#2c3136");
    px(c, mx - 1, 0.6, 2, 0.35, "#565c66");
    px(c, mx - 0.35, 1.6, 0.7, 0.8, "#3a3f45");
  }
  chain_strand(c, 4.5, 2.4, 10.6, 0.35);
  chain_strand(c, 11, 2.4, 7.2, 0.3);
  // the long chain ends in an open shackle
  px(c, 3.3, 10.8, 2.4, 0.6, "#454b52");
  px(c, 3.3, 10.8, 2.4, 0.25, "#6d747e");
  px(c, 3.3, 11.4, 0.6, 1.6, "#454b52");
  px(c, 5.1, 11.4, 0.6, 1.6, "#454b52");
  px(c, 3.3, 13, 0.75, 0.5, "#31363c"); // open jaws
  px(c, 4.95, 13, 0.75, 0.5, "#31363c");
  // the short one in a dangling ring
  px(c, 10.1, 7.3, 1.8, 1.8, "#3a3f45");
  px(c, 10.1, 7.3, 1.8, 0.4, "#565c66");
  clear(c, 10.55 * 4, 7.75 * 4, 0.9 * 4, 0.9 * 4);
  // a touch of rust where rain never reaches
  Lcg r = lcg(913);
  Color rust = faded("#6a4426", 0.3);
  for (int i = 0; i < 12; i++) {
    int x = (int)floor(12 + lcg_next(&r) * 14);
    int y = (int)floor(10 + lcg_next(&r) * 40);
    rect_c(c, x, y, 2, 2, rust);
  }
  return c;
}

static Image *chain_ceiling_sprite(void) {
  Image *c = image_new(TEX, TEX);
  // mounting plate flush with the ceiling
  px(c, 6.4, 0, 3.2, 0.7, "#2c3136");
  px(c, 6.4, 0, 3.2, 0.25, "#565c66");
  chain_strand(c, 8, 0.7, 11.4, 0.25);
  // heavy meat hook at the end
  px(c, 7.65, 11.3, 0.7, 1.7, "#454b52"); // shank
  px(c, 7.65, 11.3, 0.25, 1.7, "#6d747e");
  px(c, 6.5, 12.5, 1.6, 0.7, "#454b52"); // curl of the hook
  px(c, 6.5, 11.6, 0.65, 1.2, "#454b52"); // tip rising back up
  px(c, 6.5, 11.6, 0.65, 0.4, "#8d949c"); // worn point catches light
  // a shorter strand swings beside it, ending in a bare ring
  px(c, 10.9, 0, 1.7, 0.55, "#2c3136");
  chain_strand(c, 11.7, 0.55, 5.6, 0.2);
  px(c, 10.9, 5.5, 1.6, 1.6, "#3a3f45");
  px(c, 10.9, 5.5, 1.6, 0.35, "#565c66");
  clear(c, 11.3 * 4, 5.95 * 4, 0.8 * 4, 0.8 * 4);
  return c;
}

// ------------------------------------------------------------------ cobwebs, cracks

#define WEB_COLOR "#d8d3c6"

// one thread texel; overlapping dots build up
static void web_dot(Image *g, double x, double y, double alpha) {
  rect_c(g, floor(x), floor(y), 1, 1, faded(WEB_COLOR, alpha));
}

static void web_line(Image *g, double x0, double y0, double x1, double y1, double alpha) {
  int steps = (int)fmax(1, ceil(fmax(fabs(x1 - x0), fabs(y1 - y0))));
  for (int i = 0; i <= steps; i++) web_dot(g, x0 + ((x1 - x0) * i) / steps, y0 + ((y1 - y0) * i) / steps, alpha);
}

// Cobwebs for the corners no chatelaine has dusted in years: anchor threads
// fan out of the top corner of the wall face, sagging cross-strands ring
// them, and a torn strand drifts off the rim. Drawn as quantized 1px dots so
// the threads stay crisp pixel-art like everything else; one weave keeps its
// weaver dangling on a dropline. The web hangs in the top-left corner; the
// right-hand variant is its mirror image.
static Image *cobweb_sprite(uint32_t seed, bool spider) {
  Image *c = image_new(TEX, TEX);
  Lcg r = lcg(seed);
  // anchor threads fan out of the top corner
  struct {
    double a, len;
  } spokes[6];
  int n = 5 + rnd_int(&r, 2);
  for (int i = 0; i < n; i++) {
    double a = ((i + 0.5 + (lcg_next(&r) - 0.5) * 0.35) / n) * (M_PI / 2);
    double len = 30 + lcg_next(&r) * 20;
    spokes[i].a = a;
    spokes[i].len = len;
    web_line(c, 0, 0, cos(a) * len, sin(a) * len, 0.5);
  }
  // cross-strands between neighbouring spokes, sagging toward the corner
  for (double rr = 9; rr <= 42; rr += 7 + lcg_next(&r) * 5) {
    for (int i = 0; i + 1 < n; i++) {
      double a0 = spokes[i].a, a1 = spokes[i + 1].a;
      if (rr > spokes[i].len || rr > spokes[i + 1].len) continue;
      double am = (a0 + a1) / 2;
      double cx = cos(am) * rr * 0.82;
      double cy = sin(am) * rr * 0.82;
      double lx = cos(a0) * rr;
      double ly = sin(a0) * rr;
      for (int t = 1; t <= 6; t++) {
        double tt = t / 6.0;
        double mt = 1 - tt;
        double qx = mt * mt * cos(a0) * rr + 2 * mt * tt * cx + tt * tt * cos(a1) * rr;
        double qy = mt * mt * sin(a0) * rr + 2 * mt * tt * cy + tt * tt * sin(a1) * rr;
        web_line(c, lx, ly, qx, qy, 0.38);
        lx = qx;
        ly = qy;
      }
    }
  }
  // a torn strand dangles off the outer rim
  double ex = cos(spokes[n - 2].a) * spokes[n - 2].len;
  double ey = sin(spokes[n - 2].a) * spokes[n - 2].len;
  double tx = ex + 3 + lcg_next(&r) * 3;
  double ty = ey + 8 + lcg_next(&r) * 6;
  web_line(c, ex, ey, tx, ty, 0.3);
  if (spider) {
    // the weaver herself, low on a dropline of her own
    int sx = (int)floor(11 + lcg_next(&r) * 6);
    int sy = (int)floor(30 + lcg_next(&r) * 8);
    web_line(c, sx, 6, sx, sy, 0.35);
    rect(c, sx - 1, sy, 2, 2, "#241f18"); // body
    rect(c, sx - 3, sy, 1, 1, "#241f18"); // legs
    rect(c, sx - 2, sy - 1, 1, 1, "#241f18");
    rect(c, sx + 2, sy, 1, 1, "#241f18");
    rect(c, sx + 1, sy - 1, 1, 1, "#241f18");
  }
  return c;
}

typedef struct {
  int x, y;
} Texel;

// Paints a fissure along a path of texels: hairline at both ends, dark in
// the middle with a lit edge; `wide` doubles the middle stretch.
static void carve(Image *g, const Texel *path, int count, bool wide) {
  for (int i = 0; i < count; i++) {
    double t = (double)i / maxi(1, count - 1);
    bool hairline = t < 0.1 || t > 0.88;
    rect_c(g, path[i].x, path[i].y, wide && t > 0.3 && t < 0.65 ? 2 : 1, 1, rgba(12, 9, 6, hairline ? 0.45f : 0.8f));
    if (!hairline) rect(g, path[i].x - 1, path[i].y, 1, 1, "rgba(235,225,205,0.15)"); // lit edge
  }
}

// A settling crack in the masonry: a dark fissure wandering down the wall
// face, hairline at both ends, its lit edge catching the torchlight. With
// `spall` a chunk of the facing has broken away around the fissure, and its
// rubble lies at the foot of the wall. Transparent everywhere else: the
// renderer lays it over whatever wall the level put it on.
static Image *crack_sprite(uint32_t seed, bool spall) {
  Image *c = image_new(TEX, TEX);
  Lcg r = lcg(seed);

  // the fissure steps down the face the way settling cracks follow the
  // joints: near-vertical drops with the odd sideways jog along a course
  Texel pts[TEX * 6];
  int count = 0;
  int bottom = 46 + rnd_int(&r, TEX - 46);
  double x = 18 + lcg_next(&r) * 28;
  int drift = lcg_next(&r) < 0.5 ? -1 : 1;
  for (int y = 0; y < bottom; y++) {
    pts[count++] = (Texel){(int)floor(x), y};
    if (lcg_next(&r) < 0.16 && y > 4 && y < bottom - 6) {
      int dir = lcg_next(&r) < 0.7 ? drift : -drift;
      int run = 2 + rnd_int(&r, 4);
      for (int i = 0; i < run; i++) {
        x = fmax(4, fmin(TEX - 5, x + dir));
        pts[count++] = (Texel){(int)floor(x), y};
      }
    }
    x = fmax(4, fmin(TEX - 5, x + (lcg_next(&r) - 0.5) * 1.1));
  }
  carve(c, pts, count, true);

  // a side branch forks off partway down and peters out
  Texel fork = pts[8 + rnd_int(&r, count / 2.0)];
  int dir = lcg_next(&r) < 0.5 ? -1 : 1;
  int len = 8 + rnd_int(&r, 8);
  Texel path[16];
  double bx = fork.x;
  double by = fork.y;
  for (int i = 0; i < len; i++) {
    path[i] = (Texel){(int)floor(bx), (int)floor(by)};
    bx = fmax(2, fmin(TEX - 3, bx + dir * (0.4 + lcg_next(&r) * 0.8)));
    by += 0.5 + lcg_next(&r) * 0.5;
  }
  carve(c, path, len, false);

  if (spall) {
    // the spalled patch: facing knocked away in irregular flakes around the
    // fissure, rough fill showing inside, a pale broken rim up top
    Texel s = pts[18 + rnd_int(&r, 18)];
    Color hole = color_parse("rgba(14,11,8,0.82)");
    rect_c(c, s.x - 2, s.y - 1, 5, 3, hole);
    rect_c(c, s.x - 1 + rnd_int(&r, 2), s.y + 2, 4, 2, hole);
    rect_c(c, s.x - 3, s.y + rnd_int(&r, 2), 2, 2, hole);
    for (int i = 0; i < 4; i++) {
      int fx = s.x - 2 + rnd_int(&r, 5);
      int fy = s.y + rnd_int(&r, 3);
      rect(c, fx, fy, 2, 1, "rgba(66,55,43,0.55)");
    }
    rect(c, s.x - 3, s.y - 2, 2, 1, "rgba(235,225,205,0.2)");
    rect(c, s.x + 1, s.y - 3, 3, 1, "rgba(235,225,205,0.2)");
    // what fell lies where it landed: rubble along the skirting
    int fx = pts[count - 1].x;
    for (int i = 0; i < 4; i++) {
      int rx = maxi(2, mini(TEX - 6, fx - 7 + rnd_int(&r, 14)));
      int rw = 2 + rnd_int(&r, 2);
      int ry = TEX - 3 - rnd_int(&r, 2);
      rect(c, rx, ry, rw, 2, i % 2 ? "#514a3f" : "#645c4f");
      rect(c, rx, ry, rw, 1, "rgba(255,255,255,0.1)");
      rect(c, rx, mini(TEX - 1, ry + 2), rw, 1, "rgba(0,0,0,0.4)");
    }
  }
  return c;
}

// ------------------------------------------------------------------ portraits

// Framed portraits of the fortress's former masters. Each sitter is painted
// once per gaze: PORTRAIT_COLS steps sweep the pupils from left to right and
// the second row drops them toward the floor, so the renderer can always
// pick the frame that looks straight at the player. Everything except the
// pupils is identical across the frames (the speckle rng reseeds per sitter,
// not per frame), so a change of gaze never makes the paint shimmer.
typedef struct Sitter {
  const char *skin, *skin_shade, *hair, *hair_dark, *cloth, *cloth_lit, *trim, *iris;
  bool beard, chain, veil, pendant, scar;
} Sitter;

static const Sitter SITTERS[PORTRAIT_SITTERS] = {
  // the old lord: grey mane, full beard, wine doublet and a chain of office
  {"#c99b70", "#a2764e", "#8d8478", "#5f5b53", "#6d1a1a", "#8c2a2a", "#c9a24a", "#2e2018", .beard = true,
   .chain = true},
  // the lady: pale under a dark hood, sapphire gown, a drop pendant
  {"#d8b090", "#b0875f", "#2e2620", "#191410", "#26364e", "#35486a", "#a8b0b8", "#2a3a28", .veil = true,
   .pendant = true},
  // the steward: cropped dark hair, green doublet, an old scar
  {"#b98d62", "#8f6a42", "#3a2c1a", "#241a0e", "#2c3c24", "#3c5230", "#8d949c", "#241c14", .scar = true},
};

static Image *portrait_sprite(const Sitter *pal, double gx, double gy, uint32_t seed) {
  static const double eyes[] = {6.25, 9.05};
  static const double bosses[][2] = {{0.15, 0.15}, {14.45, 0.15}, {0.15, 14.45}, {14.45, 14.45}};
  Image *c = image_new(TEX, TEX);
  Lcg r = lcg(seed);

  // the painting itself: old varnish, murkier toward the frame
  px(c, 0, 0, 16, 16, "#241b10");
  px(c, 1.7, 1.7, 12.6, 12.6, "#3a2c1a");
  px(c, 2.4, 2.2, 11.2, 7, "#463623");

  // the hood falls behind the shoulders before the figure goes on
  if (pal->veil) px(c, 4.4, 2.8, 7.2, 8.6, pal->hair_dark);

  // throat, then shoulders over its base
  px(c, 7, 9.7, 2, 2.2, pal->skin);
  px(c, 8.3, 9.7, 0.7, 2.2, pal->skin_shade);
  px(c, 3.6, 11.6, 8.8, 3.1, pal->cloth);
  px(c, 3.6, 11.6, 8.8, 0.6, pal->cloth_lit);
  px(c, 6.7, 11.2, 2.6, 0.5, pal->trim); // collar

  // head, jaw, the shadowed side of the face
  px(c, 5.4, 3.8, 5.2, 6, pal->skin);
  px(c, 5.9, 9.3, 4.2, 0.8, pal->skin);
  px(c, 9.9, 4.2, 0.7, 5.4, pal->skin_shade);

  if (pal->beard) {
    px(c, 5.1, 3.2, 5.8, 1.3, pal->hair); // swept-back mane
    px(c, 5, 4.3, 0.8, 2.6, pal->hair);
    px(c, 10.2, 4.3, 0.8, 2.6, pal->hair);
    px(c, 6.2, 3.4, 1.4, 0.5, pal->hair_dark);
  } else if (pal->veil) {
    px(c, 5.2, 3.4, 5.6, 1.2, pal->hair); // parted beneath the hood
    px(c, 7.85, 3.4, 0.3, 1.1, pal->hair_dark);
    px(c, 5.1, 4.2, 0.9, 4.6, pal->hair);
    px(c, 10, 4.2, 0.9, 4.6, pal->hair);
  } else {
    px(c, 5.2, 3.2, 5.6, 1.5, pal->hair); // cropped
    px(c, 5.1, 4.4, 0.6, 1.6, pal->hair);
    px(c, 10.3, 4.4, 0.6, 1.6, pal->hair);
  }

  // brows: the steward's knit heavier
  double brow_h = pal->scar ? 0.6 : 0.45;
  px(c, 5.85, 5.65, 1.5, brow_h, pal->hair_dark);
  px(c, 8.65, 5.65, 1.5, brow_h, pal->hair_dark);

  // eyes: lid line, whites, and the pupils cast toward the onlooker
  px(c, 5.85, 6.15, 1.5, 0.25, pal->skin_shade);
  px(c, 8.65, 6.15, 1.5, 0.25, pal->skin_shade);
  px(c, 5.85, 6.35, 1.5, 1, "#ddd2b8");
  px(c, 8.65, 6.35, 1.5, 1, "#ddd2b8");
  double ox = gx * 0.38;
  double oy = gy * 0.15;
  for (int i = 0; i < COUNT(eyes); i++) {
    double ex = eyes[i];
    px(c, ex + ox, 6.5 + oy, 0.7, 0.65, pal->iris);
    px(c, ex + 0.15 + ox, 6.6 + oy, 0.35, 0.35, "#0d0906");
    px(c, ex + 0.5 + ox, 6.55 + oy, 0.18, 0.18, "#e8e0d0"); // catchlight
  }

  // nose and mouth
  px(c, 7.7, 6.9, 0.5, 1.7, pal->skin_shade);
  px(c, 7.4, 8.4, 1.1, 0.4, pal->skin_shade);
  if (pal->beard) {
    px(c, 5.8, 9.1, 4.4, 2.3, pal->hair);
    px(c, 6.6, 11.3, 2.8, 0.9, pal->hair);
    px(c, 6.6, 8.7, 2.8, 0.6, pal->hair); // moustache
    px(c, 7.1, 9.4, 1.8, 0.3, "#3a241c"); // the mouth lost in it
    px(c, 6.4, 9.8, 0.5, 1.6, pal->hair_dark);
    px(c, 9, 9.8, 0.5, 1.6, pal->hair_dark);
  } else {
    px(c, 6.9, 9, 2.2, 0.45, "#8a5644");
    px(c, 7.1, 9.45, 1.8, 0.25, pal->skin_shade);
  }
  if (pal->scar) px(c, 9.55, 7.4, 0.35, 1.8, "#8a5a48");
  if (pal->chain) {
    for (int i = 0; i < 6; i++) px(c, 4.6 + i * 1.2, 12.4 + (i % 2) * 0.35, 0.7, 0.5, pal->trim);
  }
  if (pal->pendant) px(c, 7.7, 12.2, 0.6, 0.8, pal->trim);

  // age: craquelure specks over the paint, never over the frame
  speckle(c, &r, "#000", 50, 0.14);
  speckle(c, &r, "#e8d8a8", 12, 0.05);

  // gilded frame, lit from above, with a nameplate on the bottom rail
  px(c, 0, 0, 16, 1.3, "#9a7930");
  px(c, 0, 14.7, 16, 1.3, "#9a7930");
  px(c, 0, 0, 1.3, 16, "#9a7930");
  px(c, 14.7, 0, 1.3, 16, "#9a7930");
  px(c, 0, 0, 16, 0.5, "#c9a24a");
  px(c, 0, 0, 0.5, 16, "#c9a24a");
  px(c, 0, 15.5, 16, 0.5, "#6b5320");
  px(c, 15.5, 0, 0.5, 16, "#6b5320");
  px(c, 1.3, 1.3, 13.4, 0.4, "rgba(0,0,0,0.45)"); // lip shadow on the paint
  px(c, 1.3, 1.3, 0.4, 13.4, "rgba(0,0,0,0.45)");
  for (int i = 0; i < COUNT(bosses); i++) {
    double bx = bosses[i][0], by = bosses[i][1];
    px(c, bx, by, 1.4, 1.4, "#6b5320");
    px(c, bx + 0.25, by + 0.25, 0.9, 0.9, "#c9a24a");
  }
  px(c, 6.2, 14.95, 3.6, 0.8, "#6b5320");
  px(c, 6.4, 15.1, 3.2, 0.5, "#c9a24a");
  return c;
}

// ------------------------------------------------------------------ HUD crest

// one cell of the crest's 16x16 grid, 3x3 texels
static void cell(Image *g, double x, double y, double w, double h, const char *color) {
  rect(g, x * 3, y * 3, w * 3, h * 3, color);
}

static void crest_crack(Image *g, const int (*cells)[4], int count) {
  for (int i = 0; i < count; i++) cell(g, cells[i][0], cells[i][1], cells[i][2], cells[i][3], "#241209");
}

static void crest_chip(Image *g, double x, double y, double w, double h) { clear(g, x * 3, y * 3, w * 3, h * 3); }

// Heraldic shield that takes damage as the player does.
static Image *crest_sprite(CrestState state) {
  Image *c = image_new(CREST_SIZE, CREST_SIZE);
  bool dead = state == CREST_DEAD;
  const char *field = dead ? "#5a5148" : "#8c1a1a";
  const char *edge = dead ? "#7a7268" : "#c9a24a";
  const char *emblem = dead ? "#8d8478" : "#e9c93c";
  // shield: border then field, tapering to a point
  cell(c, 2, 1, 12, 9, edge);
  cell(c, 3, 10, 10, 1, edge);
  cell(c, 4, 11, 8, 1, edge);
  cell(c, 5, 12, 6, 1, edge);
  cell(c, 6, 13, 4, 1, edge);
  cell(c, 7, 14, 2, 1, edge);
  cell(c, 3, 2, 10, 8, field);
  cell(c, 4, 10, 8, 1, field);
  cell(c, 5, 11, 6, 1, field);
  cell(c, 6, 12, 4, 1, field);
  cell(c, 7, 13, 2, 1, field);
  // gold cross
  cell(c, 7, 3, 2, 8, emblem);
  cell(c, 5, 5, 6, 2, emblem);

  static const int nick[][4] = {{10, 3, 1, 1}, {11, 4, 1, 1}, {10, 5, 1, 1}};
  static const int split_left[][4] = {{5, 1, 1, 2}, {6, 3, 1, 2}, {5, 5, 1, 2}, {6, 7, 1, 2}};
  static const int long_right[][4] = {{10, 2, 1, 2}, {11, 4, 1, 1}, {10, 5, 1, 2}, {9, 7, 1, 2}};
  static const int long_left[][4] = {{5, 1, 1, 2}, {6, 3, 1, 2}, {5, 5, 1, 2}, {6, 7, 1, 2}, {5, 9, 1, 2}};
  static const int point[][4] = {{7, 11, 1, 2}, {8, 13, 1, 1}};
  static const int halves[][4] = {{7, 1, 1, 3}, {8, 4, 1, 3}, {7, 7, 1, 3}, {8, 10, 1, 3}, {7, 13, 1, 2}};
  switch (state) {
  case CREST_HEALTHY:
    cell(c, 3, 2, 2, 1, "#ffe08a"); // polished glint
    break;
  case CREST_OK:
    crest_crack(c, nick, COUNT(nick));
    break;
  case CREST_HURT:
    crest_crack(c, nick, COUNT(nick));
    crest_crack(c, split_left, COUNT(split_left));
    crest_chip(c, 2, 1, 2, 2); // chipped corner
    break;
  case CREST_BAD:
    crest_crack(c, long_right, COUNT(long_right));
    crest_crack(c, long_left, COUNT(long_left));
    crest_crack(c, point, COUNT(point));
    crest_chip(c, 2, 1, 2, 3);
    crest_chip(c, 12, 1, 2, 2);
    crest_chip(c, 7, 14, 2, 1);
    break;
  case CREST_DEAD:
    // split in two
    crest_crack(c, halves, COUNT(halves));
    crest_chip(c, 2, 1, 3, 2);
    crest_chip(c, 11, 1, 3, 2);
    break;
  case CREST_COUNT:
    break;
  }
  return c;
}

// ------------------------------------------------------------------ assemble

void assets_build(Assets *a) {
  memset(a, 0, sizeof(*a));

  a->walls[WT_STONE] = stone_texture(11, STONE_BASE, false);
  a->walls[WT_MOSS] = stone_texture(22, STONE_BASE, true);
  a->walls[WT_TIMBER] = timber_texture(33);
  a->walls[WT_DUNGEON] = dungeon_texture(44);
  a->walls[WT_BRICK] = brick_texture("#8f4f2c", "#3a2c20", 55);
  a->walls[WT_BANNER] = banner_texture(66);
  a->walls[WT_SLIT] = arrow_slit_texture(77);
  a->walls[WT_BARS] = bars_texture(false);
  a->walls[WT_DOOR] = door_texture();
  a->walls[WT_BAR_DOOR] = bars_texture(true);
  a->walls[WT_GATE] = gate_texture();
  a->walls[WT_LEVER] = lever_texture(false);
  a->walls[WT_LEVER_PULLED] = lever_texture(true);
  a->walls[WT_NICHE] = niche_texture(121);
  // the same stones as the dungeon wall, minus the window, for its jambs
  a->walls[WT_DUNGEON_BARE] = stone_texture(44, DUNGEON_BASE, false);
  for (int d = 0; d < 3; d++) a->walls[WT_BREAKABLE + d] = breakable_texture(d);
  // the mirror's vortex churns over four frames
  for (int f = 0; f < 4; f++) a->walls[WT_PORTAL + f] = portal_texture(f);
  for (int k = 0; k < WT_COUNT; k++) a->walls_dark[k] = image_darken(a->walls[k], 0.65f);

  // strike1/strike2 (knife/fist windup + lunge) exist for footmen only
  static const SoldierPalette *const soldiers[] = {&PAL_GUARD, &PAL_KNIGHT, &PAL_CAPTAIN};
  for (int t = 0; t < COUNT(soldiers); t++) {
    for (int p = 0; p < POSE_COUNT; p++) a->enemies[ENEMY_GUARD + t].pose[p] = soldier_sprite(soldiers[t], (Pose)p);
  }
  EnemySprites *bat = &a->enemies[ENEMY_BAT];
  EnemySprites *dog = &a->enemies[ENEMY_DOG];
  EnemySprites *rat = &a->enemies[ENEMY_RAT];
  EnemySprites *spider = &a->enemies[ENEMY_SPIDER];
  for (int p = 0; p < POSE_STRIKE1; p++) {
    bat->pose[p] = bat_swarm_sprite((Pose)p);
    dog->pose[p] = dog_sprite((Pose)p);
    rat->pose[p] = rat_sprite((Pose)p);
    spider->pose[p] = spider_sprite((Pose)p);
  }
  // the hound's, the rat's and the spider's stand and run frames seen from
  // all four sides
  for (int p = POSE_STAND; p <= POSE_WALK2; p++) {
    dog->view[p][VIEW_RIGHT] = dog->pose[p];
    dog->view[p][VIEW_LEFT] = image_mirror(dog->pose[p]);
    dog->view[p][VIEW_FRONT] = dog_front_sprite((Pose)p);
    dog->view[p][VIEW_BACK] = dog_back_sprite((Pose)p);
    rat->view[p][VIEW_RIGHT] = rat->pose[p];
    rat->view[p][VIEW_LEFT] = image_mirror(rat->pose[p]);
    rat->view[p][VIEW_FRONT] = rat_front_sprite((Pose)p);
    rat->view[p][VIEW_BACK] = rat_back_sprite((Pose)p);
    spider->view[p][VIEW_RIGHT] = spider->pose[p];
    spider->view[p][VIEW_LEFT] = image_mirror(spider->pose[p]);
    spider->view[p][VIEW_FRONT] = spider_front_sprite((Pose)p);
    spider->view[p][VIEW_BACK] = spider_back_sprite((Pose)p);
  }

  for (int k = 0; k < ITEM_BOLTS_DROP; k++) a->items[k] = item_sprite((ItemKind)k);
  a->items[ITEM_BOLTS_DROP] = a->items[ITEM_BOLTS];
  a->sparkle = sparkle_sprite();
  for (int k = 0; k < 3; k++) a->barrels[k] = barrel_sprite(k);
  for (int f = 0; f < EXPLOSION_FRAMES; f++) a->explosion[f] = explosion_sprite(f);
  a->wall_rubble = wall_rubble_sprite();
  for (int f = 0; f < WALL_DUST_FRAMES; f++) a->wall_dust[f] = wall_dust_sprite(f);

  for (int k = 0; k < TRAP_SPRITE_COUNT; k++) a->trap[k] = trap_sprite((TrapSprite)k);

  for (int w = 0; w < WEAPON_COUNT; w++) {
    for (int f = 0; f < 3; f++) a->weapons[w][f] = weapon_view_build((WeaponKind)w, f);
    a->weapon_icons[w] = weapon_icon((WeaponKind)w);
  }

  for (int s = 0; s < CREST_COUNT; s++) a->crests[s] = crest_sprite((CrestState)s);

  for (int f = 0; f < 4; f++) {
    a->torches[f] = torch_sprite(f);
    a->fountains[f] = fountain_sprite(f);
  }

  // armour is a free-standing billboard like the furniture; ceiling chains
  // are billboards too, just anchored to the ceiling by the renderer
  a->decor[DECO_TABLE] = table_sprite();
  a->decor[DECO_CHAIR] = chair_sprite();
  a->decor[DECO_ARMOR] = armor_sprite();
  a->decor[DECO_CHAINS] = chain_ceiling_sprite();
  a->decor[DECO_STATUE] = statue_sprite();
  a->decor[DECO_URN] = urn_sprite();
  a->decor[DECO_CANDELABRA] = candelabra_sprite();
  a->decor[DECO_CANDLES] = candle_cluster_sprite();

  // flame overlays for the candle stands, and the hearth blaze overlaid
  // fullbright on the fireplace's wall quad
  for (int f = 0; f < 4; f++) {
    a->candle_flames[0][f] = candle_flames_sprite(f, CANDELABRA_FLAMES, COUNT(CANDELABRA_FLAMES));
    a->candle_flames[1][f] = candle_flames_sprite(f, CANDLE_CLUSTER_FLAMES, COUNT(CANDLE_CLUSTER_FLAMES));
    a->fire_flames[f] = fireplace_flames_sprite(f);
  }

  // wall-box furniture; faces get the same orientation shading as walls
  // (darker along y), lids are horizontal and need no dark variant
  a->boxes[BOX_WARDROBE].front = wardrobe_front_texture();
  a->boxes[BOX_WARDROBE].side = wardrobe_side_texture();
  a->boxes[BOX_CHEST].front = chest_front_texture();
  a->boxes[BOX_CHEST].side = chest_side_texture();
  a->boxes[BOX_CHEST].lid = chest_lid_texture();
  for (int b = 0; b < BOX_KIND_COUNT; b++) {
    a->boxes[b].front_dark = image_darken(a->boxes[b].front, 0.65f);
    a->boxes[b].side_dark = image_darken(a->boxes[b].side, 0.65f);
  }

  a->hangings[HANG_SWORD] = sword_sprite();
  a->hangings[HANG_SHIELD] = shield_sprite();
  a->hangings[HANG_CHAIN] = chain_wall_sprite();
  a->hangings[HANG_FIREPLACE] = fireplace_sprite();

  // cobwebs come in two weaves (the second keeps its weaver) and each is
  // built left- and right-anchored, so the renderer can tuck one into either
  // upper corner of a wall face with the threads running into the masonry
  a->cobwebs[0] = cobweb_sprite(507, false);
  a->cobwebs[1] = mirrored(cobweb_sprite(507, false));
  a->cobwebs[2] = cobweb_sprite(931, true);
  a->cobwebs[3] = mirrored(cobweb_sprite(931, true));

  // settling cracks in the masonry, two of them spalled open with rubble
  a->cracks[0] = crack_sprite(19, false);
  a->cracks[1] = crack_sprite(83, true);
  a->cracks[2] = crack_sprite(147, false);
  a->cracks[3] = crack_sprite(211, true);

  // portraits: each sitter painted once per gaze direction; the renderer
  // swaps the frames so the eyes follow the player about the room
  for (int vi = 0; vi < PORTRAIT_SITTERS; vi++) {
    for (int row = 0; row < PORTRAIT_ROWS; row++) {
      for (int col = 0; col < PORTRAIT_COLS; col++) {
        double gx = (double)col / (PORTRAIT_COLS - 1) * 2 - 1;
        a->portraits[vi][row * PORTRAIT_COLS + col] = portrait_sprite(&SITTERS[vi], gx, row, 400 + vi * 31);
      }
    }
  }
}
