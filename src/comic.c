// Ink silhouettes, etched stone and paper captions. Like the rest of the
// game, all illustrations are painted at startup, without external assets.
#include "comic.h"

#include "common.h"
#include "font.h"

#include <stdio.h>
#include <string.h>

#define ART_W 640
#define ART_H 360
#define POINT_COUNT(points) ((int)(sizeof(points) / sizeof((points)[0]) / 2))

static const struct {
  const char *title, *line[2];
  float duration;
} PANELS[COMIC_KIND_COUNT][COMIC_PANELS] = {
    {{"BETRAYAL", {"Betrayed by the men you trusted, you were left to rot",
                       "behind the walls of the fortress."}, 6.5f},
     {"A WEAK LINK", {"Tonight, a rusted link gives way. You slip your chains.",
                         "In the dark, a stolen crossbow becomes your only ally."}, 7},
     {"THE WAY OUT", {"Five floors stand between you and the night.",
                         "Find three keys on each floor. Open every gate. Escape."}, 8}},
    {{"THE LAST GATE", {"The last three keys turn. Iron groans against stone.",
                           "The final gate can no longer hold you."}, 6.5f},
     {"INTO THE NIGHT", {"The alarm echoes behind you as you cross the bridge.",
                            "Ahead lies an open road, beneath a sky without bars."}, 7},
     {"FREE", {"Let the fortress keep its secrets. Your chains lie broken.",
                  "For the first time in a long time, the next step is yours."}, 8}},
};

static void rect(Image *im, float x, float y, float w, float h, const char *c) {
  gfx_fill_rect(im, x, y, w, h, color_parse(c));
}

static void line(Image *im, float x, float y, float xx, float yy, float w, const char *c) {
  gfx_line(im, x, y, xx, yy, w, color_parse(c));
}

static void shape(Image *im, const float *xy, int n, const char *c) {
  gfx_fill_poly(im, xy, n, color_parse(c));
  gfx_stroke_poly(im, xy, n, 3, rgb(8, 12, 20));
}

static void oval(Image *im, float x, float y, float rx, float ry, const char *c, bool solid) {
  float xy[48];
  for (int i = 0; i < 24; i++) {
    float a = (float)(2 * M_PI * i / 24);
    xy[i * 2] = x + cosf(a) * rx;
    xy[i * 2 + 1] = y + sinf(a) * ry;
  }
  if (solid) gfx_fill_poly(im, xy, POINT_COUNT(xy), color_parse(c));
  else gfx_stroke_poly(im, xy, POINT_COUNT(xy), 4, color_parse(c));
}

static void local_shape(Image *im, float x, float y, float s, const float *points, int n, const char *c) {
  float xy[32];
  for (int i = 0; i < n; i++) {
    xy[i * 2] = x + points[i * 2] * s;
    xy[i * 2 + 1] = y + points[i * 2 + 1] * s;
  }
  shape(im, xy, n, c);
}

static void sky(Image *im, bool dawn) {
  for (int y = 0; y < ART_H; y++) {
    float k = y / (float)ART_H;
    Color c = dawn ? rgb(25 + 112 * k, 34 + 64 * k, 49 + 20 * k)
                   : rgb(17 + 29 * k, 28 + 33 * k, 47 + 30 * k);
    gfx_fill_rect(im, 0, (float)y, ART_W, 1, c);
  }
  Lcg rng = lcg(319);
  for (int i = 0; i < 72; i++) {
    float x = (float)lcg_next(&rng) * ART_W, y = (float)lcg_next(&rng) * 200;
    gfx_fill_rect(im, x, y, i % 5 ? 1 : 2, 1, rgba(206, 216, 211, .35f));
  }
  oval(im, 473, 72, 38, 38, dawn ? "#e5c58d" : "#b3c5c3", true);
  oval(im, 459, 61, 36, 36, dawn ? "#303a47" : "#1b293e", true);
  float far[] = {0, 190, 78, 138, 146, 177, 226, 122, 348, 194, 445, 143, 543, 179, 640, 118, 640, 360, 0, 360};
  shape(im, far, POINT_COUNT(far), dawn ? "#4c5055" : "#273d50");
  float near[] = {0, 242, 97, 216, 185, 248, 282, 194, 391, 241, 505, 192, 640, 219, 640, 360, 0, 360};
  shape(im, near, POINT_COUNT(near), "#182c36");
}

