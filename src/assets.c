// Complete painted art set. Every runtime asset is assembled here from the
// reviewed atlases and high-resolution material/effect generators. There is
// deliberately no mixed-style fallback: incomplete installations fail clearly.
#include "assets.h"
#include "art.h"
#include "horror.h"
#include "level.h"
#include "ui.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static char art_error[160];
const char *assets_error(void) {
  return art_error;
}

void art_rect(Image *im, float x, float y, float w, float h, Color c) {
  float s = im->w / 64.0f;
  gfx_fill_rect(im, x * s, y * s, w * s, h * s, c);
}
void art_line(Image *im, float x, float y, float xx, float yy, float w, Color c) {
  float s = im->w / 64.0f;
  gfx_line(im, x * s, y * s, xx * s, yy * s, w * s, c);
}
void art_poly(Image *im, const float *points, int n, Color c) {
  float xy[256], s = im->w / 64.0f;
  if (n > 128) return;
  for (int i = 0; i < n * 2; i++)
    xy[i] = points[i] * s;
  gfx_fill_poly(im, xy, n, c);
}
void art_oval(Image *im, float x, float y, float rx, float ry, float width, Color c) {
  float xy[96], s = im->w / 64.0f;
  for (int i = 0; i < 48; i++) {
    float a = i * (float)M_PI / 24;
    xy[i * 2] = (x + cosf(a) * rx) * s;
    xy[i * 2 + 1] = (y + sinf(a) * ry) * s;
  }
  if (width <= 0)
    gfx_fill_poly(im, xy, 48, c);
  else
    gfx_stroke_poly(im, xy, 48, width * s, c);
}
Image *art_cell(const Image *atlas, int columns, int index) {
  if (!atlas) return NULL;
  Image *im = image_new(TEX, TEX);
  gfx_blit(im, atlas, (index % columns) * TEX, (index / columns) * TEX, TEX, TEX, 0, 0, TEX, TEX, 1);
  return im;
}
uint32_t art_sample(const Image *im, float x, float y) {
  if (x < 0 || y < 0 || x >= im->w - 1 || y >= im->h - 1) return 0;
  int ix = (int)x, iy = (int)y;
  float fx = x - ix, fy = y - iy;
  uint32_t q[4] = {im->px[iy * im->w + ix], im->px[iy * im->w + ix + 1], im->px[(iy + 1) * im->w + ix],
                   im->px[(iy + 1) * im->w + ix + 1]};
  float k[4] = {(1 - fx) * (1 - fy), fx * (1 - fy), (1 - fx) * fy, fx * fy}, c[4] = {0};
  for (int i = 0; i < 4; i++)
    for (int j = 0; j < 4; j++)
      c[j] += ((q[i] >> (24 - j * 8)) & 255) * k[i];
  return pack_argb(c[0], c[1], c[2], c[3]);
}
// Smooth local deformation, rather than whole-frame stepping. kind 0: gait,
// 1: flame, 2: fountain, 3: a settled animal corpse.
Image *art_motion(const Image *src, int frame, int kind) {
  Image *im = image_new(src->w, src->h);
  float phase = frame * (float)M_PI * .5f;
  for (int y = 0; y < im->h; y++)
    for (int x = 0; x < im->w; x++) {
      float sx = x, sy = y;
      if (kind == 0) {
        float feet = clampf((y - im->h * .73f) / (im->h * .22f), 0, 1);
        sx -= sinf(phase) * sinf(x * .064f) * feet * 3;
        sy -= cosf(phase + x * .065f) * feet * 2;
      } else if (kind == 1) {
        float flame = clampf((im->h * .48f - y) / (im->h * .48f), 0, 1);
        sx -= sinf(phase + y * .12f) * flame * 2.3f;
        sy -= sinf(phase * .8f) * flame * 1.8f;
      } else if (kind == 2) {
        float pool = clampf((y - im->h * .65f) / (im->h * .25f), 0, 1);
        sx -= sinf(phase + y * .24f) * pool * .7f;
      } else {
        sx = (x - im->w * .5f) * .95f + im->w * .5f;
        sy = (y - im->h * .96f) * 3.2f + im->h * .96f;
      }
      im->px[y * im->w + x] = art_sample(src, sx, sy);
    }
  return im;
}
static Image *flame_layer(const Image *source) {
  Image *im = image_new(TEX, TEX);
  for (int y = 0; y < TEX; y++)
    for (int x = 0; x < TEX; x++) {
      uint32_t p = source->px[y * TEX + x];
      float a = (p >> 24) / 255.0f;
      if (a < .01f) continue;
      float red = ((p >> 16) & 255) / a, green = ((p >> 8) & 255) / a, blue = (p & 255) / a;
      if (red > 175 && green > 105 && green > blue * 1.18f) im->px[y * TEX + x] = p;
    }
  return im;
}
static Image *fireplace_frame(const Image *source, int frame) {
  Image *im = image_clone(source);
  float phase = frame * (float)(2 * M_PI / FIREPLACE_FRAMES);
  // The hearth's flame is below the middle of the atlas cell. Deform that
  // field as a whole so no still flame remains under the moving one. The
  // stone arch, andirons and base of the logs stay anchored in every frame.
  for (int y = 0; y < TEX; y++)
    for (int x = 0; x < TEX; x++) {
      float u = (x + .5f) / TEX, v = (y + .5f) / TEX;
      if (u <= .34f || u >= .68f || v <= .45f || v >= .86f) continue;
      float across = sinf((u - .34f) / .34f * (float)M_PI);
      float tip = clampf((.86f - v) / .30f, 0, 1) * clampf((v - .45f) / .08f, 0, 1);
      float sway = sinf(phase + v * 18) * 5.5f + sinf(phase * 2 - v * 25) * 2;
      float rise = sinf(phase + u * 21) * 6;
      im->px[y * TEX + x] = art_sample(source, x + sway * across * tip, y + rise * across * tip);
    }
  return im;
}
static Image *arrow(const Image *bolt, TrapSprite kind) {
  Image *im = image_new(TEX, TEX);
  if (kind == TRAP_ARROW || kind == TRAP_ARROW_LEFT) {
    // Align the photographed bolt's long axis to the direction of travel.
    double mass = 0, cx = 0, cy = 0;
    for (int y = 0; y < TEX; y++)
      for (int x = 0; x < TEX; x++) {
        float a = (bolt->px[y * TEX + x] >> 24) / 255.0f;
        mass += a;
        cx += x * a;
        cy += y * a;
      }
    cx /= mass;
    cy /= mass;
    double xx = 0, yy = 0, xy = 0;
    for (int y = 0; y < TEX; y++)
      for (int x = 0; x < TEX; x++) {
        float a = (bolt->px[y * TEX + x] >> 24) / 255.0f;
        xx += (x - cx) * (x - cx) * a;
        yy += (y - cy) * (y - cy) * a;
        xy += (x - cx) * (y - cy) * a;
      }
    float angle = .5f * atan2f(2 * xy, xx - yy), ax = -cosf(angle), ay = -sinf(angle), lo = 1e6f, hi = -1e6f;
    for (int y = 0; y < TEX; y++)
      for (int x = 0; x < TEX; x++)
        if ((bolt->px[y * TEX + x] >> 24) > 32) {
          float t = (x - cx) * ax + (y - cy) * ay;
          lo = minf(lo, t);
          hi = maxf(hi, t);
        }
    float scale = TEX * .88f / (hi - lo), middle = (lo + hi) * .5f;
    for (int y = 0; y < TEX; y++)
      for (int x = 0; x < TEX; x++) {
        float u = (x - TEX * .5f) / scale + middle, v = (y - TEX * .5f) / scale;
        im->px[y * TEX + x] = art_sample(bolt, cx + ax * u - ay * v, cy + ay * u + ax * v);
      }
    if (kind == TRAP_ARROW_LEFT) {
      Image *flip = image_mirror(im);
      image_free(im);
      return flip;
    }
  } else {
    // End-on steel has a raised ridge, bevel and a recessed socket.
    art_oval(im, 32, 34, 3.2f, 3.2f, 0, rgb(78, 58, 35));
    if (kind == TRAP_ARROW_AWAY)
      for (int i = 0; i < 3; i++) {
        float a = i * (float)M_PI * 2 / 3;
        for (int k = 0; k < 14; k++) {
          float r = 3 + k * .36f;
          art_line(im, 32 + cosf(a) * r, 33 + sinf(a) * r, 32 + cosf(a + .24f) * r, 33 + sinf(a + .24f) * r, .27f,
                   rgb(123 + k * 3, 108 + k * 3, 82 + k * 3));
        }
      }
    else {
      art_poly(im, (float[]){32, 23, 38, 34, 32, 39, 26, 34}, 4, rgb(57, 72, 78));
      art_poly(im, (float[]){32, 23, 32, 36, 26, 34}, 3, rgb(174, 188, 188));
      art_poly(im, (float[]){32, 23, 38, 34, 32, 36}, 3, rgb(100, 122, 133));
      art_line(im, 32, 24, 32, 36, .25f, rgb(224, 226, 209));
      art_line(im, 27, 34, 32, 37, .2f, rgb(121, 132, 129));
    }
  }
  return im;
}
static Image *pendulum(const Image *axe, int frame) {
  Image *im = image_new(TEX, TEX);
  float angle = -PENDULUM_MAX_ANGLE + frame * (2 * PENDULUM_MAX_ANGLE / (PENDULUM_FRAMES - 1));
  float sn = sinf(angle), cs = cosf(angle);
  for (int y = 0; y < TEX; y++)
    for (int x = 0; x < TEX; x++) {
      float across = ((x + .5f) / TEX - .5f) * PENDULUM_VIEW_WIDTH, z = 1 - (y + .5f) / TEX;
      float tangent = across * cs + (z - PENDULUM_PIVOT) * sn;
      float radius = across * sn - (z - PENDULUM_PIVOT) * cs;
      float u = (tangent / (PENDULUM_BLADE_WIDTH * 2) + .5f) * TEX;
      float v = radius / (PENDULUM_LENGTH + PENDULUM_BLADE_HEIGHT) * TEX;
      im->px[y * TEX + x] = art_sample(axe, u, v);
    }
  return im;
}
static Image *web(int index) {
  Image *im = image_new(TEX, TEX);
  float anchor = index % 2 ? 62 : 2;
  for (int spoke = 0; spoke < 9; spoke++) {
    float a = (.08f + spoke * .16f) * (index % 2 ? -1 : 1) + (index % 2 ? (float)M_PI : 0);
    art_line(im, anchor, 2, anchor + cosf(a) * 53, 2 + sinf(a) * 53, .12f, rgba(193, 198, 187, .42f));
  }
  for (int ring = 1; ring < 8; ring++) {
    float radius = ring * 6;
    for (int k = 0; k < 8; k++) {
      float a = (.08f + k * .16f) * (index % 2 ? -1 : 1) + (index % 2 ? (float)M_PI : 0),
            b = a + .16f * (index % 2 ? -1 : 1);
      art_line(im, anchor + cosf(a) * radius, 2 + sinf(a) * radius, anchor + cosf(b) * (radius + 1),
               2 + sinf(b) * (radius + 1), .1f, rgba(191, 197, 185, .35f));
    }
  }
  if (index >= 2) {
    art_oval(im, anchor + (index % 2 ? -24 : 24), 26, .7f, 1.1f, 0, rgb(48, 39, 32));
  }
  return im;
}
static Image *fracture(int index) {
  Image *im = image_new(TEX, TEX);
  Lcg rng = lcg(882 + index * 63);
  float x = 20 + index * 5, y = 1;
  for (int n = 0; n < 20; n++) {
    float xx = x + ((float)lcg_next(&rng) - .5f) * 6, yy = y + 2.5f;
    art_line(im, x + .3f, y, xx + .3f, yy, .33f, rgba(201, 184, 155, .6f));
    art_line(im, x, y, xx, yy, .22f, rgba(10, 16, 19, .9f));
    x = xx;
    y = yy;
  }
  return im;
}
static Image *gaze(const Image *source, float gx, float gy) {
  Image *im = image_clone(source);
  // Displace only the painted face very slightly; the antique frame is fixed.
  for (int y = 45; y < 130; y++)
    for (int x = 80; x < 180; x++) {
      float edge = minf(minf(x - 80, 180 - x), minf(y - 45, 130 - y));
      float k = clampf(edge / 15, 0, 1);
      im->px[y * TEX + x] = art_sample(source, x - gx * k * 1.8f, y - gy * k * 1.4f);
    }
  return im;
}
static Image *load_atlas(const char *name, int cols, int rows) {
  Image *im = image_load_art(name);
  if (!im || im->w != cols * TEX || im->h != rows * TEX) {
    snprintf(art_error, sizeof art_error, "Missing or invalid artwork: assets/%s", name);
    image_free(im);
    return NULL;
  }
  return im;
}
static Image *facade_window(const Image *stone,const Image *wood,bool shutter) {
  Image *im=image_new(TEX,TEX);
  for (int y=0;y<TEX;y++) for (int x=0;x<TEX;x++) {
    float u=(x+.5f)*64/TEX,v=(y+.5f)*64/TEX,dx=fabsf(u-32);
    float top=shutter?8:5+18*powf(dx/20,1.6f);
    if (dx>20 || v<top || v>60) continue;
    bool inside=dx<14 && v>(shutter?13:10+20*powf(dx/14,1.5f)) && v<55;
    uint32_t source=(shutter?wood:stone)->px[y*TEX+x];
    float grain=((source>>16)&255)/128.0f;
    float red=105*grain,green=108*grain,blue=105*grain;
    if (shutter) { red=73*grain; green=51*grain; blue=32*grain; }
    if (inside) {
      int pane=((int)(u/5)+(int)(v/9))%4;
      float light=.65f+.25f*(1-v/64);
      static const float colors[4][3]={{53,65,88},{114,72,44},{88,42,44},{48,76,68}};
      red=colors[pane][0]*light; green=colors[pane][1]*light; blue=colors[pane][2]*light;
      if (shutter) { red=35; green=31; blue=27; }
      if (fabsf(u-32)<.55f || fabsf(v-35)<.5f || (!shutter && (fabsf(fmodf(u,5)-2.5f)<.28f || fabsf(fmodf(v,9)-4.5f)<.25f))) {
        red=21; green=23; blue=22;
      }
    } else {
      float edge=minf(20-dx,minf(v-top,60-v));
      float shade=edge<.8f?1.3f:edge<1.6f?.6f:1;
      red*=shade; green*=shade; blue*=shade;
      if (!shutter && v<34 && fabsf(fmodf(atan2f(u-32,35-v)+3.2f,.28f)-.14f)<.015f) { red*=.5f; green*=.5f; blue*=.5f; }
    }
    im->px[y*TEX+x]=pack_argb(255,clampf(red,0,255),clampf(green,0,255),clampf(blue,0,255));
  }
  return im;
}
bool assets_build(Assets *a) {
  memset(a, 0, sizeof *a);
  art_error[0] = 0;
  static const struct {
    const char *name;
    int cols, rows;
  } sheets[] = {{"guard.bmp", 3, 4},      {"knight.bmp", 3, 4},   {"captain.bmp", 3, 4},     {"animals.bmp", 4, 4},
                {"props.bmp", 4, 4},      {"items.bmp", 4, 2},    {"horror.bmp", 3, 2},      {"fixtures.bmp", 3, 2},
                {"weapons.bmp", 3, 3},    {"combat.bmp", 4, 4},   {"materials.bmp", 4, 4},   {"effects.bmp", 4, 3},
                {"hound_walk.bmp", 3, 4}, {"rat_walk.bmp", 3, 4}, {"spider_walk.bmp", 3, 4}, {"rat_death.bmp", 3, 1},
                {"equipment.bmp", 4, 1}, {"outdoors.bmp", 2, 4}, {"gate-winch.bmp",1,1},
                {"drain-valve.bmp",1,1}, {"chapel-bell.bmp",1,1}, {"guard-walk.bmp",3,4},
                {"knight-walk.bmp",3,4}, {"captain-walk.bmp",3,4}, {"knight-melee.bmp",3,4}};
  enum { ATLAS_COUNT = sizeof sheets / sizeof sheets[0] };
  Image *atlas[ATLAS_COUNT] = {0};
  for (int i = 0; i < ATLAS_COUNT; i++) {
    atlas[i] = load_atlas(sheets[i].name, sheets[i].cols, sheets[i].rows);
    if (!atlas[i]) {
      for (int j = 0; j < i; j++)
        image_free(atlas[j]);
      return false;
    }
  }
  Image *limestone = load_atlas("limestone.bmp", 1, 1);
  if (!limestone) {
    snprintf(art_error, sizeof art_error, "Missing artwork: assets/limestone.bmp");
    for (int i = 0; i < ATLAS_COUNT; i++)
      image_free(atlas[i]);
    return false;
  }
  materials_build(a, limestone, atlas[10], atlas[4]);
  a->hangings[HANG_GLASS_WINDOW]=facade_window(limestone,a->walls[WT_TIMBER],false);
  a->hangings[HANG_SHUTTER_WINDOW]=facade_window(limestone,a->walls[WT_TIMBER],true);
  image_free(limestone);
  for (int i=0;i<3;i++) a->mechanisms[i]=art_cell(atlas[18+i],1,0);
  for (int t = 0; t < 3; t++) {
    for (int p = 0; p < POSE_COUNT; p++)
      a->enemies[t].pose[p] = art_cell(atlas[t], 3, p);
    for (int p = 0; p < 3; p++)
      for (int v = 0; v < VIEW_COUNT; v++)
        a->enemies[t].view[p][v] = art_cell(atlas[21+t],3,v*3+p);
  }
  for (int p=0;p<POSE_COUNT;p++) a->knight_melee.pose[p]=art_cell(atlas[24],3,p);
  for (int p=0;p<3;p++) for (int v=0;v<VIEW_COUNT;v++) a->knight_melee.view[p][v]=a->enemies[ENEMY_KNIGHT].view[p][v];
  static const int animal_type[] = {ENEMY_DOG, ENEMY_RAT, ENEMY_SPIDER};
  for (int row = 0; row < 3; row++) {
    EnemySprites *e = &a->enemies[animal_type[row]];
    for (int v = 0; v < VIEW_COUNT; v++) {
      for (int p = 0; p < 3; p++)
        e->view[p][v] = art_cell(atlas[12 + row], 3, v * 3 + p);
    }
    for (int p = 0; p < POSE_COUNT; p++) {
      if (p < 3)
        e->pose[p] = e->view[p][VIEW_RIGHT];
      else if (p >= POSE_DIE1 && p <= POSE_DEAD)
        e->pose[p] =
            row == 1 ? art_cell(atlas[15], 3, p - POSE_DIE1) : art_cell(atlas[9], 4, (p - POSE_DIE1) * 4 + row);
      else if (p == POSE_PAIN)
        e->pose[p] = row == 1 ? art_cell(atlas[15], 3, 0) : art_cell(atlas[9], 4, row);
      else
        e->pose[p] = art_motion(e->view[0][VIEW_FRONT], p, 0);
    }
  }
  for (int p = 0; p < POSE_COUNT; p++)
    a->enemies[ENEMY_BAT].pose[p] = p >= POSE_DIE1 && p <= POSE_DEAD ? art_cell(atlas[9], 4, (p - POSE_DIE1) * 4 + 3)
                                                                     : art_cell(atlas[3], 4, 12 + p % 4);
  for (int p = 0; p < 3; p++)
    for (int v = 0; v < VIEW_COUNT; v++)
      a->enemies[ENEMY_BAT].view[p][v] = a->enemies[ENEMY_BAT].pose[p];
  for (int i = 0; i < ITEM_KIND_COUNT; i++)
    a->items[i] = art_cell(atlas[5], 4, i == ITEM_BOLTS_DROP ? 3 : i == ITEM_ARBALEST ? 3 : i);
  // A repeating crossbow pickup is the actual painted weapon, rather than a quiver.
  image_free(a->items[ITEM_ARBALEST]);
  a->items[ITEM_ARBALEST] = art_cell(atlas[16], 4, 1);
  for (int i = 0; i < 3; i++)
    a->barrels[i] = art_cell(atlas[4], 4, i == 0 ? 10 : i == 1 ? 11 : 12);
  a->wall_rubble = image_new(TEX, TEX);
  Image *stone_debris = art_cell(atlas[11], 4, 8);
  gfx_blit(a->wall_rubble, stone_debris, 0, 0, TEX, TEX, 0, TEX * .75f, TEX, TEX * .22f, 1);
  image_free(stone_debris);
  for (int f = 0; f < WALL_DUST_FRAMES; f++) {
    static const int stage[] = {0, 1, 1, 2, 2, 3};
    a->wall_dust[f] = art_cell(atlas[11], 4, 8 + stage[f]);
    if (f == 2 || f == 4) {
      Image *src = a->wall_dust[f];
      a->wall_dust[f] = image_darken(src, .84f);
      image_free(src);
    }
  }
  for (int f = 0; f < EXPLOSION_FRAMES; f++)
    a->explosion[f] = art_cell(atlas[11], 4, f);
  a->sparkle = image_new(TEX, TEX);
  GradientStop shimmer[] = {{0, {255, 238, 187, .8f}}, {1, {255, 198, 112, 0}}};
  gfx_radial(a->sparkle, 0, 0, TEX, TEX, TEX / 2, TEX / 2, 0, TEX / 2, shimmer, 2, BLEND_OVER);
  art_line(a->sparkle, 27, 32, 37, 32, .25f, rgb(255, 240, 195));
  art_line(a->sparkle, 32, 25, 32, 39, .25f, rgb(255, 240, 195));
  a->trap[TRAP_PLATE] = art_cell(atlas[5], 4, 7);
  a->trap[TRAP_PRESSED] = image_darken(a->trap[TRAP_PLATE], .66f);
  Image *bolt = art_cell(atlas[16], 4, 3);
  for (int i = TRAP_ARROW; i < TRAP_SPRITE_COUNT; i++)
    a->trap[i] = arrow(bolt, (TrapSprite)i);
  image_free(bolt);
  Image *axe = art_cell(atlas[7], 3, 4);
  for (int i = 0; i < PENDULUM_FRAMES; i++)
    a->pendulum[i] = pendulum(axe, i);
  image_free(axe);
  for (int w = 0; w < WEAPON_COUNT; w++) {
    for (int f = 0; f < 3; f++) {
      Image *cell = art_cell(atlas[8], 3, w * 3 + f);
      a->weapons[w][f] = image_new(WEAPON_VIEW, WEAPON_VIEW);
      gfx_blit(a->weapons[w][f], cell, 0, 0, TEX, TEX, 0, 0, WEAPON_VIEW, WEAPON_VIEW, 1);
      image_free(cell);
    }
    a->weapon_icons[w] = art_cell(atlas[16], 4, w == WEAPON_DAGGER ? 2 : w == WEAPON_CROSSBOW ? 0 : 1);
  }
  for (int s = 0; s < CREST_COUNT; s++) {
    a->crests[s] = image_new(CREST_SIZE, CREST_SIZE);
    ui_emblem(a->crests[s], CREST_SIZE / 2, CREST_SIZE / 2, CREST_SIZE * .8f);
    if (s >= CREST_HURT) gfx_line(a->crests[s], 20, 10, 28, 37, 1, rgb(84, 45, 39));
  }
  Image *torch = art_cell(atlas[7], 3, 3), *fountain = art_cell(atlas[4], 4, 7), *hearth = art_cell(atlas[4], 4, 15);
  Image *candelabra = art_cell(atlas[4], 4, 8), *candles = art_cell(atlas[5], 4, 6);
  for (int f = 0; f < 4; f++) {
    a->torches[f] = art_motion(torch, f, 1);
    a->fountains[f] = art_motion(fountain, f, 2);
    Image *motion = art_motion(candelabra, f, 1);
    a->candle_flames[0][f] = flame_layer(motion);
    image_free(motion);
    motion = art_motion(candles, f, 1);
    a->candle_flames[1][f] = flame_layer(motion);
    image_free(motion);
  }
  for (int f = 0; f < FIREPLACE_FRAMES; f++)
    a->fireplaces[f] = fireplace_frame(hearth, f);
  image_free(torch);
  image_free(fountain);
  a->decor[DECO_FOUNTAIN] = a->fountains[0];
  a->decor[DECO_TABLE] = art_cell(atlas[4], 4, 0);
  a->decor[DECO_CHAIR] = art_cell(atlas[4], 4, 1);
  a->decor[DECO_WARDROBE] = art_cell(atlas[4], 4, 2);
  a->decor[DECO_CHEST] = art_cell(atlas[4], 4, 3);
  a->decor[DECO_FIREPLACE] = hearth;
  a->decor[DECO_ARMOR] = art_cell(atlas[4], 4, 4);
  a->decor[DECO_STATUE] = art_cell(atlas[4], 4, 5);
  a->decor[DECO_URN] = art_cell(atlas[4], 4, 6);
  a->decor[DECO_CHAINS] = art_cell(atlas[4], 4, 9);
  a->decor[DECO_CANDELABRA] = candelabra;
  a->decor[DECO_CANDLES] = candles;
  for (int i = 0; i < 8; i++) a->decor[DECO_OAK + i] = art_cell(atlas[17], 2, i);
  for (int f = 0; f < 4; f++)
    a->warden[f] = art_cell(atlas[6], 3, f);
  a->prisoner = art_cell(atlas[6], 3, 4);
  a->haunt_shadow = art_cell(atlas[6], 3, 5);
  a->horror_note = art_cell(atlas[5], 4, 5);
  a->white_candles = image_clone(candles);
  Image *paper = art_cell(atlas[7], 3, 5);
  a->note_page = image_new(TEX, TEX);
  gfx_blit(a->note_page, paper, 34, 6, 188, 244, 0, 0, TEX, TEX, 1);
  image_free(paper);
  a->hangings[HANG_SWORD] = art_cell(atlas[4], 4, 14);
  a->hangings[HANG_SHIELD] = art_cell(atlas[4], 4, 13);
  a->hangings[HANG_CHAIN] = image_clone(a->decor[DECO_CHAINS]);
  a->hangings[HANG_FIREPLACE] = image_clone(hearth);
  for (int i = 0; i < 4; i++) {
    a->cobwebs[i] = web(i);
    a->cracks[i] = fracture(i);
  }
  for (int sitter = 0; sitter < PORTRAIT_SITTERS; sitter++) {
    Image *portrait = art_cell(atlas[7], 3, sitter);
    for (int row = 0; row < PORTRAIT_ROWS; row++)
      for (int col = 0; col < PORTRAIT_COLS; col++)
        a->portraits[sitter][row * PORTRAIT_COLS + col] = gaze(portrait, col / (float)(PORTRAIT_COLS - 1) * 2 - 1, row);
    image_free(portrait);
  }
  a->hangings[HANG_PORTRAIT] = a->portraits[0][0];
  a->hangings[HANG_COBWEB] = a->cobwebs[0];
  a->hangings[HANG_CRACK] = a->cracks[0];
  for (int i = 0; i < ATLAS_COUNT; i++)
    image_free(atlas[i]);
  return true;
}
// All members are Image pointers, including the nested arrays/box structs.
// Copy each pointer representation before using it; this avoids aliasing the
// struct as an array and frees shared views exactly once.
void assets_free(Assets *a) {
  Image *seen[1024];
  size_t count = 0;
  for (size_t offset = 0; offset + sizeof(Image *) <= sizeof *a; offset += sizeof(Image *)) {
    Image *im;
    memcpy(&im, (const unsigned char *)a + offset, sizeof im);
    if (!im) continue;
    bool duplicate = false;
    for (size_t i = 0; i < count; i++)
      if (seen[i] == im) {
        duplicate = true;
        break;
      }
    if (!duplicate) {
      seen[count++] = im;
      image_free(im);
    }
  }
  memset(a, 0, sizeof *a);
}