static void masonry(Image *im, float x, float y, float w, float h, float block, const char *color) {
  rect(im, x, y, w, h, color);
  for (float yy = y + block; yy < y + h; yy += block) {
    line(im, x, yy, x + w, yy, 2, "#111a25");
    int row = (int)((yy - y) / block);
    for (float xx = x + (row % 2 ? block : block * .3f); xx < x + w; xx += block * 2) {
      line(im, xx, yy - block + 2, xx, yy - 2, 2, "#17222d");
      line(im, xx + 4, yy - block + 4, minf(xx + block * .7f, x + w), yy - block + 4, 1, "#58616a");
    }
  }
  line(im, x, y, x, y + h, 4, "#080c14");
  line(im, x + w, y, x + w, y + h, 4, "#080c14");
}

static void tower(Image *im, float x, float y, float w, float h, float s) {
  masonry(im, x, y, w, h, 12 * s, "#394957");
  rect(im, x + w * .7f, y, w * .3f, h, "#23323f");
  for (int i = 0; i < 4; i++) {
    float xx = x + i * w / 4;
    rect(im, xx, y - 12 * s, w / 7, 15 * s, "#465765");
    line(im, xx, y - 12 * s, xx + w / 7, y - 12 * s, 2, "#93a1a0");
  }
  for (int i = 0; i < 2; i++) {
    float xx = x + w * (.27f + i * .4f);
    rect(im, xx, y + 29 * s, 5 * s, 20 * s, "#0a111b");
    rect(im, xx + s, y + 33 * s, 2 * s, 11 * s, "#c18b45");
  }
}

static void castle(Image *im, float x, float y, float s) {
  masonry(im, x + 30 * s, y + 45 * s, 260 * s, 130 * s, 12 * s, "#2b3b49");
  tower(im, x + 113 * s, y - 55 * s, 80 * s, 160 * s, s);
  float roof[] = {0, 0, 46, -44, 92, 0};
  local_shape(im, x + 107 * s, y - 55 * s, s, roof, POINT_COUNT(roof), "#14212e");
  tower(im, x, y + 10 * s, 66 * s, 180 * s, s);
  tower(im, x + 252 * s, y, 66 * s, 190 * s, s);
  tower(im, x + 100 * s, y + 91 * s, 51 * s, 99 * s, s);
  tower(im, x + 182 * s, y + 91 * s, 51 * s, 99 * s, s);
  float arch[] = {0, 83, 0, 25, 15, 0, 30, 25, 30, 83};
  local_shape(im, x + 151 * s, y + 107 * s, s, arch, POINT_COUNT(arch), "#080f18");
  for (int i = 0; i < 3; i++)
    line(im, x + (157 + i * 9) * s, y + 128 * s, x + (157 + i * 9) * s, y + 188 * s, 2 * s, "#62717a");
  // A blood-red standard identifies the garrison, even at thumbnail size.
  float banner[] = {0, 0, 28, 0, 28, 67, 14, 56, 0, 67};
  local_shape(im, x + 137 * s, y - 31 * s, s, banner, POINT_COUNT(banner), "#8d393c");
  line(im, x + 151 * s, y - 20 * s, x + 151 * s, y + 10 * s, 3 * s, "#c69c64");
}

// A cloaked prisoner or an armed, helmeted silhouette. Coordinates are feet.
static void figure(Image *im, float x, float y, float s, bool guard) {
  const float cloak[] = {-13, -67, -27, -12, -5, -5, 17, -11, 12, -67, 0, -78};
  const float hood[] = {-13, -70, -12, -89, -2, -100, 10, -92, 14, -71, 4, -63};
  local_shape(im, x, y, s, cloak, POINT_COUNT(cloak), guard ? "#522e36" : "#17212a");
  line(im, x - 6 * s, y - 13 * s, x - 12 * s, y, 8 * s, "#0a1019");
  line(im, x + 7 * s, y - 13 * s, x + 14 * s, y, 8 * s, "#0a1019");
  local_shape(im, x, y, s, hood, POINT_COUNT(hood), guard ? "#73808a" : "#34434a");
  const float face[] = {-5, -87, 5, -89, 8, -75, 0, -70, -6, -75};
  local_shape(im, x, y, s, face, POINT_COUNT(face), guard ? "#17212b" : "#9a8171");
  line(im, x - 16 * s, y - 62 * s, x - 5 * s, y - 20 * s, 2 * s, "#78817c");
  if (guard) {
    line(im, x - 18 * s, y - 82 * s, x + 18 * s, y - 82 * s, 4 * s, "#111a25");
    line(im, x + 27 * s, y - 2 * s, x + 27 * s, y - 115 * s, 4 * s, "#9d8c70");
    const float tip[] = {22, -114, 27, -132, 32, -114};
    local_shape(im, x, y, s, tip, POINT_COUNT(tip), "#b6bbb2");
  }
}

static void key(Image *im, float x, float y, float s) {
  oval(im, x, y, 10 * s, 10 * s, "#101722", true);
  oval(im, x, y, 9 * s, 9 * s, "#d9b76b", false);
  line(im, x, y + 9 * s, x, y + 45 * s, 7 * s, "#0a111a");
  line(im, x, y + 9 * s, x, y + 45 * s, 4 * s, "#d9b76b");
  line(im, x, y + 34 * s, x + 8 * s, y + 34 * s, 5 * s, "#d9b76b");
  line(im, x, y + 43 * s, x + 8 * s, y + 43 * s, 5 * s, "#d9b76b");
}

static void draw_betrayal(Image *im) {
  sky(im, false);
  castle(im, 111, 110, 1.05f);
  float bridge[] = {0, 358, 183, 284, 370, 284, 640, 358};
  shape(im, bridge, POINT_COUNT(bridge), "#3e4650");
  for (int i = 0; i < 9; i++)
    line(im, i * 85.0f - 40, 359, 241 + i * 8.0f, 286, 2, "#1b242f");
  figure(im, 251, 343, .83f, true);
  figure(im, 304, 350, .75f, false);
  figure(im, 357, 343, .83f, true);
  line(im, 267, 308, 295, 316, 2, "#9d9990");
  line(im, 313, 316, 341, 308, 2, "#9d9990");
  // Foreground rock and thorn silhouettes frame the procession.
  float rock[] = {0, 264, 40, 297, 69, 282, 123, 360, 0, 360};
  shape(im, rock, POINT_COUNT(rock), "#0b1520");
  for (int i = 0; i < 6; i++) {
    float x = 550 + i * 19.0f;
    line(im, x, 360, x - 15, 287 - i % 3 * 12.0f, 4, "#0a141e");
    line(im, x - 7, 325, x + 8, 307, 3, "#0a141e");
  }
}

static void draw_chains(Image *im) {
  masonry(im, 0, 0, ART_W, ART_H, 44, "#35424c");
  // A high barred window throws a cold wedge of light into the cell.
  float light[] = {360, 52, 466, 53, 558, 360, 147, 360};
  gfx_fill_poly(im, light, POINT_COUNT(light), rgba(145, 172, 175, .17f));
  const float arch[] = {0, 126, 0, 32, 29, 0, 77, 0, 106, 32, 106, 126};
  local_shape(im, 359, 22, 1, arch, POINT_COUNT(arch), "#172333");
  for (int i = 0; i < 4; i++) line(im, 374 + i * 25.0f, 44, 374 + i * 25.0f, 147, 7, "#89999c");
  line(im, 361, 104, 463, 104, 6, "#849197");
  float floor[] = {0, 297, 640, 263, 640, 360, 0, 360};
  shape(im, floor, POINT_COUNT(floor), "#242b32");
  for (int i = 0; i < 10; i++) line(im, i * 75.0f, 360, 328, 267, 2, "#111c26");
  // The prisoner's hand still carries the broken cuff.
  float arm[] = {0, 360, 26, 254, 116, 211, 202, 214, 260, 237, 250, 262, 179, 257, 139, 279, 99, 360};
  shape(im, arm, POINT_COUNT(arm), "#ad8d72");
  float sleeve[] = {0, 360, 0, 302, 53, 263, 112, 304, 99, 360};
  shape(im, sleeve, POINT_COUNT(sleeve), "#202e38");
  float cuff[] = {117, 212, 144, 205, 170, 267, 144, 281};
  shape(im, cuff, POINT_COUNT(cuff), "#7d8588");
  line(im, 128, 215, 153, 273, 3, "#bdc3bb");
  line(im, 187, 224, 230, 242, 2, "#594c44");
  for (int i = 0; i < 4; i++) oval(im, 180 + i * 27.0f, 285 + i * 4.0f, 17, 10, "#a4aa9f", false);
  for (int i = 0; i < 4; i++) oval(im, 367 + i * 30.0f, 292 - i * 12.0f, 18, 10, "#77858b", false);
  oval(im, 493, 242, 19, 24, "#a4aa9f", false);
  rect(im, 486, 230, 13, 19, "#19232d");
}

static void draw_crossbow(Image *im) {
  masonry(im, 0, 0, ART_W, ART_H, 40, "#2d3945");
  const float arch[] = {0, 265, 0, 68, 45, 10, 92, -10, 139, 10, 184, 68, 184, 265};
  local_shape(im, 389, 58, 1, arch, POINT_COUNT(arch), "#0d1825");
  for (int i = 0; i < 8; i++) line(im, 402 + i * 23.0f, 104, 402 + i * 23.0f, 321, 7, "#67727a");
  line(im, 390, 193, 573, 193, 9, "#89918b");
  figure(im, 489, 305, .9f, true);
  float table[] = {0, 267, 299, 211, 390, 360, 0, 360};
  shape(im, table, POINT_COUNT(table), "#65503d");
  for (int i = 0; i < 7; i++) line(im, 0, 287 + i * 20.0f, 315 + i * 10.0f, 236 + i * 21.0f, 2, "#302e2b");
  float stock[] = {143, 360, 270, 139, 285, 142, 199, 360};
  shape(im, stock, POINT_COUNT(stock), "#9b7650");
  float bow[] = {137, 121, 201, 130, 257, 155, 302, 192, 342, 252, 321, 238, 282, 199,
                 245, 176, 196, 151, 148, 138};
  shape(im, bow, POINT_COUNT(bow), "#9aa5a1");
  line(im, 141, 130, 226, 241, 3, "#ded1aa");
  line(im, 226, 241, 335, 247, 3, "#ded1aa");
  line(im, 277, 144, 212, 256, 3, "#e1d6b8");
  float bolt[] = {274, 142, 277, 124, 281, 145};
  shape(im, bolt, POINT_COUNT(bolt), "#b7c2bd");
  for (int i = 0; i < 3; i++) key(im, 123 + i * 57.0f, 69 + i % 2 * 12.0f, .8f);
  for (int i = 0; i < 5; i++) line(im, 74 + i * 13.0f, 315, 124 + i * 13.0f, 240, 3, "#c6b58c");
}

static void draw_gate(Image *im) {
  sky(im, false);
  float road[] = {282, 208, 347, 208, 480, 360, 139, 360};
  shape(im, road, POINT_COUNT(road), "#9a957f");
  masonry(im, 0, 0, 177, ART_H, 40, "#3e4b55");
  masonry(im, 463, 0, 177, ART_H, 40, "#2c3946");
  rect(im, 163, 0, 24, ART_H, "#8d9385");
  rect(im, 453, 0, 24, ART_H, "#64777c");
  float arch[] = {177, 0, 463, 0, 463, 133, 421, 80, 377, 51, 320, 41, 264, 51, 219, 80, 177, 133};
  shape(im, arch, POINT_COUNT(arch), "#4b5961");
  rect(im, 38, 125, 94, 157, "#141f2b");
  for (int i = 0; i < 3; i++) key(im, 84, 145 + i * 43.0f, .44f);
  // Torch brackets, with the flames animated separately.
  line(im, 146, 198, 137, 150, 7, "#111b25");
  line(im, 494, 198, 503, 150, 7, "#111b25");
}

static void draw_escape(Image *im) {
  sky(im, false);
  castle(im, 25, 116, .72f);
  float road[] = {0, 360, 135, 279, 394, 255, 640, 302, 640, 360};
  shape(im, road, POINT_COUNT(road), "#697479");
  float wall[] = {0, 300, 391, 251, 640, 286, 640, 309, 393, 272, 0, 329};
  shape(im, wall, POINT_COUNT(wall), "#2b3a46");
  for (int i = 0; i < 12; i++) {
    float x = i * 62.0f;
    float y = 297 - minf(x, 390) * .125f + maxf(0, x - 390) * .14f;
    rect(im, x, y - 17, 22, 24, "#485965");
    line(im, x, y - 17, x + 22, y - 17, 2, "#a0aaa2");
  }
  figure(im, 408, 325, 1.23f, false);
  line(im, 424, 272, 454, 289, 6, "#846d50");
  line(im, 445, 268, 449, 301, 4, "#a5aaa1");
  for (int i = 0; i < 5; i++) line(im, 312 - i * 16.0f, 288 + i * 9.0f, 354 - i * 16.0f, 283 + i * 9.0f, 2, "#a0a99e");
}

static void draw_freedom(Image *im) {
  sky(im, true);
  castle(im, 43, 190, .24f);
  float hills[] = {0, 306, 86, 268, 182, 295, 274, 257, 395, 273, 513, 226, 640, 241, 640, 360, 0, 360};
  shape(im, hills, POINT_COUNT(hills), "#253c3b");
  float road[] = {193, 360, 291, 314, 447, 303, 535, 264, 486, 301, 345, 325, 306, 360};
  shape(im, road, POINT_COUNT(road), "#a89873");
  // Pine trees and a traveller, small against the open landscape.
  for (int i = 0; i < 6; i++) {
    float tree[] = {0, 0, -24, 73, -12, 69, -36, 113, 36, 113, 12, 69, 24, 73};
    local_shape(im, 30 + i * 29.0f, 233 - i % 3 * 14.0f, .55f + i % 2 * .2f, tree, POINT_COUNT(tree), "#101f27");
  }
  figure(im, 351, 320, .58f, false);
  float ground[] = {0, 337, 97, 320, 158, 337, 271, 360, 0, 360};
  shape(im, ground, POINT_COUNT(ground), "#0c1821");
  oval(im, 140, 341, 25, 12, "#9caaa2", false);
  for (int i = 0; i < 3; i++) oval(im, 178 + i * 24.0f, 347 + i * 4.0f, 15, 7, "#9caaa2", false);
}

static void grain(Image *im, unsigned seed) {
  Lcg rng = lcg(seed);
  for (int y = 0; y < im->h; y++) {
    for (int x = 0; x < im->w; x++) {
      uint32_t p = im->px[y * im->w + x];
      int d = (int)(lcg_next(&rng) * 9) - 4;
      // Stable paper grain, never frame-to-frame random noise.
      im->px[y * im->w + x] = pack_argb(255, (float)clampi((int)((p >> 16) & 255) + d, 0, 255),
                                      (float)clampi((int)((p >> 8) & 255) + d, 0, 255),
                                      (float)clampi((int)(p & 255) + d, 0, 255));
    }
  }
}

void comic_art_build(ComicArt *art) {
  void (*paint[COMIC_KIND_COUNT][COMIC_PANELS])(Image *) = {
      {draw_betrayal, draw_chains, draw_crossbow}, {draw_gate, draw_escape, draw_freedom}};
  for (int k = 0; k < COMIC_KIND_COUNT; k++) {
    for (int i = 0; i < COMIC_PANELS; i++) {
      art->scene[k][i] = image_new(ART_W, ART_H);
      paint[k][i](art->scene[k][i]);
      grain(art->scene[k][i], (unsigned)(103 + k * 71 + i));
    }
  }
  art->scratch = image_new(ART_W, ART_H);
}

void comic_art_free(ComicArt *art) {
  for (int k = 0; k < COMIC_KIND_COUNT; k++)
    for (int i = 0; i < COMIC_PANELS; i++) image_free(art->scene[k][i]);
  image_free(art->scratch);
  memset(art, 0, sizeof *art);
}

void comic_start(Comic *comic, ComicKind kind) {
  *comic = (Comic){.kind = kind};
}

bool comic_next(Comic *comic) {
  if (comic->panel + 1 >= COMIC_PANELS) return true;
  comic->panel++;
  return false;
}

bool comic_update(Comic *comic, float dt) {
  // Carry excess time across panels, so playback does not depend on frame rate.
  while (dt > 0) {
    float remaining = maxf(0, PANELS[comic->kind][comic->panel].duration - comic->age[comic->panel]);
    float step = minf(dt, remaining);
    for (int i = 0; i <= comic->panel; i++) comic->age[i] += step;
    dt -= step;
    if (step >= remaining) {
      if (comic_next(comic)) return true;
    } else break;
  }
  return false;
}

static void flame(Image *im, float x, float y, float t) {
  float flicker = sinf(t * 9) * 3 + sinf(t * 17) * 2;
  GradientStop glow[] = {{0, {232, 154, 62, .28f}}, {1, {232, 154, 62, 0}}};
  gfx_radial(im, x - 58, y - 58, 116, 116, x, y, 0, 58, glow, 2, BLEND_SCREEN);
  float fire[] = {x - 7, y + 7, x - 9, y - 3, x + flicker, y - 26, x + 4, y - 9, x + 8, y + 3, x + 5, y + 9};
  gfx_fill_poly(im, fire, POINT_COUNT(fire), rgb(223, 142, 62));
  float core[] = {x - 3, y + 6, x, y - 12 + flicker, x + 4, y + 6};
  gfx_fill_poly(im, core, POINT_COUNT(core), rgb(244, 220, 153));
}

static void animate(Image *im, ComicKind kind, int panel, float t) {
  if (kind == COMIC_INTRO && panel == 0) {
    for (int i = 0; i < 35; i++) {
      float x = fmodf(i * 73.0f + t * 68, 680) - 20;
      float y = fmodf(i * 43.0f + t * 190, 380) - 10;
      gfx_line(im, x, y, x - 5, y + 16, 1, rgba(160, 186, 193, .28f));
    }
    // Wind tugs at the standard over the keep.
    float flag[] = {273, 80, 312, 85 + sinf(t * 2) * 7, 308, 111, 273, 106};
    shape(im, flag, POINT_COUNT(flag), "#8d393c");
  } else if (kind == COMIC_INTRO && panel == 1) {
    float drop = minf(75, t * 22), drift = minf(22, t * 8);
    oval(im, 311 + drift, 293 + drop, 16, 9, "#b1b6a8", false);
    if (t < 1.2f) {
      float a = 1 - t / 1.2f;
      gfx_line(im, 314, 267, 324, 250, 2, rgba(222, 209, 163, a));
      gfx_line(im, 334, 277, 352, 272, 2, rgba(222, 209, 163, a));
    }
  } else if (kind == COMIC_INTRO && panel == 2) {
    flame(im, 356, 154, t);
    for (int i = 0; i < 3; i++) {
      float a = .35f + .3f * sinf(t * 2 + i * 2.0f);
      gfx_line(im, 123 + i * 57.0f - 7, 69 + i % 2 * 12.0f, 123 + i * 57.0f + 7,
               69 + i % 2 * 12.0f, 1, rgba(255, 239, 190, a));
    }
  } else if (kind == COMIC_OUTRO && panel == 0) {
    // The portcullis rises clear of the road over the opening beat.
    float lift = clampf(t / 4, 0, 1) * 295;
    image_set_clip(im, 186, 41, 454, ART_H);
    for (int i = 0; i < 11; i++) {
      float x = 192 + i * 25.0f;
      line(im, x, 0, x, 321 - lift, 7, "#0c1520");
      line(im, x - 2, 0, x - 2, 321 - lift, 2, "#b0b8ae");
      float tooth[] = {x - 5, 317 - lift, x + 5, 317 - lift, x, 333 - lift};
      shape(im, tooth, POINT_COUNT(tooth), "#87928e");
    }
    for (int i = 0; i < 5; i++) rect(im, 186, 70 + i * 57.0f - lift, 268, 8, "#748186");
    image_reset_clip(im);
    flame(im, 136, 142, t);
    flame(im, 504, 142, t + 1);
  } else if (kind == COMIC_OUTRO && panel == 1) {
    float flutter = sinf(t * 4) * 8;
    float cloak[] = {400, 245, 379, 268, 352 + flutter, 282, 375, 300, 407, 291, 416, 258};
    shape(im, cloak, POINT_COUNT(cloak), "#293c43");
    line(im, 396, 254, 375, 280, 2, "#84918a");
  } else {
    // Birds leave the walls behind with the traveller.
    for (int i = 0; i < 3; i++) {
      float x = 376 + i * 29.0f + sinf(t * .3f) * 20;
      float y = 155 - i * 15.0f;
      float wing = sinf(t * 3.5f + i) * 4;
      line(im, x - 7, y + wing, x, y + 2, 2, "#172632");
      line(im, x, y + 2, x + 7, y + wing, 2, "#172632");
    }
  }
  // Dust in shafts of light / windblown motes; deterministic and subtle.
  if (!(kind == COMIC_INTRO && panel == 0)) {
    for (int i = 0; i < 15; i++) {
      float x = fmodf(i * 83.0f + t * 5, 640);
      float y = fmodf(i * 51.0f + 360 - fmodf(t * 9, 360), 360);
      gfx_fill_rect(im, x, y, 1, 2, rgba(218, 207, 168, .4f));
    }
  }
}

static float smooth(float v) {
  v = clampf(v, 0, 1);
  return v * v * (3 - 2 * v);
}

static void panel_image(Image *fb, const Image *scene, float x, float y, float w, float h, float t, int index) {
  float zoom = 1.025f + .045f * smooth(t / 10);
  float scale = maxf(w / scene->w, h / scene->h) * zoom;
  float sw = w / scale, sh = h / scale;
  float pan = sinf(t * .12f + index) * .2f;
  float sx = (scene->w - sw) * (.5f + pan), sy = (scene->h - sh) * .5f;
  gfx_blit(fb, scene, sx, sy, sw, sh, x, y, w, h, 1);
}

void comic_draw_cover(const ComicArt *art, Image *fb, float time) {
  image_fill(fb, 0xff101822);
  if (!art->scene[COMIC_INTRO][0]) return;
  panel_image(fb, art->scene[COMIC_INTRO][0], 0, 0, (float)fb->w, (float)fb->h, time, 0);
  gfx_fill_rect(fb, 0, 0, (float)fb->w, (float)fb->h, rgba(5, 8, 14, .65f));
}

void comic_draw(ComicArt *art, Image *fb, const Comic *comic) {
  static const float frame[COMIC_PANELS][4] = {{18, 45, 354, 239}, {382, 45, 240, 115}, {382, 170, 240, 114}};
  image_fill(fb, 0xff10151e);
  font_draw(fb, comic->kind == COMIC_INTRO ? "PROLOGUE / THE LONG NIGHT" : "EPILOGUE / BEYOND THE WALLS",
            18, 26, 13, FONT_BOLD, ALIGN_LEFT, rgb(218, 185, 121));
  char number[24];
  snprintf(number, sizeof number, "%02d / %02d", comic->panel + 1, COMIC_PANELS);
  font_draw(fb, number, 622, 26, 12, FONT_BOLD, ALIGN_RIGHT, rgb(170, 174, 167));
  // The current panel grows into the large frame. Older panels settle into
  // the margin, preserving the page while giving every scene room to breathe.
  float change = smooth(comic->age[comic->panel] / .7f);
  int previous = maxi(0, comic->panel - 1);
  for (int pass = 0; pass < COMIC_PANELS; pass++) {
    // Paint the moving active frame last, over the panels it passes.
    int i = pass == COMIC_PANELS - 1 ? comic->panel : pass < comic->panel ? pass : pass + 1;
    int slot = i == comic->panel ? 0 : i < comic->panel ? i + 1 : i;
    int old_slot = i == previous ? 0 : i < previous ? i + 1 : i;
    float box[4];
    for (int j = 0; j < 4; j++) box[j] = frame[old_slot][j] + (frame[slot][j] - frame[old_slot][j]) * change;
    float x = box[0], y = box[1], w = box[2], h = box[3];
    bool active = i == comic->panel;
    rect(fb, x - 3, y - 3, w + 6, h + 6, active ? "#d6b570" : "#4f5b61");
    rect(fb, x, y, w, h, "#18212b");
    if (i <= comic->panel && art->scene[comic->kind][i] && art->scratch) {
      memcpy(art->scratch->px, art->scene[comic->kind][i]->px, (size_t)ART_W * ART_H * sizeof(uint32_t));
      animate(art->scratch, comic->kind, i, comic->age[i]);
      panel_image(fb, art->scratch, x, y, w, h, comic->age[i], i);
      gfx_fill_rect(fb, x, y + h - 25, w, 25, rgba(6, 11, 18, .85f));
      font_draw(fb, PANELS[comic->kind][i].title, x + 11, y + h - 8, 12, FONT_BOLD, ALIGN_LEFT,
                rgb(229, 214, 178));
      float fade = 1 - smooth(comic->age[i] / .65f);
      gfx_fill_rect(fb, x, y, w, h, rgba(16, 21, 30, fade));
    } else {
      // Blank panels wait their turn, like an unfinished comic page.
      for (int j = 0; j < 7; j++)
        gfx_line(fb, x + w * .3f + j * 13, y + h * .35f, x + w * .3f + j * 13 - 14,
                 y + h * .65f, 1, rgba(120, 132, 136, .12f));
    }
    rect(fb, x + 7, y + 7, 23, 22, active ? "#d6b570" : "#25313c");
    snprintf(number, sizeof number, "%d", i + 1);
    font_draw(fb, number, x + 18, y + 23, 13, FONT_BOLD, ALIGN_CENTER,
              active ? rgb(19, 26, 34) : rgb(169, 178, 175));
  }
  // Paper narration sits outside the illustrations: readable at any point
  // in the animation, without a typewriter effect delaying the reader.
  rect(fb, 18, 296, 604, 53, "#d6c9a5");
  line(fb, 26, 299, 613, 299, 1, "#f0e0bc");
  for (int i = 0; i < 2; i++)
    font_draw(fb, PANELS[comic->kind][comic->panel].line[i], 31, 317 + i * 18.0f, 12, FONT_BOLD,
              ALIGN_LEFT, rgb(30, 34, 39));
  for (int i = 0; i < COMIC_PANELS; i++) {
    float x = 18 + i * 204.0f;
    rect(fb, x, 358, 196, 2, "#39434a");
    float progress = i < comic->panel ? 1 : i == comic->panel
        ? clampf(comic->age[i] / PANELS[comic->kind][i].duration, 0, 1) : 0;
    gfx_fill_rect(fb, x, 358, 196 * progress, 2, rgb(214, 181, 112));
  }
}
