// Raycasting renderer: walls via DDA (with sliding doors), billboard
// sprites clipped against a z-buffer, POV weapon, HUD and automap.
// Torchlit atmosphere: everything fades into darkness with distance.

#include "render.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "font.h"
#include "ui.h"

#define W SCREEN_W
#define H SCREEN_H
#define MAX_STEPS 128
#define NEAR_PLANE 0.05f

static const Color FOG = {9, 15, 21, 1}; // the darkness everything sinks into

static float fog_amount(float dist) { return minf(0.88f, maxf(0, (dist - 1.4f) / 13)); }

// wall-box furniture: shallow boxes standing flush against walls, in
// fractions of a wall cell. Boxes lower than the eye (0.5) also get a lid.
typedef struct {
  float half_w, depth, height;
} BoxSpec;

static const BoxSpec BOX_SPECS[BOX_KIND_COUNT] = {
    [BOX_WARDROBE] = {0.46f, 0.22f, 0.8f},
    [BOX_CHEST] = {0.31f, 0.24f, 0.42f},
};

// wall hangings: flat trophies just off the wall face, one oriented quad
// each, spanning z_bot..z_top of the wall height
typedef struct {
  float z_bot, z_top, offset;
} HangingSpec;

static const HangingSpec HANGING_SPECS[HANG_KIND_COUNT] = {
    [HANG_SWORD] = {0.3f, 0.9f, 0.03f},    [HANG_SHIELD] = {0.35f, 0.85f, 0.03f},
    [HANG_CHAIN] = {0.22f, 0.92f, 0.03f},  [HANG_PORTRAIT] = {0.32f, 0.84f, 0.03f},
    [HANG_COBWEB] = {0.55f, 1, 0.02f},     [HANG_CRACK] = {0, 1, 0.02f},
};

// distance buckets for pre-darkened sprite variants
static const float SPRITE_SHADES[4] = {1, 0.78f, 0.58f, 0.42f};

static int shade_bucket(float dist) { return dist < 3.5f ? 0 : dist < 6 ? 1 : dist < 9 ? 2 : 3; }

static float torch_flicker(float time, float phase) {
  return 0.84f + sinf(time * 17 + phase) * 0.1f + sinf(time * 29 + phase * 1.7f) * 0.06f;
}

static float torch_light(const Game *g, float x, float y, float time) {
  const Level *lv = g->level;
  float light = 0;
  for (int i = 0; i < lv->torch_count; i++) {
    const Torch *t = &lv->torches[i];
    float dist = hypotf((float)t->x - x, (float)t->y - y);
    if (dist >= 3.4f) continue;
    light = maxf(light, (1 - dist / 3.4f) * torch_flicker(time, (float)t->phase) * horror_torch_strength(g, t->x, t->y));
  }
  // the hearths burn like oversized torches and reach a little further
  for (int i = 0; i < lv->fire_light_count; i++) {
    const Torch *f = &lv->fire_lights[i];
    float dist = hypotf((float)f->x - x, (float)f->y - y);
    if (dist >= 4.2f) continue;
    light = maxf(light, (1 - dist / 4.2f) * torch_flicker(time, (float)f->phase) * horror_torch_strength(g, f->x, f->y));
  }
  for (int i = 0; i < lv->horror.haven_count; i++) {
    const Haven *v = &lv->horror.havens[i];
    float dist = hypotf(x - v->x - .5f, y - v->y - .5f);
    light = maxf(light, 1.1f * maxf(0, 1 - dist / (v->radius + 1.6f)));
  }
  return light;
}

// Uncollected keys shed a faint golden shimmer on the nearby masonry — a
// quiet hint that spills around corners and through open doors. Slower and
// dimmer than the torches: a glow to notice, not a beacon. Each key breathes
// on its own phase, derived from where it lies.
static float key_pulse(float time, float phase) { return 0.72f + sinf(time * 2.6f + phase) * 0.28f; }
static float key_phase(const Item *it) { return it->x * 5.1f + it->y * 7.7f; }

static float key_light(const Game *g, float x, float y) {
  float light = 0;
  for (int i = 0; i < g->item_count; i++) {
    const Item *it = &g->items[i];
    if (it->kind != ITEM_KEY) continue;
    float dist = hypotf(it->x - x, it->y - y);
    if (dist >= 2.75f) continue;
    light = maxf(light, (1 - dist / 2.75f) * key_pulse(g->time, key_phase(it)));
  }
  return light;
}

// The torchlight, key shimmer and fog laid over a surface column, folded
// into one per-channel scale and offset.
typedef SurfaceTint Tint;

static void tint_add(Tint *t, float r, float g, float b, float a) {
  t->k *= 1 - a;
  t->r = t->r * (1 - a) + r * a;
  t->g = t->g * (1 - a) + g * a;
  t->b = t->b * (1 - a) + b * a;
}

static Tint surface_tint(const Game *g, float x, float y, float dist) {
  Tint t = {1, 0, 0, 0};
  float light = torch_light(g, x, y, g->time);
  if (light > 0.015f) tint_add(&t, 255, 161, 69, minf(0.25f, light * 0.25f));
  float glow = key_light(g, x, y);
  if (glow > 0.02f) tint_add(&t, 255, 214, 92, minf(0.1f, glow * 0.1f));
  bool outside = world_outside(g->level, (int)floorf(x), (int)floorf(y));
  float fog = maxf(0, (outside ? minf(.84f, maxf(0, (dist - 3) / 27)) : fog_amount(dist)) - light * 0.42f - glow * 0.3f);
  if (fog > 0.02f) tint_add(&t, FOG.r, FOG.g, FOG.b, fog);
  if (outside) {
    t.k *= .88f;
    tint_add(&t, 32, 48, 57, .09f);
  } else if (g->level->horror.chapter) {
    // Local contrast, a cool unlit distance and a readable area at the feet.
    float brightness = clampf(.54f + light * .52f + maxf(0, 1 - dist / 2) * .19f, .54f, 1);
    t.k *= brightness; t.r *= brightness; t.g *= brightness; t.b *= brightness;
    tint_add(&t, 8, 17, 24, minf(.22f, dist * .024f) * maxf(0, 1 - light));
  }
  return t;
}

static Tint face_tint(const Game *g, float x, float y, float dist, int side, float rx, float ry) {
  // A grid boundary belongs to two lighting sectors. Sample just in front
  // of the face so roundoff cannot alternate exterior and interior fog.
  if (side == 0) x -= copysignf(.001f, rx);
  else y -= copysignf(.001f, ry);
  return surface_tint(g, x, y, dist);
}

static int wall_tex_for(char c) {
  switch (c) {
  case '2': return WT_MOSS;
  case '3': return WT_TIMBER;
  case '4': return WT_DUNGEON;
  case '5': return WT_BRICK;
  case '6': return WT_BANNER;
  case '7': return WT_SLIT;
  case '8': return WT_BREAKABLE;
  case 'B': return WT_BARS;
  case 'D': return WT_DOOR;
  case 'R': return WT_BAR_DOOR;
  case 'X': return WT_GATE;
  case 'N': return WT_NICHE;
  default: return WT_STONE;
  }
}

// The sides of a door or niche opening (and of a secret wall slid aside)
// are its jambs. They keep their wall's masonry but none of its dressing:
// a banner, window, slit or mirror belongs on the face, not in the reveal.
static int jamb_tex(int tex) {
  switch (tex) {
  case WT_DUNGEON: return WT_DUNGEON_BARE;
  case WT_BANNER:
  case WT_SLIT: return WT_STONE;
  default: return tex >= WT_PORTAL && tex <= WT_PORTAL_LAST ? WT_STONE : tex;
  }
}

static const Image *shaded(const Image *img, int bucket) {
  if (bucket == 0) return img;
  Image *m = (Image *)img;
  if (!m->shades[bucket]) m->shades[bucket] = image_darken(img, SPRITE_SHADES[bucket]);
  return m->shades[bucket];
}

// rows [y0, y1) whose pixel centres fall inside [top, top + height)
static bool column_rows(const Image *fb, float top, float height, int *y0, int *y1) {
  *y0 = (int)ceilf(top - 0.5f);
  *y1 = (int)ceilf(top + height - 0.5f);
  if (*y0 < fb->clip_y0) *y0 = fb->clip_y0;
  if (*y1 > fb->clip_y1) *y1 = fb->clip_y1;
  return *y0 < *y1;
}

// One texture column stretched over [top, top + height) at screen column x,
// composited and then tinted as a whole.
static void draw_column(Renderer *r, Image *fb, const Image *tex, int tex_x, int x, float top, float height,
                        const Tint *tint, float dist, bool write_depth) {
  int y0, y1;
  if (height <= 0 || x < fb->clip_x0 || x >= fb->clip_x1 || !column_rows(fb, top, height, &y0, &y1)) return;
  float ky = (float)tex->h / height;
  for (int py = y0; py < y1; py++) {
    int index = py * W + x;
    if (dist > r->depth[index] + .0001f) continue;
    int ty = (int)floorf(((float)py + 0.5f - top) * ky);
    if (ty < 0) ty = 0;
    if (ty >= tex->h) ty = tex->h - 1;
    uint32_t *d = fb->px + (size_t)py * fb->w + x;
    px_over(d, tex->px[ty * tex->w + tex_x], 1);
    if (write_depth) { r->depth[index] = dist; r->floor_mask[index] = 0; }
    if (tint) {
      uint32_t v = *d;
      float uv = ((float)py + .5f - top) / height;
      float ao = .78f + .22f * minf(1, minf(uv, 1 - uv) * 9);
      *d = pack_argb(255, (float)((v >> 16) & 255) * tint->k * ao + tint->r,
                     (float)((v >> 8) & 255) * tint->k * ao + tint->g, (float)(v & 255) * tint->k * ao + tint->b);
    }
  }
}

// Tall masonry keeps its texel scale. Feature textures occupy the bottom
// storey; plain stone continues above doors, banners and carved recesses.
static void draw_wall_column(Renderer *r, Image *fb, const Image *tex, const Image *upper, int tex_x,
                             int x, float base, float ceiling, const Tint *tint, float dist, bool write_depth) {
  float h = VIEW_H / dist;
  int clip0 = fb->clip_y0, clip1 = fb->clip_y1;
  fb->clip_y0 = maxi(clip0, (int)ceilf(VIEW_H / 2.0f + h * (r->eye_z - ceiling) - .5f));
  fb->clip_y1 = mini(clip1, (int)ceilf(VIEW_H / 2.0f + h * (r->eye_z - base) - .5f));
  for (float z = base; z < ceiling; z += 1) {
    draw_column(r, fb, z == base ? tex : upper, tex_x, x,
                VIEW_H / 2.0f + h * (r->eye_z - z - 1), h, tint, dist, write_depth);
  }
  fb->clip_y0 = clip0;
  fb->clip_y1 = clip1;
}

static int tex_column(const Image *tex, float u) {
  return clampi((int)floorf(u * tex->w), 0, tex->w - 1);
}

// camera-space transform of a world point: x across the view, y depth
typedef struct {
  float x, y, u;
} CamPt;

static float inv_det(const Player *p) { return 1 / (p->plane_x * p->dir_y - p->dir_x * p->plane_y); }

static CamPt to_cam(const Player *p, float wx, float wy, float u) {
  float k = inv_det(p);
  float sx = wx - p->x, sy = wy - p->y;
  CamPt c = {k * (p->dir_y * sx - p->dir_x * sy), k * (-p->plane_y * sx + p->plane_x * sy), u};
  return c;
}

static CamPt clip_near(CamPt out, CamPt in) {
  float t = (NEAR_PLANE - out.y) / (in.y - out.y);
  CamPt c = {out.x + (in.x - out.x) * t, NEAR_PLANE, out.u + (in.u - out.u) * t};
  return c;
}

// Projects a vertical quad's bottom edge from (x0,y0) to (x1,y1) and walks
// its screen columns with perspective-correct depth and texture u.
typedef struct {
  float sxa, span, inv_ya, inv_yb, u_ya, u_yb;
  int x_start, x_end;
} QuadSpan;

static bool quad_span(const Player *p, float x0, float y0, float x1, float y1, QuadSpan *q) {
  CamPt a = to_cam(p, x0, y0, 0);
  CamPt b = to_cam(p, x1, y1, 1);
  if (a.y <= NEAR_PLANE && b.y <= NEAR_PLANE) return false;
  if (a.y < NEAR_PLANE)
    a = clip_near(a, b);
  else if (b.y < NEAR_PLANE)
    b = clip_near(b, a);
  float sxa = (W / 2.0f) * (1 + a.x / a.y);
  float sxb = (W / 2.0f) * (1 + b.x / b.y);
  if (sxa > sxb) {
    CamPt t = a;
    a = b;
    b = t;
    float s = sxa;
    sxa = sxb;
    sxb = s;
  }
  q->x_start = maxi(0, (int)ceilf(sxa));
  q->x_end = mini(W - 1, (int)floorf(sxb));
  if (q->x_end < q->x_start) return false;
  q->sxa = sxa;
  q->span = sxb - sxa;
  q->inv_ya = 1 / a.y;
  q->inv_yb = 1 / b.y;
  q->u_ya = a.u * q->inv_ya;
  q->u_yb = b.u * q->inv_yb;
  return true;
}

static void quad_at(const QuadSpan *q, int x, float *dist, float *u) {
  float t = q->span > 1e-6f ? ((float)x - q->sxa) / q->span : 0;
  *dist = 1 / (q->inv_ya + (q->inv_yb - q->inv_ya) * t);
  *u = (q->u_ya + (q->u_yb - q->u_ya) * t) * *dist;
}

// ------------------------------------------------------------------ setup

void renderer_init(Renderer *r, const Assets *assets) {
  memset(r, 0, sizeof *r);
  r->assets = assets;
  r->shake_buf = malloc(sizeof(uint32_t) * W * VIEW_H);
  r->depth = malloc(sizeof(float) * W * VIEW_H);
  r->floor_mask = calloc((size_t)W * VIEW_H, 1);
  // static vignette overlay for the 3D view
  r->vignette = image_new(W, VIEW_H);
  GradientStop stops[2] = {{0, {0, 0, 0, 0}}, {1, {3, 8, 14, 0.48f}}};
  gfx_radial(r->vignette, 0, 0, W, VIEW_H, W / 2.0f, VIEW_H / 2.0f, VIEW_H * 0.45f, W * 0.62f, stops, 2, BLEND_OVER);
}

void renderer_free(Renderer *r) {
  image_free(r->vignette);
  free(r->shake_buf);
  free(r->plane_tints);
  free(r->depth);
  free(r->floor_mask);
  r->vignette = NULL;
  r->shake_buf = NULL;
  r->plane_tints = NULL;
  r->depth = NULL;
  r->floor_mask = NULL;
  r->plane_tint_count = 0;
}

// Calculate illumination once per map corner, then interpolate it across
// the planes. No per-pixel searches through the torches or keys.
static void build_plane_tints(Renderer *r, const Game *g) {
  const Level *lv = g->level;
  size_t count = (size_t)(lv->w + 1) * (lv->h + 1);
  if (count > r->plane_tint_count) {
    r->plane_tints = realloc(r->plane_tints, count * sizeof *r->plane_tints);
    r->plane_tint_count = count;
  }
  for (int y = 0; y <= lv->h; y++) {
    for (int x = 0; x <= lv->w; x++) {
      float dist = (x - g->player.x) * g->player.dir_x + (y - g->player.y) * g->player.dir_y;
      r->plane_tints[y * (lv->w + 1) + x] = surface_tint(g, x, y, maxf(0, dist));
    }
  }
}

static Tint plane_tint(const Renderer *r, int stride, int x, int y, float u, float v) {
  const Tint *a = &r->plane_tints[y * stride + x], *b = a + stride;
  float aa = (1 - u) * (1 - v), ab = u * (1 - v), ba = (1 - u) * v, bb = u * v;
  return (Tint){a[0].k * aa + a[1].k * ab + b[0].k * ba + b[1].k * bb,
                a[0].r * aa + a[1].r * ab + b[0].r * ba + b[1].r * bb,
                a[0].g * aa + a[1].g * ab + b[0].g * ba + b[1].g * bb,
                a[0].b * aa + a[1].b * ab + b[0].b * ba + b[1].b * bb};
}

static uint32_t plane_color(uint32_t texel, Color palette, const Tint *tint) {
  return pack_argb(255, minf(255, ((texel >> 16) & 255) * palette.r * tint->k + tint->r),
                        minf(255, ((texel >> 8) & 255) * palette.g * tint->k + tint->g),
                        minf(255, (texel & 255) * palette.b * tint->k + tint->b));
}

// Row-wise floor casting: the ceiling is the mirrored plane at z=1.
// Texture coordinates stay fixed in the world when moving or turning.
static void render_planes(Renderer *r, Image *fb, const Game *g) {
  const Level *lv = g->level;
  const Player *p = &g->player;
  build_plane_tints(r, g);
  Color floor_palette = rgb(lv->floor_color.r * (1.3f / 128), lv->floor_color.g * (1.3f / 128),
                            lv->floor_color.b * (1.3f / 128));
  Color ceil_palette = rgb(lv->ceil_color.r * (2.1f / 128), lv->ceil_color.g * (2.1f / 128),
                           lv->ceil_color.b * (2.1f / 128));
  float plane_length = hypotf(p->plane_x, p->plane_y);
  float ray_length = maxf(hypotf(p->dir_x - p->plane_x, p->dir_y - p->plane_y),
                          hypotf(p->dir_x + p->plane_x, p->dir_y + p->plane_y));
  for (int y = VIEW_H / 2; y < VIEW_H; y++) {
    float offset = (float)y + .5f - VIEW_H / 2.0f;
    float dist = VIEW_H * .5f / offset;
    float step_x = 2 * p->plane_x * dist / W, step_y = 2 * p->plane_y * dist / W;
    float wx = p->x + (p->dir_x - p->plane_x) * dist;
    float wy = p->y + (p->dir_y - p->plane_y) * dist;
    float footprint = TEX * maxf(dist * ray_length / offset, 2 * plane_length * dist / W);
    int mip = clampi((int)log2f(maxf(1, footprint)), 0, SURFACE_MIPS - 1);
    const Image *floor_tex = r->assets->floors[lv->surface_theme][mip];
    const Image *ceil_tex = r->assets->ceilings[lv->surface_theme][mip];
    uint32_t *floor_row = fb->px + (size_t)y * fb->w;
    uint32_t *ceil_row = fb->px + (size_t)(VIEW_H - 1 - y) * fb->w;
    for (int x = 0; x < W; x++, wx += step_x, wy += step_y) {
      // Keep the walls and closed doors already painted in this column.
      if (dist >= r->zbuffer[x]) continue;
      int cx = (int)floorf(wx), cy = (int)floorf(wy);
      if (cx < 0 || cy < 0 || cx >= lv->w || cy >= lv->h) continue;
      float u = wx - cx, v = wy - cy;
      int tx = mini(floor_tex->w - 1, (int)(u * floor_tex->w));
      int ty = mini(floor_tex->h - 1, (int)(v * floor_tex->h));
      Tint tint = plane_tint(r, lv->w + 1, cx, cy, u, v);
      floor_row[x] = plane_color(floor_tex->px[ty * floor_tex->w + tx], floor_palette, &tint);
      ceil_row[x] = plane_color(ceil_tex->px[ty * ceil_tex->w + tx], ceil_palette, &tint);
      r->depth[y * W + x] = r->depth[(VIEW_H - 1 - y) * W + x] = dist;
      r->floor_mask[y * W + x] = 1;
    }
  }
}

// ------------------------------------------------------------------ walls

typedef struct {
  float dist;
  int side;
  int tex;
  float wall_x;
} Hit;

#define ROOF_THICKNESS .125f

static bool doorway(char cell) { return cell == 'D' || cell == 'R' || cell == 'Z'; }
static bool solid_masonry(char cell) { return !level_is_open(cell) && !doorway(cell) && cell != 'B'; }

static float roof_height(const Level *lv, int x, int y) {
  float roof = lv->ceilings[y * lv->w + x];
  return isfinite(roof) ? roof : level_floor_at(lv, x, y) + 1;
}

// Exterior physics has a high, invisible ceiling. Masonry uses the authored
// roof instead, and reaches the adjacent room's ceiling even on legacy walls
// whose own ceiling was left unspecified.
static float wall_top(const Game *g, int x, int y) {
  const Level *lv = g->level;
  float top = roof_height(lv, x, y);
  static const int dirs[4][2] = {{1,0},{-1,0},{0,1},{0,-1}};
  for (int d = 0; d < 4; d++) {
    int nx = x + dirs[d][0], ny = y + dirs[d][1];
    if (nx < 0 || ny < 0 || nx >= lv->w || ny >= lv->h || world_outside(lv, nx, ny)) continue;
    char next = game_cell(g, nx, ny);
    if (level_is_open(next) || doorway(next)) top = maxf(top, level_ceiling_at(lv, nx, ny));
  }
  return top + ROOF_THICKNESS;
}

// A door's ceiling is the opening, not the height of the surrounding wall.
// Its two jambs carry the full header above the sliding leaf.
static float doorway_top(const Game *g, int x, int y) {
  const Level *lv = g->level;
  float top = roof_height(lv, x, y) + ROOF_THICKNESS;
  static const int dirs[4][2] = {{1,0},{-1,0},{0,1},{0,-1}};
  for (int d = 0; d < 4; d++) {
    int nx = x + dirs[d][0], ny = y + dirs[d][1];
    if (nx < 0 || ny < 0 || nx >= lv->w || ny >= lv->h) continue;
    char next = game_cell(g, nx, ny);
    if (!level_is_open(next) && !doorway(next)) top = maxf(top, wall_top(g, nx, ny));
  }
  return top;
}

static void render_hit(Renderer *r, Image *fb, const Game *g, int x, Hit hit, int cx, int cy, int from_x, int from_y) {
  if (hit.dist <= .0001f) return;
  const Assets *as = r->assets;
  const Level *lv = g->level;
  float camera = 2.0f * x / W - 1;
  float rx = g->player.dir_x + g->player.plane_x * camera;
  float ry = g->player.dir_y + g->player.plane_y * camera;
  float wx = g->player.x + rx * hit.dist, wy = g->player.y + ry * hit.dist;
  Tint tint = face_tint(g, wx, wy, hit.dist, hit.side, rx, ry);
  const Image *tex = (hit.side ? as->walls_dark : as->walls)[hit.tex];
  const Image *upper = (hit.side ? as->walls_dark : as->walls)[WT_STONE];
  float base = minf(level_floor_at(lv, cx, cy), level_floor_at(lv, from_x, from_y));
  float top = doorway(game_cell(g, cx, cy)) ? roof_height(lv, cx, cy) : wall_top(g, cx, cy);
  draw_wall_column(r, fb, tex, upper, tex_column(tex, hit.wall_x), x, base, top, &tint, hit.dist, true);
}

static void push_overlay(Renderer *r, int column, float dist, int side, int tex, float wall_x, float floor, float ceiling) {
  if (r->overlay_count[column] >= MAX_OVERLAYS) return;
  r->overlays[column][r->overlay_count[column]++] = (Overlay){dist, side, tex, wall_x, floor, ceiling};
}

static Hit cast_ray(Renderer *r, Image *fb, const Game *g, int column, float pos_x, float pos_y, float ray_x, float ray_y) {
  const Level *lv = g->level;
  int map_x = (int)floorf(pos_x), map_y = (int)floorf(pos_y);
  float delta_x = fabsf(1 / ray_x), delta_y = fabsf(1 / ray_y);
  int step_x, step_y;
  float side_x, side_y;
  if (ray_x < 0) {
    step_x = -1;
    side_x = (pos_x - map_x) * delta_x;
  } else {
    step_x = 1;
    side_x = (map_x + 1 - pos_x) * delta_x;
  }
  if (ray_y < 0) {
    step_y = -1;
    side_y = (pos_y - map_y) * delta_y;
  } else {
    step_y = 1;
    side_y = (map_y + 1 - pos_y) * delta_y;
  }

  int side = 0;
  // Outdoor walls cover only part of a column. Keep traversing so taller
  // walls and scenery beyond a parapet can pass the per-pixel depth test.
  Hit first = {1e9f, 0, WT_STONE, 0};
  r->overlay_count[column] = 0;
  for (int i = 0; i < MAX_STEPS; i++) {
    if (side_x < side_y) {
      side_x += delta_x;
      map_x += step_x;
      side = 0;
    } else {
      side_y += delta_y;
      map_y += step_y;
      side = 1;
    }
    if (map_x < 0 || map_y < 0 || map_x >= lv->w || map_y >= lv->h) break;
    char cell = game_cell(g, map_x, map_y);
    if (level_is_open(cell)) continue;

    if (cell == 'D' || cell == 'R' || cell == 'N') {
      // sliding door (and niche): test the plane in the middle of the cell;
      // a niche's face is carved back to it, and rays that clip past the
      // opening run on into the neighbours, whose sides become the jambs
      float dist_mid, coord;
      if (side == 0) {
        dist_mid = (map_x - pos_x + (1 - step_x) / 2 + step_x * 0.5f) / ray_x;
        coord = pos_y + dist_mid * ray_y - map_y;
      } else {
        dist_mid = (map_y - pos_y + (1 - step_y) / 2 + step_y * 0.5f) / ray_y;
        coord = pos_x + dist_mid * ray_x - map_x;
      }
      if (cell == 'N') {
        if (coord >= 0 && coord < 1) {
          Hit hit = {dist_mid, side, WT_NICHE, coord};
          if (!lv->world.cells) return hit;
          if (first.dist == 1e9f) first = hit;
          render_hit(r, fb, g, column, hit, map_x, map_y, map_x, map_y);
        }
        continue;
      }
      float open = game_door_open_amount(g, map_x, map_y);
      if (coord >= 0 && coord < 1 && coord >= open) {
        if (cell == 'R') {
          push_overlay(r, column, dist_mid, side, WT_BAR_DOOR, coord - open,
                       level_floor_at(lv, map_x, map_y),
                       lv->world.cells ? roof_height(lv, map_x, map_y) : level_ceiling_at(lv, map_x, map_y));
          continue;
        }
        Hit hit = {dist_mid, side, WT_DOOR, coord - open};
        if (!lv->world.cells) return hit;
        if (first.dist == 1e9f) first = hit;
        render_hit(r, fb, g, column, hit, map_x, map_y, map_x, map_y);
      }
      continue; // door open at this spot — ray passes through
    }

    // solid wall (or exit door "X")
    float dist, wall_x;
    if (side == 0) {
      dist = (map_x - pos_x + (1 - step_x) / 2) / ray_x;
      wall_x = pos_y + dist * ray_y;
    } else {
      dist = (map_y - pos_y + (1 - step_y) / 2) / ray_y;
      wall_x = pos_x + dist * ray_x;
    }
    wall_x -= floorf(wall_x);
    if ((side == 0 && ray_x > 0) || (side == 1 && ray_y < 0)) wall_x = 1 - wall_x;
    if (cell == 'B') {
      push_overlay(r, column, dist, side, WT_BARS, wall_x,
                   level_floor_at(lv, map_x, map_y),
                   lv->world.cells ? wall_top(g, map_x, map_y) : level_ceiling_at(lv, map_x, map_y));
      continue;
    }
    int tex;
    if (cell == '8') {
      int hp = g->wall_health[map_y * lv->w + map_x];
      tex = WT_BREAKABLE + (hp <= BREAKABLE_WALL_HEALTH / 3 ? 2 : hp <= BREAKABLE_WALL_HEALTH * 2 / 3 ? 1 : 0);
    } else if (cell == '~') {
      // mirror portal: the vortex in the glass churns through four texture
      // frames keyed to the game clock
      tex = WT_PORTAL + ((int)floorf(g->time * 7) & 3);
    } else if (cell == 'Z') {
      // secret wall: disguised in the neighbours' masonry, and unlike the
      // recessed doors it sits flush on the cell face; when its lever is
      // pulled it slides aside exactly like a door leaf
      const Door *door = game_door_at((Game *)g, map_x, map_y);
      float open = door ? door->open : 0;
      if (wall_x < open) continue; // ray slips through the revealed gap
      tex = wall_tex_for(door ? door->tex : '1');
      wall_x -= open;
    } else if (cell == 'L') {
      // the handle hangs on a single face; the ray sees it only when it
      // struck that face — every other side is plain masonry
      const Lever *lever = game_lever_at(g, map_x, map_y);
      int face_x = side == 0 ? -step_x : 0;
      int face_y = side == 1 ? -step_y : 0;
      bool on_face = lever && lever->dx == face_x && lever->dy == face_y;
      tex = on_face ? (game_lever_pulled(g, map_x, map_y) ? WT_LEVER_PULLED : WT_LEVER)
                    : wall_tex_for(lever ? lever->tex : '1');
    } else {
      tex = wall_tex_for(cell);
    }
    // a ray that comes out of a door, a niche or an opened secret wall has
    // struck the side of that opening
    char from = game_cell(g, side == 0 ? map_x - step_x : map_x, side == 1 ? map_y - step_y : map_y);
    if (from == 'D' || from == 'R' || from == 'N' || from == 'Z') tex = jamb_tex(tex);
    Hit hit = {dist, side, tex, wall_x};
    if (!lv->world.cells) return hit;
    if (first.dist == 1e9f) first = hit;
    render_hit(r, fb, g, column, hit, map_x, map_y, side == 0 ? map_x - step_x : map_x,
               side == 1 ? map_y - step_y : map_y);
  }
  return first;
}

// Walk every sector crossed by a column. Each tread owns a horizontal span;
// height changes expose stone risers and ceiling soffits. Depth is stored per
// pixel so a ledge hides feet while leaving the upper body visible.
static void sector_plane(Renderer *r, Image *fb, const Game *g, int x, float rx, float ry,
                          float enter, float exit, float z, bool ceiling, bool cap) {
  float rise = r->eye_z - z;
  if ((!ceiling && rise <= 0) || (ceiling && rise >= 0)) return;
  float a = VIEW_H / 2.0f + rise * VIEW_H / maxf(.001f, enter);
  float b = VIEW_H / 2.0f + rise * VIEW_H / maxf(.001f, exit);
  int y0 = maxi(0, (int)ceilf(minf(a, b) - .5f));
  int y1 = mini(VIEW_H, (int)ceilf(maxf(a, b) - .5f));
  const Level *lv = g->level;
  Color palette = ceiling ? rgb(lv->ceil_color.r * (2.1f / 128), lv->ceil_color.g * (2.1f / 128), lv->ceil_color.b * (2.1f / 128))
                          : rgb(lv->floor_color.r * (1.3f / 128), lv->floor_color.g * (1.3f / 128), lv->floor_color.b * (1.3f / 128));
  if (cap) palette = rgb(.95f, .95f, .95f);
  for (int y = y0; y < y1; y++) {
    float offset = y + .5f - VIEW_H / 2.0f;
    float dist = rise * VIEW_H / offset;
    int index = y * W + x;
    if (dist < enter - .0001f || dist > exit + .0001f || dist >= r->depth[index]) continue;
    float wx = g->player.x + rx * dist, wy = g->player.y + ry * dist;
    int cx = (int)floorf(wx), cy = (int)floorf(wy);
    if (cx < 0 || cy < 0 || cx >= lv->w || cy >= lv->h) continue;
    float u = wx - cx, v = wy - cy;
    float footprint = TEX * maxf(dist * hypotf(rx, ry) / fabsf(offset), 1.32f * dist / W);
    int mip = clampi((int)log2f(maxf(1, footprint)), 0, SURFACE_MIPS - 1);
    GroundKind ground = world_ground(lv, cx, cy);
    const Image *tex = cap ? r->assets->floors[lv->surface_theme][mip] : ceiling ? r->assets->ceilings[lv->surface_theme][mip] :
                       ground ? r->assets->grounds[ground - 1][mip] : r->assets->floors[lv->surface_theme][mip];
    int tx = mini(tex->w - 1, (int)(u * tex->w)), ty = mini(tex->h - 1, (int)(v * tex->h));
    Tint tint = plane_tint(r, lv->w + 1, cx, cy, u, v);
    Color local_palette = !ceiling && !cap && ground ? rgb(1.13f, 1.13f, 1.13f) : palette;
    fb->px[(size_t)y * fb->w + x] = plane_color(tex->px[ty * tex->w + tx], local_palette, &tint);
    r->depth[index] = dist;
    r->floor_mask[index] = !ceiling && !cap;
  }
}

static void sector_riser(Renderer *r, Image *fb, const Game *g, int x, float rx, float ry,
                          float dist, int side, float bottom, float top) {
  if (dist < .001f || top <= bottom) return;
  float h = VIEW_H / dist;
  float screen_top = VIEW_H / 2.0f + h * (r->eye_z - top);
  int y0, y1;
  if (!column_rows(fb, screen_top, h * (top - bottom), &y0, &y1)) return;
  float wx = g->player.x + rx * dist, wy = g->player.y + ry * dist;
  float along = side ? wx : wy;
  along -= floorf(along);
  const Image *tex = (side ? r->assets->walls_dark : r->assets->walls)[WT_STONE];
  Tint tint = face_tint(g, wx, wy, dist, side, rx, ry);
  for (int y = y0; y < y1; y++) {
    int index = y * W + x;
    if (dist >= r->depth[index]) continue;
    float z = r->eye_z - (y + .5f - VIEW_H / 2.0f) / h;
    float v = top - z;
    v -= floorf(v);
    uint32_t color = tex->px[tex_column(tex, v) * tex->w + tex_column(tex, along)];
    float bevel = top - z < .009f ? 1.16f : z - bottom < .012f ? .62f : .82f;
    fb->px[(size_t)y * fb->w + x] = pack_argb(255,
        minf(255, ((color >> 16) & 255) * bevel * tint.k + tint.r),
        minf(255, ((color >> 8) & 255) * bevel * tint.k + tint.g),
        minf(255, (color & 255) * bevel * tint.k + tint.b));
    r->depth[index] = dist;
    r->floor_mask[index] = 0;
  }
}

static void render_sectors(Renderer *r, Image *fb, const Game *g) {
  build_plane_tints(r, g);
  const Player *p = &g->player;
  const Level *lv = g->level;
  bool volumes = lv->world.cells != NULL;
  for (int x = 0; x < W; x++) {
    float camera = 2.0f * x / W - 1;
    float rx = p->dir_x + p->plane_x * camera, ry = p->dir_y + p->plane_y * camera;
    int cx = (int)floorf(p->x), cy = (int)floorf(p->y);
    int sx = rx < 0 ? -1 : 1, sy = ry < 0 ? -1 : 1;
    float dx = rx != 0 ? fabsf(1 / rx) : INFINITY, dy = ry != 0 ? fabsf(1 / ry) : INFINITY;
    float side_x = (rx < 0 ? p->x - cx : cx + 1 - p->x) * dx;
    float side_y = (ry < 0 ? p->y - cy : cy + 1 - p->y) * dy;
    float enter = 0;
    for (int i = 0; i < MAX_STEPS; i++) {
      float exit = minf(side_x, side_y);
      if (!volumes) exit = minf(r->zbuffer[x], exit);
      float floor = level_floor_at(g->level, cx, cy);
      float ceiling = volumes ? roof_height(lv, cx, cy) : level_ceiling_at(lv, cx, cy);
      bool outside = world_outside(g->level, cx, cy);
      char cell = game_cell(g, cx, cy);
      bool door = doorway(cell);
      bool solid = volumes && solid_masonry(cell);
      float top = solid ? wall_top(g, cx, cy) : door ? doorway_top(g, cx, cy) : ceiling + ROOF_THICKNESS;
      if (solid) {
        // A carved niche still has a floor in front of its recessed face.
        if (cell == 'N') sector_plane(r, fb, g, x, rx, ry, enter, exit, floor, false, false);
        sector_plane(r, fb, g, x, rx, ry, enter, exit, top, false, true);
      } else {
        sector_plane(r, fb, g, x, rx, ry, enter, exit, floor, false, false);
        if (!outside || (volumes && door)) {
          sector_plane(r, fb, g, x, rx, ry, enter, exit, ceiling, true, false);
          if (volumes) sector_plane(r, fb, g, x, rx, ry, enter, exit, top, false, true);
        }
      }
      if (!volumes && exit >= r->zbuffer[x]) break;
      int side;
      if (side_x < side_y) { side_x += dx; cx += sx; side = 0; }
      else { side_y += dy; cy += sy; side = 1; }
      if (cx < 0 || cy < 0 || cx >= g->level->w || cy >= g->level->h) break;
      float next = level_floor_at(g->level, cx, cy);
      float next_ceiling = volumes ? roof_height(lv, cx, cy) : level_ceiling_at(lv, cx, cy);
      char next_cell = game_cell(g, cx, cy);
      // Opaque wall faces already contain their roof edge and floor riser.
      // Painting a second, coplanar surface makes roundoff expose alternating
      // columns of two different shades. Niches retain their recessed face.
      if ((solid && cell != 'N') || (volumes && solid_masonry(next_cell) && next_cell != 'N')) {
        enter = exit;
        continue;
      }
      if (next != floor)
        sector_riser(r, fb, g, x, rx, ry, exit, side, minf(floor, next), maxf(floor, next));
      bool next_outside = world_outside(g->level, cx, cy);
      if (volumes && door)
        sector_riser(r, fb, g, x, rx, ry, exit, side, ceiling, top);
      if (volumes && doorway(next_cell))
        sector_riser(r, fb, g, x, rx, ry, exit, side, next_ceiling, doorway_top(g, cx, cy));
      if (next_ceiling != ceiling && !outside && !next_outside)
        sector_riser(r, fb, g, x, rx, ry, exit, side, minf(ceiling, next_ceiling),
                     maxf(ceiling, next_ceiling) + (volumes ? ROOF_THICKNESS : 0));
      else if (outside != next_outside) {
        float eave = outside ? next_ceiling : ceiling;
        // Only the edge of a real roof exists at a threshold; never erect a
        // wall from its lintel all the way to the exterior's physics ceiling.
        sector_riser(r, fb, g, x, rx, ry, exit, side, eave, eave + ROOF_THICKNESS);
      }
      enter = exit;
    }
  }
}

// Cast pits, shallow water and pressure plates into the floor plane. These
// surfaces never write the column z-buffer: sight and arrows pass above them.
static void render_floor_features(Renderer *r, Image *fb, const Game *g) {
  if (!g->level->pit_count && !g->level->water_count && !g->level->horror.trace_count && !g->trap_count) return;
  const Player *p = &g->player;
  const Image *stone = r->assets->walls[WT_STONE];
  int frame = (int)fmodf(g->time * 8, WATER_FRAMES);
  float drift = fmodf(g->time * .35f, 1);
  for (int x = 0; x < W; x++) {
    float camera_x = 2.0f * x / W - 1;
    float rx = p->dir_x + p->plane_x * camera_x;
    float ry = p->dir_y + p->plane_y * camera_x;
    int last_cell = -1;
    int trace = -1;
    const Trap *plate = NULL;
    int flow_x = 0, flow_y = 0, banks = 0;
    float light = 0;
    Tint tint = {1, 0, 0, 0};
    for (int y = VIEW_H / 2; y < VIEW_H; y++) {
      float slope = ((float)y + 0.5f - VIEW_H / 2.0f) / VIEW_H;
      float dist = (g->level->has_heights || g->level->has_ceilings) ? r->depth[y * W + x] : 0.5f / slope;
      if (!r->floor_mask[y * W + x] || (!g->level->world.cells && dist >= r->zbuffer[x])) continue;
      float wx = p->x + rx * dist, wy = p->y + ry * dist;
      int cx = (int)floorf(wx), cy = (int)floorf(wy);
      char kind = game_cell(g, cx, cy);
      bool water = level_is_water(kind);
      int cell = cy * g->level->w + cx;
      if (cell != last_cell) {
        trace = -1;
        for (int i = 0; i < g->level->horror.trace_count; i++)
          if (g->level->horror.traces[i].x == cx && g->level->horror.traces[i].y == cy) trace = i;
        plate = NULL;
        for (int i = 0; i < g->trap_count; i++)
          if (g->traps[i].spawn->x == cx && g->traps[i].spawn->y == cy) plate = &g->traps[i];
        last_cell = cell;
        if (kind != '!' && !water && trace < 0 && !plate) continue;
        float depth = (cx + 0.5f - p->x) * p->dir_x + (cy + 0.5f - p->y) * p->dir_y;
        tint = surface_tint(g, cx + 0.5f, cy + 0.5f, maxf(0, depth));
        if (water) {
          level_water_flow(kind, &flow_x, &flow_y);
          banks = (!level_is_water(game_cell(g, cx - 1, cy)) ? 1 : 0) |
                  (!level_is_water(game_cell(g, cx + 1, cy)) ? 2 : 0) |
                  (!level_is_water(game_cell(g, cx, cy - 1)) ? 4 : 0) |
                  (!level_is_water(game_cell(g, cx, cy + 1)) ? 8 : 0);
          light = torch_light(g, cx + .5f, cy + .5f, g->time);
        }
        last_cell = cell;
      }
      if (kind != '!' && !water && trace < 0 && !plate) continue;
      float u = wx - cx, v = wy - cy;
      const Image *decal = NULL;
      if (plate) {
        // A fixed 0.6-tile square lies flush with the local floor instead
        // of standing upright as a camera-facing sprite.
        u = (u - .2f) / .6f;
        v = (v - .2f) / .6f;
        if (u < 0 || u >= 1 || v < 0 || v >= 1) continue;
        decal = r->assets->trap[plate->state == TRAP_ARMED ? TRAP_PLATE : TRAP_PRESSED];
      } else if (trace >= 0) {
        decal = r->assets->horror_traces[g->level->horror.traces[trace].kind];
      }
      if (decal) {
        uint32_t c = decal->px[tex_column(decal, v) * decal->w + tex_column(decal, u)];
        if (c >> 24) {
          float a = (c >> 24) / 255.0f;
          px_blend(&fb->px[(size_t)y * fb->w + x], ((c >> 16) & 255) / a * tint.k + tint.r,
                   ((c >> 8) & 255) / a * tint.k + tint.g, (c & 255) / a * tint.k + tint.b, a);
        }
        continue;
      }
      float edge = minf(minf(u, 1 - u), minf(v, 1 - v));
      uint32_t color;
      if (water) {
        // Only the outside shoreline gets a lip: adjacent water cells
        // merge into one basin, with uninterrupted world-space ripples.
        float bank = 1;
        if (banks & 1) bank = minf(bank, u);
        if (banks & 2) bank = minf(bank, 1 - u);
        if (banks & 4) bank = minf(bank, v);
        if (banks & 8) bank = minf(bank, 1 - v);
        if (bank < WATER_RIM) {
          color = stone->px[tex_column(stone, v) * stone->w + tex_column(stone, u)];
          float k = bank > WATER_RIM - .015f ? .52f : .9f;
          color = pack_argb(255, ((color >> 16) & 255) * k, ((color >> 8) & 255) * k, (color & 255) * k);
        } else {
          bool flowing = flow_x || flow_y;
          float tu = wx * .5f, tv = wy * .5f;
          if (flowing) {
            tu = (wx * flow_x + wy * flow_y) * .5f - drift;
            tv = (-wx * flow_y + wy * flow_x) * .5f;
          }
          tu -= floorf(tu);
          tv -= floorf(tv);
          const Image *tex = r->assets->water[flowing][frame];
          color = tex->px[tex_column(tex, tv) * tex->w + tex_column(tex, tu)];
          float red = (color >> 16) & 255, green = (color >> 8) & 255, blue = color & 255;
          // Pale shoals and broken warm glints under the wall torches.
          float shoal = maxf(0, 1 - (bank - WATER_RIM) / .14f);
          float glint = maxf(0, (green - 65) / 75) * light;
          color = pack_argb(255, red + shoal * 16 + glint * 78,
                                green + shoal * 12 + glint * 34, blue + shoal * 5);
        }
      } else if (edge < PIT_RIM) {
        // The rim's lighter bevel makes the opening readable in the gloom.
        color = stone->px[tex_column(stone, v) * stone->w + tex_column(stone, u)];
        float k = edge > PIT_RIM - 0.016f ? 1.18f : edge < 0.012f ? 0.55f : 0.85f;
        color = pack_argb(255, minf(255, ((color >> 16) & 255) * k),
                          minf(255, ((color >> 8) & 255) * k), minf(255, (color & 255) * k));
      } else {
        // Continue the same ray until it meets the inner wall of the shaft.
        float ex = rx != 0 ? ((rx > 0 ? cx + 1 - PIT_RIM : cx + PIT_RIM) - p->x) / rx : INFINITY;
        float ey = ry != 0 ? ((ry > 0 ? cy + 1 - PIT_RIM : cy + PIT_RIM) - p->y) / ry : INFINITY;
        float exit = minf(ex, ey);
        float depth = maxf(0, exit * slope - (r->eye_z - level_floor_at(g->level, cx, cy)));
        float along = ex < ey ? p->y + ry * exit : p->x + rx * exit;
        along -= floorf(along);
        float tz = depth - floorf(depth);
        color = stone->px[tex_column(stone, tz) * stone->w + tex_column(stone, along)];
        float shade = maxf(0.025f, 0.65f * expf(-depth * 3.2f));
        if (ex < ey) shade *= 0.78f;
        color = pack_argb(255, ((color >> 16) & 255) * shade,
                          ((color >> 8) & 255) * shade, (color & 255) * shade);
      }
      fb->px[(size_t)y * fb->w + x] = pack_argb(255, ((color >> 16) & 255) * tint.k + tint.r,
                                               ((color >> 8) & 255) * tint.k + tint.g,
                                               (color & 255) * tint.k + tint.b);
    }
  }
}

// Transparent walls are drawn over the opaque scene first; sprites then
// redraw only the bars that are closer than them, which keeps actors
// correctly in front of or behind a barred surface.
static void render_transparent_walls(Renderer *r, Image *fb, int start_x, int end_x, float in_front_of) {
  for (int x = maxi(0, start_x); x <= mini(W - 1, end_x); x++) {
    // rays meet the bars near-to-far; painting needs the reverse order
    for (int i = r->overlay_count[x] - 1; i >= 0; i--) {
      const Overlay *o = &r->overlays[x][i];
      if (o->dist >= in_front_of || o->dist <= 0.0001f) continue;
      const Image *tex = (o->side == 1 ? r->assets->walls_dark : r->assets->walls)[o->tex];
      draw_wall_column(r, fb, tex, tex, tex_column(tex, o->wall_x), x, o->floor, o->ceiling, NULL, o->dist, false);
    }
  }
}

// ------------------------------------------------------------------ boxes

// Perspective-correct textured vertical quad from (x0,y0) to (x1,y1) with
// outward normal (nx,ny), lit and fogged exactly like the walls.
static void draw_box_face(Renderer *r, Image *fb, const Game *g, float x0, float y0, float x1, float y1, float nx,
                          float ny, const BoxTextures *texs, bool front, float height) {
  const Player *p = &g->player;
  if (((x0 + x1) / 2 - p->x) * nx + ((y0 + y1) / 2 - p->y) * ny >= 0) return; // backface
  QuadSpan q;
  if (!quad_span(p, x0, y0, x1, y1, &q)) return;

  // faces along the y axis take the darker set, like side==1 walls
  bool dark = fabsf(ny) > fabsf(nx);
  const Image *tex = front ? (dark ? texs->front_dark : texs->front) : (dark ? texs->side_dark : texs->side);
  for (int x = q.x_start; x <= q.x_end; x++) {
    float dist, u;
    quad_at(&q, x, &dist, &u);
    if (!g->level->world.cells && dist >= r->zbuffer[x]) continue;
    float line_h = VIEW_H / dist;
    float base = level_floor(g->level, (x0 + x1) * .5f + nx * .1f, (y0 + y1) * .5f + ny * .1f);
    float top = VIEW_H / 2.0f + line_h * (r->eye_z - base - height);
    Tint tint = surface_tint(g, x0 + (x1 - x0) * u, y0 + (y1 - y0) * u, dist);
    draw_column(r, fb, tex, tex_column(tex, u), x, top, line_h * height, &tint, dist, true);
    if (!(g->level->has_heights || g->level->has_ceilings)) r->zbuffer[x] = dist;
  }
}

// Horizontal lid of a low box, seen because the eye (0.5) sits above it.
// Per column the ray is intersected with the box footprint (slab test in
// the box frame); the [near, far] depth span projects to a vertical screen
// strip filled from one lid texture column. With the unnormalised ray
// direction dir + plane*cameraX, the ray parameter IS the perpendicular
// depth, so it compares directly against the z-buffer. The lid never
// writes depth: it always sits directly above its own faces on screen.
static void draw_box_lid(Renderer *r, Image *fb, const Game *g, const WallBox *wd, const BoxSpec *spec,
                         const Image *tex) {
  const Player *p = &g->player;
  float fx = wd->x + 0.5f + wd->dx * 0.5f; // centre of the wall face
  float fy = wd->y + 0.5f + wd->dy * 0.5f;
  float tx = (float)-wd->dy, ty = (float)wd->dx;
  // player position in the box frame: n out from the wall, t along it
  float pn = (p->x - fx) * wd->dx + (p->y - fy) * wd->dy;
  float pt = (p->x - fx) * tx + (p->y - fy) * ty;

  // conservative column range from the four footprint corners
  float k = inv_det(p);
  float sx0 = W, sx1 = -1;
  bool clipped = false;
  const float corners[4][2] = {
      {0, -spec->half_w}, {0, spec->half_w}, {spec->depth, -spec->half_w}, {spec->depth, spec->half_w}};
  for (int i = 0; i < 4; i++) {
    float wx = fx + wd->dx * corners[i][0] + tx * corners[i][1] - p->x;
    float wy = fy + wd->dy * corners[i][0] + ty * corners[i][1] - p->y;
    float cam_y = k * (-p->plane_y * wx + p->plane_x * wy);
    if (cam_y <= 0.05f) {
      clipped = true;
      continue;
    }
    float sx = (W / 2.0f) * (1 + (k * (p->dir_y * wx - p->dir_x * wy)) / cam_y);
    sx0 = minf(sx0, sx);
    sx1 = maxf(sx1, sx);
  }
  if (clipped) {
    sx0 = 0;
    sx1 = W - 1;
  }
  int x_start = maxi(0, (int)floorf(sx0));
  int x_end = mini(W - 1, (int)ceilf(sx1));

  for (int x = x_start; x <= x_end; x++) {
    float camera_x = 2.0f * x / W - 1;
    float rdx = p->dir_x + p->plane_x * camera_x;
    float rdy = p->dir_y + p->plane_y * camera_x;
    float rn = rdx * wd->dx + rdy * wd->dy;
    float rt = rdx * tx + rdy * ty;
    float s_near = 0.05f, s_far = INFINITY;
    if (fabsf(rn) < 1e-9f) {
      if (pn < 0 || pn > spec->depth) continue;
    } else {
      float a = (0 - pn) / rn, b = (spec->depth - pn) / rn;
      s_near = maxf(s_near, minf(a, b));
      s_far = maxf(a, b);
    }
    if (fabsf(rt) < 1e-9f) {
      if (pt < -spec->half_w || pt > spec->half_w) continue;
    } else {
      float a = (-spec->half_w - pt) / rt, b = (spec->half_w - pt) / rt;
      s_near = maxf(s_near, minf(a, b));
      s_far = minf(s_far, maxf(a, b));
    }
    if (s_far <= s_near || (!g->level->world.cells && s_near >= r->zbuffer[x])) continue;

    float base = level_floor_at(g->level, wd->x + wd->dx, wd->y + wd->dy);
    float rise = (r->eye_z - base - spec->height) * VIEW_H; // eye sits above the lid
    float y_top = VIEW_H / 2.0f + rise / s_far;
    float y_bot = VIEW_H / 2.0f + rise / s_near;
    if (y_bot - y_top < 0.05f) continue;
    float s_mid = (s_near + s_far) / 2;
    float u = (pt + s_mid * rt + spec->half_w) / (spec->half_w * 2);
    Tint tint = surface_tint(g, p->x + rdx * s_mid, p->y + rdy * s_mid, s_mid);
    draw_column(r, fb, tex, tex_column(tex, u), x, y_top, y_bot - y_top, &tint, s_near, true);
  }
}

// A wall box (wardrobe, chest) stands flush against a wall cell: a front
// face and two short sides, each a vertical quad, plus a lid when the box
// is low enough to be looked down upon. The wall behind keeps its normal
// texture; the box simply draws over part of it. After backface culling
// the visible faces never overlap on screen, so draw order within one box
// does not matter — except the lid, drawn first so its own front face
// (sharing the entry depth) cannot z-reject it.
static void render_wall_boxes(Renderer *r, Image *fb, const Game *g) {
  const Level *lv = g->level;
  for (int i = 0; i < lv->wall_box_count; i++) {
    const WallBox *wd = &lv->wall_boxes[i];
    const BoxSpec *spec = &BOX_SPECS[wd->kind];
    const BoxTextures *texs = &r->assets->boxes[wd->kind];
    if (level_floor_at(lv, wd->x + wd->dx, wd->y + wd->dy) + spec->height < r->eye_z && texs->lid) draw_box_lid(r, fb, g, wd, spec, texs->lid);
    float fx = wd->x + 0.5f + wd->dx * 0.5f; // centre of the wall face
    float fy = wd->y + 0.5f + wd->dy * 0.5f;
    float tx = (float)-wd->dy, ty = (float)wd->dx; // tangent along the wall
    float ax = fx + tx * spec->half_w, ay = fy + ty * spec->half_w; // corners at the wall
    float bx = fx - tx * spec->half_w, by = fy - ty * spec->half_w;
    float nx = wd->dx * spec->depth, ny = wd->dy * spec->depth; // out to the front corners
    draw_box_face(r, fb, g, ax + nx, ay + ny, bx + nx, by + ny, (float)wd->dx, (float)wd->dy, texs, true,
                  spec->height);
    draw_box_face(r, fb, g, ax + nx, ay + ny, ax, ay, tx, ty, texs, false, spec->height);
    draw_box_face(r, fb, g, bx + nx, by + ny, bx, by, -tx, -ty, texs, false, spec->height);
  }
}

// ------------------------------------------------------------------ hangings

static bool ray_interval(float origin, float dir, float lo, float hi, float *enter, float *exit) {
  if (fabsf(dir) < 1e-7f) return origin >= lo && origin <= hi;
  float a = (lo - origin) / dir, b = (hi - origin) / dir;
  *enter = maxf(*enter, minf(a, b));
  *exit = minf(*exit, maxf(a, b));
  return *enter <= *exit;
}

static bool fireplace_opening(float u, float v) {
  float arch = (u - .5f) / .235f;
  return fabsf(arch) < 1 && v > .415f + .085f * arch * arch && v < .89f;
}

static void render_fireplace(Renderer *r, Image *fb, const Game *g, const Hanging *hg) {
  const Player *p = &g->player;
  const float width = .62f, height = .62f, projection = .045f, recess = .14f;
  float cx = hg->x + .5f + hg->dx * .5f, cy = hg->y + .5f + hg->dy * .5f;
  float tx = -hg->dy, ty = hg->dx;
  float across = (p->x - cx) * tx + (p->y - cy) * ty;
  float normal = (p->x - cx) * hg->dx + (p->y - cy) * hg->dy;
  if (normal < 0) return;
  float eye = r->eye_z - level_floor_at(g->level, hg->x + hg->dx, hg->y + hg->dy);
  int frame = ((int)floorf(g->time * 18) + (int)(hg->seed % FIREPLACE_FRAMES)) % FIREPLACE_FRAMES;
  const Image *tex = r->assets->fireplaces[frame];
  const Image *stone = r->assets->hangings[HANG_FIREPLACE];

  // Only the shallow stone surround projects into the room. An arched
  // opening continues into the wall, with solid reveals in front of the
  // animated firebox. Carve only pixels belonging to this supporting face;
  // foreground geometry and the rest of the masonry keep their depth.
  for (int x = fb->clip_x0; x < fb->clip_x1; x++) {
    float camera_x = 2.0f * x / W - 1;
    float rx = p->dir_x + p->plane_x * camera_x, ry = p->dir_y + p->plane_y * camera_x;
    float da = rx * tx + ry * ty, dn = rx * hg->dx + ry * hg->dy;
    float near = NEAR_PLANE, far = INFINITY;
    if (!ray_interval(across, da, -width / 2, width / 2, &near, &far) ||
        !ray_interval(normal, dn, -recess, projection, &near, &far)) continue;
    float wall_dist = dn < -1e-7f ? -normal / dn : INFINITY;
    float back_dist = dn < -1e-7f ? (-recess - normal) / dn : INFINITY;
    float top = VIEW_H * .5f + minf((eye - height) * VIEW_H / near, (eye - height) * VIEW_H / far);
    float bottom = VIEW_H * .5f + maxf(eye * VIEW_H / near, eye * VIEW_H / far);
    int y0 = maxi(fb->clip_y0, (int)ceilf(top - .5f));
    int y1 = mini(fb->clip_y1, (int)ceilf(bottom - .5f));
    Tint tint = surface_tint(g, cx + hg->dx * projection, cy + hg->dy * projection, near);
    for (int y = y0; y < y1; y++) {
      int index = y * W + x;
      float dz = (VIEW_H * .5f - y - .5f) / VIEW_H;
      float wall_u = .5f - (across + da * wall_dist) / width;
      float wall_v = 1 - (eye + dz * wall_dist) / height;
      bool opening = isfinite(wall_dist) && fabsf(r->depth[index] - wall_dist) < .001f &&
                     fireplace_opening(wall_u, wall_v);
      float enter = near, exit = opening ? far : minf(far, r->depth[index]);
      if (!ray_interval(eye, dz, 0, height, &enter, &exit)) continue;
      float dist = minf(enter + .00001f, exit);
      float u = (.5f - (across + da * dist) / width) * tex->w;
      float v = (1 - (eye + dz * dist) / height) * tex->h;
      float du = -da / width * tex->w, dv = -dz / height * tex->h;
      int ix = clampi((int)floorf(u), 0, tex->w - 1), iy = clampi((int)floorf(v), 0, tex->h - 1);
      int sx = du < 0 ? -1 : 1, sy = dv < 0 ? -1 : 1;
      float step_x = fabsf(du) < 1e-7f ? INFINITY : fabsf(1 / du);
      float step_y = fabsf(dv) < 1e-7f ? INFINITY : fabsf(1 / dv);
      float next_x = isinf(step_x) ? INFINITY : dist + (ix + (sx > 0) - u) / du;
      float next_y = isinf(step_y) ? INFINITY : dist + (iy + (sy > 0) - v) / dv;
      uint32_t color = 0;
      bool inner = false, back = false;
      while (dist <= exit && ix >= 0 && ix < tex->w && iy >= 0 && iy < tex->h) {
        bool aperture = fireplace_opening((ix + .5f) / tex->w, (iy + .5f) / tex->h);
        uint32_t sample = stone->px[iy * tex->w + ix];
        if (!aperture && (sample >> 24) >= 128) { color = sample; break; }
        inner |= aperture;
        if (next_x < next_y) { dist = next_x; next_x += step_x; ix += sx; }
        else { dist = next_y; next_y += step_y; iy += sy; }
      }
      if (!color && back_dist >= enter && back_dist <= exit + .00001f) {
        float bu = .5f - (across + da * back_dist) / width;
        float bv = 1 - (eye + dz * back_dist) / height;
        if (fireplace_opening(bu, bv)) {
          ix = tex_column(tex, bu); iy = tex_column(tex, bv);
          color = tex->px[iy * tex->w + ix];
          dist = back_dist;
          back = true;
        }
      }
      if ((color >> 24) < 128) continue;
      bool front = !back && fabsf(normal + dn * dist - projection) < .0001f;
      if (inner && !back) {
        const Image *brick = r->assets->walls[WT_BRICK];
        float along = (normal + dn * dist + recess) / (projection + recess);
        color = brick->px[tex_column(brick, (eye + dz * dist) / height) * brick->w + tex_column(brick, along)];
      } else if (!front && !back) {
        float along = clampf((normal + dn * dist) / projection, 0, 1);
        float stone_x = ix < tex->w / 2 ? .12f + along * .10f : .88f - along * .10f;
        uint32_t surface = stone->px[iy * tex->w + tex_column(stone, stone_x)];
        if ((surface >> 24) >= 128) color = surface;
      }
      float k = 255.0f / (color >> 24);
      float red = ((color >> 16) & 255) * k, green = ((color >> 8) & 255) * k, blue = (color & 255) * k;
      float shade = back ? .85f : front ? 1 : inner ? .52f : .72f;
      float emission = back ? clampf((green - 80) / 95, 0, 1) * clampf((red - blue - 50) / 70, 0, 1) : 0;
      fb->px[index] = pack_argb(255,
          (red * tint.k * shade + tint.r) * (1 - emission) + red * emission,
          (green * tint.k * shade + tint.g) * (1 - emission) + green * emission,
          (blue * tint.k * shade + tint.b) * (1 - emission) + blue * emission);
      r->depth[index] = dist;
      r->floor_mask[index] = 0;
    }
  }
}

static void draw_hanging_quad(Renderer *r, Image *fb, const Game *g, float x0, float y0, float x1, float y1,
                              const Image *tex, const HangingSpec *spec) {
  QuadSpan q;
  if (!quad_span(&g->player, x0, y0, x1, y1, &q)) return;
  for (int x = q.x_start; x <= q.x_end; x++) {
    float dist, u;
    quad_at(&q, x, &dist, &u);
    if (!g->level->world.cells && dist >= r->zbuffer[x]) continue;
    float line_h = VIEW_H / dist;
    float base = level_floor(g->level, (x0 + x1) * .5f, (y0 + y1) * .5f);
    float top = VIEW_H / 2.0f + line_h * (r->eye_z - base - spec->z_top);
    draw_column(r, fb, tex, tex_column(tex, u), x, top, line_h * (spec->z_top - spec->z_bot), NULL, dist, false);
  }
}

// Flat decorations against the wall: one perspective-correct quad each,
// z-tested per column (so boxes and walls occlude them) but never written —
// nothing can stand between a hanging and its wall. Transparent textures
// rule out the walls' fog tint, so they shade like sprites instead:
// pre-darkened distance buckets, lifted near torches.
static void render_wall_hangings(Renderer *r, Image *fb, const Game *g) {
  const Player *p = &g->player;
  const Assets *as = r->assets;
  const Level *lv = g->level;
  for (int i = 0; i < lv->hanging_count; i++) {
    const Hanging *hg = &lv->hangings[i];
    if (hg->kind == HANG_FIREPLACE) {
      render_fireplace(r, fb, g, hg);
      continue;
    }
    const HangingSpec *spec = &HANGING_SPECS[hg->kind];
    float half_w = (spec->z_top - spec->z_bot) / 2; // square quad, square texture
    float fx = hg->x + 0.5f + hg->dx * (0.5f + spec->offset);
    float fy = hg->y + 0.5f + hg->dy * (0.5f + spec->offset);
    if ((fx - p->x) * hg->dx + (fy - p->y) * hg->dy >= 0) continue; // backface
    float tx = (float)-hg->dy, ty = (float)hg->dx;
    const Image *img = as->hangings[hg->kind];
    if (hg->kind == HANG_COBWEB) {
      // tucked into one upper corner of the face — the seed picks which —
      // with the matching left- or right-anchored weave, so the threads
      // run into the masonry the quad edge touches
      float side = (hg->seed & 1) ? 1.0f : -1.0f;
      fx += tx * side * (0.5f - half_w);
      fy += ty * side * (0.5f - half_w);
      img = as->cobwebs[((hg->seed >> 1) & 1) * 2 + (side > 0 ? 0 : 1)];
    } else if (hg->kind == HANG_CRACK) {
      img = as->cracks[hg->seed % 4];
    } else if (hg->kind == HANG_PORTRAIT) {
      // the painted eyes follow the player: the angle the player makes with
      // the painting's normal picks a gaze column (texture u runs against
      // +t, so a player on the +t side takes the leftmost gaze), and
      // standing right in front makes the sitter glance down
      float fwd = (p->x - fx) * hg->dx + (p->y - fy) * hg->dy;
      float lat = (p->x - fx) * tx + (p->y - fy) * ty;
      float a = atan2f(-lat, fwd) / ((float)M_PI / 2); // -1..1 across the wall
      int col = clampi((int)round_half_up(((a + 1) / 2) * (PORTRAIT_COLS - 1)), 0, PORTRAIT_COLS - 1);
      int row = fwd * fwd + lat * lat < 3.6f ? 1 : 0;
      img = as->portraits[hg->seed % PORTRAIT_SITTERS][row * PORTRAIT_COLS + col];
    }
    if (!img) continue;
    int bucket = shade_bucket(hypotf(fx - p->x, fy - p->y));
    float light = torch_light(g, fx, fy, g->time);
    if (light > 0.5f)
      bucket = maxi(0, bucket - 2);
    else if (light > 0.15f)
      bucket = maxi(0, bucket - 1);
    float ax = fx + tx * half_w, ay = fy + ty * half_w;
    float bx = fx - tx * half_w, by = fy - ty * half_w;
    draw_hanging_quad(r, fb, g, ax, ay, bx, by, shaded(img, bucket), spec);
  }
}

// ------------------------------------------------------------------ sprites

typedef struct {
  float x, y;
  const Image *img;
  float scale;
  bool has_z;
  float z_center; // height of the sprite's centre; otherwise it stands on the floor
  bool fullbright;
  float alpha;
  bool dropline; // a spider's silk thread runs from the sprite's top to the ceiling
  float dist;
  int order;
  const PendulumSpawn *pendulum;
} Sprite;

static Sprite *sprites;
static int sprite_count, sprite_cap;

static Sprite *add_sprite(float x, float y, const Image *img, float scale) {
  if (sprite_count == sprite_cap) {
    sprite_cap = sprite_cap ? sprite_cap * 2 : 256;
    sprites = realloc(sprites, sizeof(Sprite) * sprite_cap);
  }
  Sprite *s = &sprites[sprite_count];
  *s = (Sprite){.x = x, .y = y, .img = img, .scale = scale, .alpha = 1, .order = sprite_count};
  sprite_count++;
  return s;
}

static int cmp_sprite(const void *a, const void *b) {
  const Sprite *p = a, *q = b;
  if (p->dist != q->dist) return p->dist > q->dist ? -1 : 1; // far first
  return p->order - q->order;
}

static void collect_sprites(Renderer *r, const Game *g) {
  const Assets *as = r->assets;
  const Player *p = &g->player;
  const Level *lv = g->level;
  sprite_count = 0;
  const HorrorLayout *h = &lv->horror;
  for (int i = 0; i < h->note_count; i++) {
    if (world_monk_note(lv, i)) continue;
    Sprite *s = add_sprite(h->notes[i].x + .5f, h->notes[i].y + .5f, as->horror_note, .28f);
    s->has_z = true; s->z_center = .11f;
  }
  for (int i = 0; i < h->haven_count; i++)
    add_sprite(h->havens[i].x + .5f, h->havens[i].y + .5f, as->white_candles, .43f)->fullbright = true;
  for (int i = 0; i < h->scene_count; i++) {
    float x, y, alpha;
    if (!horror_scene_visible(g, i, &x, &y, &alpha)) continue;
    HauntKind kind = h->scenes[i].kind;
    const Image *im = kind == HAUNT_CELL ? as->prisoner : kind == HAUNT_ARMOR ? as->decor[DECO_ARMOR] : as->haunt_shadow;
    Sprite *s = add_sprite(x, y, im, kind == HAUNT_ARMOR ? .8f : .94f);
    s->alpha = alpha;
  }
  const Warden *warden = &g->horror.warden;
  if (h->chapter && warden->state != WARDEN_DORMANT) {
    int frame = warden->state == WARDEN_HUNT ? 1 + ((int)(g->time * 2.4f) & 1) : 0;
    if (warden->attack_timer > .55f) frame = 3;
    Sprite *s = add_sprite(warden->x, warden->y, as->warden[frame], .97f);
    if (warden->state == WARDEN_WARNING) s->alpha = clampf(1 - warden->state_timer / WARDEN_WARNING_TIME, .15f, 1);
    if (warden->state == WARDEN_FADING) s->alpha = warden->state_timer / 2;
    if (warden->state == WARDEN_STAGGER) s->alpha = .45f;
  }
  for (int i = 0; i < lv->w * lv->h; i++) {
    if (lv->grid[i] != '8' || g->wall_health[i]) continue;
    float x = i % lv->w + 0.5f, y = i / lv->w + 0.5f;
    add_sprite(x, y, as->wall_rubble, 1.05f);
    if (g->wall_dust[i] > 0) {
      float age = 1 - g->wall_dust[i] / WALL_DUST_TIME;
      int frame = clampi((int)(age * WALL_DUST_FRAMES), 0, WALL_DUST_FRAMES - 1);
      Sprite *s = add_sprite(x, y, as->wall_dust[frame], 1.3f + age * 0.5f);
      s->has_z = true;
      s->z_center = 0.45f + age * 0.15f;
    }
  }
  for (int i = 0; i < g->barrel_count; i++) {
    const Barrel *b = &g->barrels[i];
    add_sprite(b->x, b->y, as->barrels[b->destroyed ? 2 : b->powder ? 1 : 0], 0.55f);
    if (b->blast_timer > 0) {
      float age = 1 - b->blast_timer / BARREL_EXPLOSION_TIME;
      int frame = clampi((int)(age * EXPLOSION_FRAMES), 0, EXPLOSION_FRAMES - 1);
      Sprite *s = add_sprite(b->x, b->y, as->explosion[frame], 1.8f + age * 0.8f);
      s->has_z = true;
      s->z_center = 0.5f + age * 0.2f;
      s->fullbright = frame < 5;
    }
  }
  for (int i = 0; i < g->enemy_count; i++) {
    const Enemy *e = &g->enemies[i];
    Sprite *s = add_sprite(e->x, e->y, enemy_sprite(e, as, p), e->stats->scale);
    // flying enemies carry a height; footmen stand on the floor
    s->has_z = e->flying;
    s->z_center = e->fly_height;
    s->dropline = e->stats->climber && e->flying && !enemy_dead(e);
  }
  for (int i = 0; i < g->item_count; i++) {
    const Item *it = &g->items[i];
    add_sprite(it->x, it->y, as->items[it->kind], it->kind == ITEM_KEY ? 0.55f : 0.45f);
    if (it->kind == ITEM_KEY) {
      // every few seconds the key throws a brief glint — fullbright, so it
      // still reads at the far end of a foggy corridor; each key twinkles
      // on its own schedule
      float cycle = fmodf(fmodf(g->time * 0.38f + key_phase(it) * 0.11f, 1) + 1, 1);
      if (cycle < 0.16f) {
        float a = sinf((cycle / 0.16f) * (float)M_PI);
        Sprite *s = add_sprite(it->x, it->y, as->sparkle, 0.34f * (0.55f + 0.45f * a));
        s->has_z = true;
        s->z_center = 0.34f; // over the key's bow
        s->fullbright = true;
        s->alpha = a;
      }
    }
  }
  for (int i = 0; i < lv->pendulum_count; i++) {
    const PendulumSpawn *trap = &lv->pendulums[i];
    float offset = PENDULUM_LENGTH * sinf(game_pendulum_angle(trap, g->time));
    Sprite *s = add_sprite(trap->x + 0.5f + trap->dx * offset,
                           trap->y + 0.5f + trap->dy * offset, NULL, 1);
    s->pendulum = trap;
  }
  for (int i = 0; i < g->projectile_count; i++) {
    // pick the arrow view from its flight direction in camera space:
    // depth along the view axis vs. lateral drift across the screen
    const Projectile *a = &g->projectiles[i];
    float depth = a->dir_x * p->dir_x + a->dir_y * p->dir_y;
    float lat = (a->dir_x * p->plane_x + a->dir_y * p->plane_y) / 0.66f;
    int view;
    if (depth < -fabsf(lat))
      view = TRAP_ARROW_TOWARD;
    else if (depth > fabsf(lat))
      view = TRAP_ARROW_AWAY;
    else
      view = lat > 0 ? TRAP_ARROW : TRAP_ARROW_LEFT;
    // right at the eye an arrow is only a smear; one flying away that close
    // was just loosed by the player, and the POV streak still shows it
    float min_dist = view == TRAP_ARROW_AWAY ? 1.8f : 0.7f;
    if (sq(a->x - p->x) + sq(a->y - p->y) < sq(min_dist)) continue;
    Sprite *s = add_sprite(a->x, a->y, as->trap[view], 0.82f);
    s->has_z = true;
    s->z_center = a->z - level_floor(lv, a->x, a->y) - 0.03f; // the shaft is painted just above the texture's middle
  }
  for (int i = 0; i < lv->decoration_count; i++) {
    const Decoration *d = &lv->decorations[i];
    if (d->kind == DECO_FIREPLACE) continue; // collision/light marker; the wall attachment owns its geometry
    float dx = (float)d->x, dy = (float)d->y;
    if (d->kind == DECO_FOUNTAIN) {
      // the water animates like the torch flames, just at a calmer pace
      add_sprite(dx, dy, as->fountains[(int)floorf(g->time * 6) & 3], 0.85f);
      continue;
    }
    if (d->kind == DECO_CHAINS) {
      // hung from above: anchored so the mounting plate touches the ceiling
      Sprite *s = add_sprite(dx, dy, as->decor[DECO_CHAINS], 0.55f);
      s->has_z = true;
      s->z_center = level_ceiling(lv, dx, dy) - level_floor(lv, dx, dy) - 0.55f / 2;
      continue;
    }
    if (d->kind == DECO_CANDELABRA || d->kind == DECO_CANDLES) {
      // shaded stand first, then its flames as a fullbright overlay drawn
      // in the same sprite grid and scale, so they register exactly and
      // keep glowing through the distance fog; each stand flickers on its
      // own schedule
      float scale = d->kind == DECO_CANDELABRA ? 0.66f : 0.5f;
      add_sprite(dx, dy, as->decor[d->kind], scale);
      int frame = ((int)floorf(g->time * 8) + ((int)floor(d->x * 31 + d->y * 17) & 3)) & 3;
      Sprite *s = add_sprite(dx, dy, as->candle_flames[d->kind == DECO_CANDELABRA ? 0 : 1][frame], scale);
      s->fullbright = true;
      continue;
    }
    if (!as->decor[d->kind]) continue; // wall boxes are drawn as quads, not billboards
    float scale = d->kind >= DECO_OAK ? world_prop_scale(d->kind)
                  : d->kind == DECO_TABLE    ? 0.7f
                  : d->kind == DECO_ARMOR  ? 0.78f
                  : d->kind == DECO_STATUE ? 0.72f
                  : d->kind == DECO_URN    ? 0.5f
                                           : 0.64f;
    add_sprite(dx, dy, as->decor[d->kind], scale);
  }
  for (int i = 0; i < lv->torch_count; i++) {
    const Torch *t = &lv->torches[i];
    int frame = (int)floor(g->time * 12 + t->phase * 1.7) & 3;
    Sprite *s = add_sprite((float)t->x, (float)t->y, as->torches[frame], 0.52f);
    s->has_z = true;
    s->z_center = 0.58f;
    s->fullbright = true;
    s->alpha = horror_torch_strength(g, t->x, t->y);
  }
  for (int i = 0; i < sprite_count; i++) sprites[i].dist = sq(p->x - sprites[i].x) + sq(p->y - sprites[i].y);
  qsort(sprites, (size_t)sprite_count, sizeof(Sprite), cmp_sprite);
}

static void render_pendulum(Renderer *r, Image *fb, const Game *g, const PendulumSpawn *s) {
  float angle = game_pendulum_angle(s, g->time);
  int frame = clampi((int)round_half_up((angle / PENDULUM_MAX_ANGLE + 1) * 0.5f * (PENDULUM_FRAMES - 1)),
                     0, PENDULUM_FRAMES - 1);
  float cx = s->x + 0.5f, cy = s->y + 0.5f;
  const Player *p = &g->player;
  const Image *tex = r->assets->pendulum[frame];
  const float half = PENDULUM_VIEW_WIDTH / 2, thickness = .018f;
  float across = (p->x - cx) * s->dx + (p->y - cy) * s->dy;
  float normal = -(p->x - cx) * s->dy + (p->y - cy) * s->dx;
  float eye = r->eye_z - level_floor(g->level, cx, cy);

  // Extrude the painted alpha silhouette, including the holes in the blade.
  // Walking texture cells along each ray gives the same solid shape from
  // every angle, with continuous sides instead of an edge-on substitute.
  for (int x = fb->clip_x0; x < fb->clip_x1; x++) {
    float camera_x = 2.0f * x / W - 1;
    float rx = p->dir_x + p->plane_x * camera_x, ry = p->dir_y + p->plane_y * camera_x;
    float da = rx * s->dx + ry * s->dy, dn = -rx * s->dy + ry * s->dx;
    float near = NEAR_PLANE, far = g->level->world.cells ? INFINITY : r->zbuffer[x];
    if (!ray_interval(across, da, -half, half, &near, &far) ||
        !ray_interval(normal, dn, -thickness, thickness, &near, &far)) continue;
    float top = VIEW_H * .5f + minf((eye - 1) * VIEW_H / near, (eye - 1) * VIEW_H / far);
    float bottom = VIEW_H * .5f + maxf(eye * VIEW_H / near, eye * VIEW_H / far);
    int y0 = maxi(fb->clip_y0, (int)ceilf(top - .5f));
    int y1 = mini(fb->clip_y1, (int)ceilf(bottom - .5f));
    float painted_to = 0;
    for (int y = y0; y < y1; y++) {
      int index = y * W + x;
      float dz = (VIEW_H * .5f - y - .5f) / VIEW_H;
      float enter = near, exit = minf(far, r->depth[index]);
      if (!ray_interval(eye, dz, 0, 1, &enter, &exit) || enter > exit) continue;
      float dist = minf(enter + .00001f, exit);
      float u = ((across + da * dist) / PENDULUM_VIEW_WIDTH + .5f) * tex->w;
      float v = (1 - eye - dz * dist) * tex->h;
      float du = da / PENDULUM_VIEW_WIDTH * tex->w, dv = -dz * tex->h;
      int tx = clampi((int)floorf(u), 0, tex->w - 1), ty = clampi((int)floorf(v), 0, tex->h - 1);
      int sx = du < 0 ? -1 : 1, sy = dv < 0 ? -1 : 1;
      float step_x = fabsf(du) < 1e-7f ? INFINITY : fabsf(1 / du);
      float step_y = fabsf(dv) < 1e-7f ? INFINITY : fabsf(1 / dv);
      float next_x = isinf(step_x) ? INFINITY : dist + (tx + (sx > 0) - u) / du;
      float next_y = isinf(step_y) ? INFINITY : dist + (ty + (sy > 0) - v) / dv;
      while (dist <= exit && tx >= 0 && tx < tex->w && ty >= 0 && ty < tex->h) {
        uint32_t color = tex->px[ty * tex->w + tx];
        int alpha = color >> 24;
        if (alpha >= 128) {
          bool side = dist > enter + .00002f || fabsf(normal + dn * dist) < thickness - .0001f;
          float ridge = 1 - clampf(fabsf(normal + dn * dist) / thickness, 0, 1);
          float light = SPRITE_SHADES[shade_bucket(dist)] * (side ? .68f + .22f * ridge : 1);
          float k = light * 255 / alpha;
          fb->px[index] = pack_argb(255, ((color >> 16) & 255) * k, ((color >> 8) & 255) * k, (color & 255) * k);
          r->depth[index] = dist;
          r->floor_mask[index] = 0;
          painted_to = maxf(painted_to, dist);
          break;
        }
        if (next_x < next_y) { dist = next_x; next_x += step_x; tx += sx; }
        else { dist = next_y; next_y += step_y; ty += sy; }
      }
    }
    if (painted_to > 0) render_transparent_walls(r, fb, x, x, painted_to);
  }
}

static void render_sprites(Renderer *r, Image *fb, const Game *g) {
  const Player *p = &g->player;
  collect_sprites(r, g);
  float k = inv_det(p);
  for (int i = 0; i < sprite_count; i++) {
    const Sprite *s = &sprites[i];
    if (s->pendulum) {
      render_pendulum(r, fb, g, s->pendulum);
      continue;
    }
    if (!s->img) continue;
    float sx = s->x - p->x, sy = s->y - p->y;
    float transform_x = k * (p->dir_y * sx - p->dir_x * sy);
    float transform_y = k * (-p->plane_y * sx + p->plane_x * sy);
    if (transform_y <= 0.05f) continue;

    float screen_x = (W / 2.0f) * (1 + transform_x / transform_y);
    float full_h = fabsf(VIEW_H / transform_y);
    float spr_h = full_h * s->scale;
    float spr_w = spr_h;
    float base = level_floor(g->level, s->x, s->y);
    float top = !s->has_z ? VIEW_H / 2.0f + full_h * (r->eye_z - base) - spr_h
                          : VIEW_H / 2.0f + full_h * (r->eye_z - base - s->z_center) - spr_h / 2;
    int start_x = (int)floorf(screen_x - spr_w / 2);
    int end_x = (int)ceilf(screen_x + spr_w / 2);
    if (end_x < 0 || start_x >= W || spr_w < 1) continue;

    // draw in vertical stripes, clipped by the wall z-buffer, darkened with
    // distance to match the wall fog
    const Image *img = s->img;
    int bucket = 0;
    if (!s->fullbright) {
      bucket = shade_bucket(transform_y);
      float light = torch_light(g, s->x, s->y, g->time);
      if (light > 0.5f)
        bucket = maxi(0, bucket - 2);
      else if (light > 0.15f)
        bucket = maxi(0, bucket - 1);
      img = shaded(s->img, bucket);
    }
    // the dropline goes in first, so the spider hangs in front of its own
    // thread; a wall in front of the middle column hides all of it
    int mid = (int)floorf(screen_x);
    float ceil_y = VIEW_H / 2.0f + full_h * (r->eye_z - level_ceiling(g->level, s->x, s->y));
    if (s->dropline && mid >= 0 && mid < W &&
        (g->level->world.cells || transform_y < r->zbuffer[mid]) && top > ceil_y) {
      float lw = maxf(1, full_h / 240);
      float f = SPRITE_SHADES[bucket];
      if (!(g->level->has_heights || g->level->has_ceilings)) {
        gfx_fill_rect(fb, screen_x - lw / 2, ceil_y, lw, top + 1 - ceil_y, rgba(216 * f, 211 * f, 198 * f, 0.5f));
      } else {
        for (int y = maxi(0, (int)ceilf(ceil_y)); y < mini(VIEW_H, (int)ceilf(top)); y++)
          if (transform_y < r->depth[y * W + mid])
            gfx_fill_rect(fb, screen_x - lw / 2, y, lw, 1, rgba(216 * f, 211 * f, 198 * f, 0.5f));
      }
    }
    int x0 = maxi(0, start_x), x1 = mini(W - 1, end_x);
    if (g->level->has_heights || g->level->has_ceilings) {
      int y0, y1;
      if (!column_rows(fb, top, spr_h, &y0, &y1)) continue;
      for (int x = x0; x <= x1; x++) {
        int tx = clampi((int)((x + .5f - (screen_x - spr_w / 2)) / spr_w * img->w), 0, img->w - 1);
        for (int y = y0; y < y1; y++) {
          if (transform_y > r->depth[y * W + x] + .0001f) continue;
          int ty = clampi((int)((y + .5f - top) / spr_h * img->h), 0, img->h - 1);
          px_over(&fb->px[(size_t)y * fb->w + x], img->px[ty * img->w + tx], s->alpha);
        }
        render_transparent_walls(r, fb, x, x, transform_y);
      }
      continue;
    }
    int run_start = -1;
    for (int x = x0; x <= x1 + 1; x++) {
      bool visible = x <= x1 && transform_y < r->zbuffer[x];
      if (visible && run_start < 0) run_start = x;
      if (!visible && run_start >= 0) {
        float tex_x0 = ((float)(run_start - start_x) / spr_w) * img->w;
        float tex_x1 = ((float)(x - start_x) / spr_w) * img->w;
        gfx_blit(fb, img, tex_x0, 0, maxf(0.01f, tex_x1 - tex_x0), img->h, (float)run_start, top,
                 (float)(x - run_start), spr_h, s->alpha);
        render_transparent_walls(r, fb, run_start, x - 1, transform_y);
        run_start = -1;
      }
    }
  }
}

// ------------------------------------------------------------------ glows

typedef struct {
  float x, y; // world position
  float height; // glow centre as a fraction of the wall height
  float margin; // how far off-screen the centre may be and still show
  float min_r, max_r, r_scale;
  GradientStop stops[3];
} GlowSpec;

static void draw_glow(Renderer *r, Image *fb, const Player *p, const GlowSpec *gs) {
  float k = inv_det(p);
  float sx = gs->x - p->x, sy = gs->y - p->y;
  float transform_x = k * (p->dir_y * sx - p->dir_x * sy);
  float transform_y = k * (-p->plane_y * sx + p->plane_x * sy);
  if (transform_y <= 0.08f) return;
  float screen_x = (W / 2.0f) * (1 + transform_x / transform_y);
  if (screen_x < -gs->margin || screen_x > W + gs->margin) return;
  int zi = clampi((int)round_half_up(screen_x), 0, W - 1);
  int yi = clampi((int)(VIEW_H / 2.0f + VIEW_H / transform_y * (r->eye_z - gs->height)), 0, VIEW_H - 1);
  if (transform_y > r->depth[yi * W + zi] + 0.15f) return;
  float full_h = VIEW_H / transform_y;
  float screen_y = VIEW_H / 2.0f + full_h * (r->eye_z - gs->height);
  float radius = maxf(gs->min_r, minf(gs->max_r, full_h * gs->r_scale));
  int x0 = maxi(0, (int)floorf(screen_x - radius)), x1 = mini(W, (int)ceilf(screen_x + radius));
  int y0 = maxi(0, (int)floorf(screen_y - radius)), y1 = mini(VIEW_H, (int)ceilf(screen_y + radius));
  if (x0 >= x1 || y0 >= y1) return;
  for (int y = y0; y < y1; y++)
    memcpy(r->shake_buf + y * W + x0, fb->px + (size_t)y * fb->w + x0, (size_t)maxi(0, x1 - x0) * sizeof(uint32_t));
  gfx_radial(fb, screen_x - radius, screen_y - radius, radius * 2, radius * 2, screen_x, screen_y, 0, radius, gs->stops,
             3, BLEND_SCREEN);
  for (int y = y0; y < y1; y++)
    for (int x = x0; x < x1; x++)
      if (transform_y > r->depth[y * W + x] + .15f) fb->px[(size_t)y * fb->w + x] = r->shake_buf[y * W + x];
}

static GradientStop stop(float pos, float r, float g, float b, float a) {
  GradientStop s = {pos, {r, g, b, a}};
  return s;
}

static void render_glows(Renderer *r, Image *fb, const Game *g) {
  const Player *p = &g->player;
  const Level *lv = g->level;
  for (int i = 0; i < lv->horror.haven_count; i++) {
    const Haven *v = &lv->horror.havens[i];
    GlowSpec gs = {v->x + .5f, v->y + .5f, .26f, 170, 10, 120, .65f,
                   {stop(0, 255, 244, 199, .23f), stop(.35f, 255, 212, 148, .09f), stop(1, 255, 190, 100, 0)}};
    gs.height += level_floor(lv, gs.x, gs.y);
    draw_glow(r, fb, p, &gs);
  }
  for (int i = 0; i < g->barrel_count; i++) {
    const Barrel *b = &g->barrels[i];
    float heat = maxf(0, b->blast_timer / BARREL_EXPLOSION_TIME * 2 - 1);
    if (heat <= 0) continue;
    GlowSpec gs = {b->x, b->y, 0.5f, 220, 12, 180, 1.2f,
                   {stop(0, 255, 190, 80, 0.5f * heat), stop(0.3f, 255, 100, 20, 0.2f * heat),
                    stop(1, 255, 70, 0, 0)}};
    gs.height += level_floor(lv, gs.x, gs.y);
    draw_glow(r, fb, p, &gs);
  }
  for (int i = 0; i < lv->torch_count; i++) {
    const Torch *t = &lv->torches[i];
    float flicker = torch_flicker(g->time, (float)t->phase) * horror_torch_strength(g, t->x, t->y);
    GlowSpec gs = {(float)t->x, (float)t->y, 0.72f, 180, 16, 150, 0.62f,
                   {stop(0, 255, 174, 55, 0.2f * flicker), stop(0.28f, 255, 112, 22, 0.1f * flicker),
                    stop(1, 255, 80, 0, 0)}};
    gs.height += level_floor(lv, gs.x, gs.y);
    draw_glow(r, fb, p, &gs);
  }
  // candle flames breathe a smaller, calmer halo than the torches — a glow
  // to notice across a dark hall, not a light to read by
  for (int i = 0; i < lv->decoration_count; i++) {
    const Decoration *d = &lv->decorations[i];
    if (d->kind != DECO_CANDELABRA && d->kind != DECO_CANDLES) continue;
    bool stand = d->kind == DECO_CANDELABRA;
    float flicker = 0.8f + 0.2f * sinf(g->time * 5.3f + (float)(d->x * 31 + d->y * 17));
    // centred on the flames: shoulder height for the candelabrum, down by
    // the slab for the niche cluster
    GlowSpec gs = {(float)d->x, (float)d->y, stand ? 0.52f : 0.18f, 120, 10, 90, stand ? 0.38f : 0.28f,
                   {stop(0, 255, 196, 96, 0.13f * flicker), stop(0.35f, 255, 140, 40, 0.06f * flicker),
                    stop(1, 255, 90, 10, 0)}};
    gs.height += level_floor(lv, gs.x, gs.y);
    draw_glow(r, fb, p, &gs);
  }
  // the hearths throw the broadest bloom of all — low, wide and ruddy,
  // pooling on the floor in front of the fire
  for (int i = 0; i < lv->fire_light_count; i++) {
    const Torch *f = &lv->fire_lights[i];
    float flicker = torch_flicker(g->time, (float)f->phase);
    GlowSpec gs = {(float)f->x, (float)f->y, 0.19f, 220, 16, 170, 0.7f,
                   {stop(0, 255, 158, 44, 0.22f * flicker), stop(0.3f, 255, 96, 16, 0.11f * flicker),
                    stop(1, 255, 70, 0, 0)}};
    gs.height += level_floor(lv, gs.x, gs.y);
    draw_glow(r, fb, p, &gs);
  }
  // keys breathe a soft golden halo in the same pass, hugging the floor
  // where the key lies
  for (int i = 0; i < g->item_count; i++) {
    const Item *it = &g->items[i];
    if (it->kind != ITEM_KEY) continue;
    float pulse = key_pulse(g->time, key_phase(it));
    GlowSpec gs = {it->x, it->y, 0.3f, 120, 10, 95, 0.42f,
                   {stop(0, 255, 224, 120, 0.13f * pulse), stop(0.35f, 255, 190, 70, 0.06f * pulse),
                    stop(1, 255, 170, 40, 0)}};
    gs.height += level_floor(lv, gs.x, gs.y);
    draw_glow(r, fb, p, &gs);
  }
}

// Embers occupy world space and are tested against geometry, so sparks
// disappear behind a portcullis or a ledge with their parent flame.
static void render_embers(Renderer *r, Image *fb, const Game *g) {
  const Player *p = &g->player;
  for (int i = 0; i < g->level->torch_count; i++) {
    const Torch *t = &g->level->torches[i];
    float strength = horror_torch_strength(g, t->x, t->y);
    if (strength < .1f) continue;
    for (int j = 0; j < 3; j++) {
      float age = fmodf(g->time * .32f + (float)t->phase + j * .33f, 1);
      float wx = t->x + sinf(age * 8 + i + j) * .035f;
      float wy = t->y + cosf(age * 7 + i) * .035f;
      float sx = wx - p->x, sy = wy - p->y, k = inv_det(p);
      float tx = k * (p->dir_y * sx - p->dir_x * sy);
      float depth = k * (-p->plane_y * sx + p->plane_x * sy);
      if (depth <= .15f || depth > 7) continue;
      float x = W / 2.0f * (1 + tx / depth);
      float z = level_floor(g->level, wx, wy) + .76f + age * .18f;
      float y = VIEW_H / 2.0f + VIEW_H / depth * (r->eye_z - z);
      int ix = (int)x, iy = (int)y;
      if (ix < 0 || ix >= W || iy < 0 || iy >= VIEW_H || depth > r->depth[iy * W + ix] + .02f) continue;
      float size = clampf(.9f / depth, .5f, 1.5f);
      gfx_fill_rect(fb, x, y, size, size * 1.4f, rgba(245, 183, 96, (1 - age) * .65f * strength));
    }
  }
}

// ------------------------------------------------------------------ weapon

static void render_weapon(Renderer *r, Image *fb, const Game *g) {
  const Player *p = &g->player;
  const Image *img = r->assets->weapons[p->weapon][clampi(p->weapon_frame, 0, 2)];
  const float size = 258;
  float bob_x = sinf(p->bob_phase) * 10 * p->bob_amount;
  float bob_y = fabsf(cosf(p->bob_phase)) * 8 * p->bob_amount + 4;
  gfx_blit(fb, img, 0, 0, (float)img->w, (float)img->h, W / 2.0f - size / 2 + bob_x, VIEW_H - size + 28 + bob_y, size,
           size, 1);
  // crosshair
  gfx_fill_rect(fb, W / 2.0f - 1, VIEW_H / 2.0f - 1, 2, 2, rgba(234, 223, 195, .8f));
  gfx_fill_rect(fb, W / 2.0f - 6, VIEW_H / 2.0f, 2, 1, rgba(0, 0, 0, .55f));
  gfx_fill_rect(fb, W / 2.0f + 4, VIEW_H / 2.0f, 2, 1, rgba(0, 0, 0, .55f));
}

static void render_flashes(Image *fb, const Game *g) {
  if (g->portal_flash > 0) gfx_fill_rect(fb, 0, 0, W, VIEW_H, rgba(150, 90, 230, minf(0.55f, g->portal_flash)));
  // venom in the blood: a sickly green haze that throbs with every pang
  float sick = (g->player.venom > 0 ? 0.08f : 0) + g->venom_flash * 0.5f;
  if (sick > 0) gfx_fill_rect(fb, 0, 0, W, VIEW_H, rgba(80, 160, 40, minf(0.35f, sick)));
  if (g->damage_flash > 0) gfx_fill_rect(fb, 0, 0, W, VIEW_H, rgba(190, 0, 0, minf(0.55f, g->damage_flash)));
  if (g->pickup_flash > 0) gfx_fill_rect(fb, 0, 0, W, VIEW_H, rgba(230, 200, 60, minf(0.3f, g->pickup_flash)));
  if (g->message[0] && g->message_timer > 0) {
    float a = minf(1, g->message_timer);
    float tw = font_measure(g->message, 15, FONT_BOLD);
    gfx_fill_rect(fb, W / 2.0f - tw / 2 - 12, 30, tw + 24, 26, rgba(12, 8, 4, 0.6f * a));
    gfx_fill_rect(fb, W / 2.0f - tw / 2 - 12, 30, tw + 24, 1, rgba(201, 162, 74, 0.7f * a));
    gfx_fill_rect(fb, W / 2.0f - tw / 2 - 12, 55, tw + 24, 1, rgba(201, 162, 74, 0.7f * a));
    font_draw(fb, g->message, W / 2.0f, 48, 15, FONT_BOLD, ALIGN_CENTER, rgba(235, 215, 140, a));
  }
}

static void render_horror_text(Renderer *r, Image *fb, const Game *g) {
  int index = g->horror.reading;
  if (index < 0) {
    if (horror_near_note(g) >= 0 && !g->player_dead) {
      gfx_fill_rect(fb, 142, VIEW_H - 31, 356, 22, rgba(7, 10, 13, .85f));
      const Monk *monk = world_monk_note(g->level, horror_near_note(g));
      font_draw(fb, monk ? "USE - SPEAK WITH THE MONK" : "USE - READ THE PRISONER'S NOTE", W / 2, VIEW_H - 16, 11, FONT_BOLD, ALIGN_CENTER, rgb(226, 210, 171));
    }
    return;
  }
  gfx_fill_rect(fb, 0, 0, W, VIEW_H, rgba(2, 5, 8, .78f));
  gfx_fill_rect(fb, 61, 36, 520, 250, rgba(0, 0, 0, .6f));
  gfx_blit(fb, r->assets->note_page, 0, 0, TEX, TEX, 58, 32, 520, 250, 1);
  gfx_fill_rect(fb, 80, 79, 472, 1, rgb(135, 109, 76));
  const Monk *monk = world_monk_note(g->level, index);
  font_draw(fb, monk ? monk->name : "WORDS LEFT BEHIND", W / 2, 64, 18, FONT_BOLD, ALIGN_CENTER, rgb(92, 44, 34));
  // Word-wrap the short handwritten fragments; | starts a new paragraph.
  const char *text = g->level->horror.notes[index].text;
  char line[224] = {0};
  size_t len = 0;
  float y = 105;
  for (const char *p = text;;) {
    while (*p == ' ') p++;
    const char *end = p;
    while (*end && *end != ' ' && *end != '|') end++;
    size_t word = (size_t)(end - p);
    char candidate[224];
    snprintf(candidate, sizeof candidate, "%s%s%.*s", line, len ? " " : "", (int)word, p);
    if (len && font_measure(candidate, 14, FONT_REGULAR) > 466) {
      font_draw(fb, line, 85, y, 14, FONT_REGULAR, ALIGN_LEFT, rgb(49, 40, 33));
      y += 20;
      len = 0; line[0] = 0;
    }
    if (word) {
      if (len) line[len++] = ' ';
      memcpy(line + len, p, word); len += word; line[len] = 0;
    }
    if (!*end || *end == '|') {
      font_draw(fb, line, 85, y, 14, FONT_REGULAR, ALIGN_LEFT, rgb(49, 40, 33));
      y += 26; len = 0; line[0] = 0;
      if (!*end) break;
    }
    p = end + 1;
  }
  font_draw(fb, monk ? "USE - END CONVERSATION" : "USE - FOLD THE NOTE", W / 2, 263, 10, FONT_BOLD, ALIGN_CENTER, rgb(112, 72, 50));
}

// ------------------------------------------------------------------ 3D view

void render_view(Renderer *r, Image *fb, const Game *g) {
  const Player *p = &g->player;
  const Assets *as = r->assets;
  image_set_clip(fb, 0, 0, W, VIEW_H);
  r->eye_z = level_floor(g->level, p->x, p->y) + .5f + p->step_offset;
  for (int i = 0; i < W * VIEW_H; i++) r->depth[i] = INFINITY;
  memset(r->floor_mask, 0, (size_t)W * VIEW_H);

  for (int y = 0; y < VIEW_H; y++) {
    uint32_t *row = fb->px + (size_t)y * fb->w;
    uint32_t c = pack_argb(255, FOG.r, FOG.g, FOG.b);
    for (int x = 0; x < W; x++) row[x] = c;
  }

  if (g->level->world.cells) {
    const Image *sky = as->sky;
    for (int x = 0; x < W; x++) {
      float camera = 2.0f * x / W - 1;
      float angle = atan2f(p->dir_y + p->plane_y * camera, p->dir_x + p->plane_x * camera);
      float u = angle / (2 * (float)M_PI) + 1;
      int tx = (int)(u * sky->w) % sky->w;
      for (int y = 0; y < VIEW_H / 2; y++) {
        float elevation = atan2f(VIEW_H / 2.0f - y - .5f, VIEW_H);
        int ty = clampi((int)((1 - elevation / .48f) * (sky->h - 1)), 0, sky->h - 1);
        fb->px[(size_t)y * fb->w + x] = sky->px[ty * sky->w + tx];
      }
    }
  }

  for (int x = 0; x < W; x++) {
    float camera_x = 2.0f * x / W - 1;
    float ray_x = p->dir_x + p->plane_x * camera_x;
    float ray_y = p->dir_y + p->plane_y * camera_x;
    Hit hit = cast_ray(r, fb, g, x, p->x, p->y, ray_x, ray_y);
    r->zbuffer[x] = hit.dist;
    if (hit.dist <= 0.0001f || hit.dist == 1e9f || g->level->world.cells) continue;
    const Image *tex = (hit.side == 1 ? as->walls_dark : as->walls)[hit.tex];
    Tint tint = surface_tint(g, p->x + ray_x * hit.dist, p->y + ray_y * hit.dist, hit.dist);
    float near = maxf(0, hit.dist - .001f);
    float base = level_floor(g->level, p->x + ray_x * near, p->y + ray_y * near);
    float ceiling = level_ceiling(g->level, p->x + ray_x * near, p->y + ray_y * near);
    const Image *upper = (hit.side == 1 ? as->walls_dark : as->walls)[WT_STONE];
    draw_wall_column(r, fb, tex, upper, tex_column(tex, hit.wall_x), x, base, ceiling, &tint, hit.dist, true);
  }

  if (g->level->has_heights || g->level->has_ceilings) render_sectors(r, fb, g);
  else render_planes(r, fb, g);
  render_floor_features(r, fb, g);

  // Wall boxes (wardrobes, chests) are opaque geometry: drawn right after
  // the walls, z-tested and written per column, so bars and sprites clip
  // against them too.
  render_wall_boxes(r, fb, g);
  render_wall_hangings(r, fb, g);
  render_transparent_walls(r, fb, 0, W - 1, INFINITY);
  render_glows(r, fb, g);
  render_sprites(r, fb, g);
  render_embers(r, fb, g);
  gfx_blit(fb, r->vignette, 0, 0, W, VIEW_H, 0, 0, W, VIEW_H, 1);
  if (g->horror.tension > .01f) {
    GradientStop stops[] = {stop(0, 0, 0, 0, 0), stop(.65f, 0, 0, 0, 0), stop(1, 2, 4, 7, g->horror.tension * .25f)};
    gfx_radial(fb, 0, 0, W, VIEW_H, W / 2, VIEW_H / 2, 0, W / 2, stops, 3, BLEND_OVER);
  }
  if (g->pit_fall <= 0) render_weapon(r, fb, g);

  // camera shake: jitter the whole view, slightly overscanned so the
  // displaced frame never exposes the edges
  if (g->shake > 0) {
    float amp = g->shake * g->shake * 7;
    float dx = (float)(frand() * 2 - 1) * amp;
    float dy = (float)(frand() * 2 - 1) * amp;
    float zoom = 1 + (amp * 2) / VIEW_H;
    for (int y = 0; y < VIEW_H; y++) memcpy(r->shake_buf + y * W, fb->px + (size_t)y * fb->w, sizeof(uint32_t) * W);
    static int src_x[W];
    for (int x = 0; x < W; x++)
      src_x[x] = clampi((int)floorf(((float)x + 0.5f - W / 2.0f - dx) / zoom + W / 2.0f), 0, W - 1);
    for (int y = 0; y < VIEW_H; y++) {
      int sy = clampi((int)floorf(((float)y + 0.5f - VIEW_H / 2.0f - dy) / zoom + VIEW_H / 2.0f), 0, VIEW_H - 1);
      uint32_t *row = fb->px + (size_t)y * fb->w;
      const uint32_t *src = r->shake_buf + sy * W;
      for (int x = 0; x < W; x++) row[x] = src[src_x[x]];
    }
  }
  if (g->pit_fall > 0) {
    float fall = minf(1, g->pit_fall / PIT_FALL_TIME);
    int drop = (int)(fall * fall * VIEW_H * 0.65f);
    // As the camera sinks, the world rises and the shaft swallows the view.
    for (int y = 0; y < VIEW_H - drop; y++)
      memmove(fb->px + (size_t)y * fb->w, fb->px + (size_t)(y + drop) * fb->w, sizeof(uint32_t) * W);
    gfx_fill_rect(fb, 0, VIEW_H - drop, W, drop, rgb(3, 2, 1));
    gfx_fill_rect(fb, 0, 0, W, VIEW_H, rgba(0, 0, 0, fall * fall));
  }
  render_flashes(fb, g);
  const Landmark *landmark = world_landmark(g->level, p->x, p->y);
  if (landmark && g->horror.reading < 0) {
    float width = font_measure(landmark->name, 9, FONT_BOLD);
    gfx_fill_rect(fb, 13, 10, width + 18, 22, rgba(4, 11, 16, .70f));
    gfx_fill_rect(fb, 13, 10, 1, 22, rgba(188, 166, 116, .65f));
    font_draw(fb, landmark->name, 22, 25, 9, FONT_BOLD, ALIGN_LEFT, rgb(203, 196, 170));
  }
  render_horror_text(r, fb, g);
  image_reset_clip(fb);
}

// ------------------------------------------------------------------ automap

void render_map(Renderer *r, Image *fb, const Game *g, const char *close_button) {
  const Level *lv = g->level;
  int cs = maxi(1, mini(8, mini((VIEW_H - 40) / lv->h, (W - 56) / lv->w)));
  float mw = (float)(lv->w * cs), mh = (float)(lv->h * cs);
  float ox = W / 2.0f - mw / 2, oy = (VIEW_H - mh) / 2;
  gfx_fill_rect(fb, 0, 0, W, VIEW_H, rgba(3, 8, 13, .78f));
  ui_panel(fb, ox - 14, oy - 18, mw + 28, mh + 40);
  Color gold = color_parse("#c2a674");
  Color wall = color_parse("#687479"), door = color_parse("#a18a65");
  for (int y = 0; y < lv->h; y++) {
    for (int x = 0; x < lv->w; x++) {
      if (!g->visited[y * lv->w + x]) continue;
      char cell = game_cell(g, x, y);
      Color c;
      switch (cell) {
      case '.':
        c = color_parse("#1d2a34");
        break;
      case '!': c = color_parse("#8c4028"); break;
      case '=': c = color_parse("#28535b"); break;
      case '>': case '<': case '}': case '{': c = color_parse("#367681"); break;
      case 'D': c = door; break;
      case 'B': c = color_parse("#59636a"); break;
      case 'R': c = color_parse("#76828a"); break;
      case 'X': c = gold; break;
      // secret walls and unpulled levers masquerade as plain wall on the
      // map; once worked they show up in door brown / lever gold
      case 'Z': c = game_door_open_amount(g, x, y) > 0.05f ? door : wall; break;
      case 'L': c = game_lever_pulled(g, x, y) ? gold : wall; break;
      case '~': c = color_parse("#8a4fc0"); break;
      case 'N':
        c = color_parse("#485a63");
        break;
      case '8': c = color_parse("#a58a66"); break;
      default: c = wall; break;
      }
      if (cell == '.' && lv->has_heights) {
        float z = level_floor_at(lv, x, y);
        c = rgb(clampf(36 + z * 18, 15, 95), clampf(28 + z * 15, 12, 80), clampf(18 + z * 12, 8, 65));
      }
      if (cell == '.' && world_outside(lv, x, y)) {
        GroundKind ground = world_ground(lv, x, y);
        c = ground == GROUND_GRASS ? rgb(40, 60, 43) :
            ground == GROUND_EARTH || ground == GROUND_LEAVES ? rgb(65, 53, 40) : rgb(64, 70, 67);
      }
      gfx_fill_rect(fb, ox + x * cs, oy + y * cs, cs - 0.5f, cs - 0.5f, c);
      if (level_is_open(cell) && lv->has_heights) {
        static const int dirs[4][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};
        for (int d = 0; d < 4; d++) {
          int dx = dirs[d][0], dy = dirs[d][1];
          if (!level_is_open(game_cell(g, x + dx, y + dy)) ||
              level_floor_at(lv, x + dx, y + dy) <= level_floor_at(lv, x, y)) continue;
          float cx = ox + (x + .5f) * cs, cy = oy + (y + .5f) * cs;
          Color tread = color_parse("#b3a17b");
          for (int k = -1; k <= 1; k++)
            gfx_line(fb, cx + dx * k - dy * cs * .28f, cy + dy * k + dx * cs * .28f,
                     cx + dx * k + dy * cs * .28f, cy + dy * k - dx * cs * .28f, 1, tread);
          break;
        }
      }
      if (cell == '!')
        gfx_fill_rect(fb, ox + x * cs + 1, oy + y * cs + 1, cs - 2.5f, cs - 2.5f, color_parse("#080604"));
      if (level_is_water(cell) && cell != '=') {
        int dx, dy;
        level_water_flow(cell, &dx, &dy);
        float cx = ox + (x + .5f) * cs, cy = oy + (y + .5f) * cs;
        Color foam = color_parse("#8bb8b6");
        gfx_line(fb, cx - dx * cs * .25f, cy - dy * cs * .25f,
                 cx + dx * cs * .25f, cy + dy * cs * .25f, 1, foam);
        gfx_line(fb, cx + (dx + dy) * cs * .05f, cy + (dy - dx) * cs * .05f,
                 cx + dx * cs * .25f, cy + dy * cs * .25f, 1, foam);
      }
    }
  }
  // traps the player has set off
  for (int i = 0; i < g->trap_count; i++) {
    const Trap *t = &g->traps[i];
    if (!t->seen) continue;
    gfx_fill_rect(fb, ox + t->spawn->x * cs, oy + t->spawn->y * cs, cs - 0.5f, cs - 0.5f, color_parse("#8c2020"));
  }
  // Explored ceiling blades show their swing direction.
  for (int i = 0; i < lv->pendulum_count; i++) {
    const PendulumSpawn *s = &lv->pendulums[i];
    if (!g->visited[s->y * lv->w + s->x]) continue;
    float x = ox + (s->x + 0.5f) * cs, y = oy + (s->y + 0.5f) * cs;
    Color blade = color_parse("#d48751");
    gfx_line(fb, x - s->dx * cs * .4f, y - s->dy * cs * .4f,
             x + s->dx * cs * .4f, y + s->dy * cs * .4f, 1.5f, blade);
    gfx_fill_rect(fb, x - 1.5f, y - 1.5f, 3, 3, blade);
  }
  // Peaceful inhabitants appear only after their cells have been explored.
  for (int i = 0; i < lv->world.monk_count; i++) {
    const Monk *m = &lv->world.monks[i];
    if (!g->visited[m->y * lv->w + m->x]) continue;
    ui_diamond(fb, ox + (m->x + .5f) * cs, oy + (m->y + .5f) * cs, 2, rgb(219, 204, 157));
  }
  // player arrow
  const Player *p = &g->player;
  float ang = atan2f(p->dir_y, p->dir_x);
  float c = cosf(ang), s = sinf(ang);
  float cx = ox + p->x * cs, cy = oy + p->y * cs;
  const float local[3][2] = {{cs * 0.9f, 0}, {-cs * 0.5f, -cs * 0.5f}, {-cs * 0.5f, cs * 0.5f}};
  float pts[6];
  for (int i = 0; i < 3; i++) {
    pts[i * 2] = cx + local[i][0] * c - local[i][1] * s;
    pts[i * 2 + 1] = cy + local[i][0] * s + local[i][1] * c;
  }
  gfx_fill_poly(fb, pts, 3, color_parse("#ffd23c"));
  char hint[64];
  snprintf(hint, sizeof hint, "MAP — %s TO CLOSE", close_button ? close_button : "TAB");
  font_draw(fb, hint, W / 2.0f, oy + mh + 6, 10, FONT_REGULAR, ALIGN_CENTER, color_parse("#a89878"));
}

// ------------------------------------------------------------------ HUD bar

static void hud_text(Image *fb, const char *value, float x, float y, float size, Color color) {
  font_draw(fb, value, x, y, size, FONT_BOLD, ALIGN_LEFT, color);
}

void render_hud(Renderer *r, Image *fb, const Game *g) {
  const Player *p = &g->player;
  const float y = VIEW_H;
  Color gold = rgb(194, 166, 116), cream = rgb(231, 224, 208), muted = rgb(138, 154, 162);
  for (int row = 0; row < H - VIEW_H; row++) {
    float k = 1 - row / (float)(H - VIEW_H);
    gfx_fill_rect(fb, 0, y + row, W, 1, rgb(8 + k * 8, 13 + k * 8, 18 + k * 9));
  }
  gfx_fill_rect(fb, 0, y, W, 1, rgb(91, 87, 72));
  gfx_fill_rect(fb, 0, y + 1, W, 2, rgba(0, 0, 0, .75f));
  ui_rule(fb, 12, y + 4, W - 24);
  for (int i = 0; i < 4; i++) {
    float x = (float[]){178, 288, 401, 493}[i];
    gfx_fill_rect(fb, x, y + 19, 1, 42, rgba(153, 147, 123, .22f));
    ui_diamond(fb, x + .5f, y + 13, 1.5f, rgba(194, 166, 116, .5f));
  }
  ui_emblem(fb, 32, y + 40, 34);
  if (p->health <= 25) {
    gfx_line(fb, 30, y + 28, 35, y + 37, 1, rgb(151, 66, 58));
    gfx_line(fb, 35, y + 37, 29, y + 48, 1, rgb(151, 66, 58));
  }
  hud_text(fb, p->venom > 0 ? "VENOM" : "VITALITY", 61, y + 24, 8, muted);
  char buf[40];
  snprintf(buf, sizeof buf, "%d", maxi(0, p->health));
  hud_text(fb, buf, 60, y + 49, 25, p->health <= 25 ? rgb(223, 123, 110) : cream);
  hud_text(fb, "/ 100", 118, y + 48, 10, muted);
  gfx_fill_rect(fb, 61, y + 57, 98, 5, rgb(3, 6, 8));
  float health = clampf(p->health / 100.0f, 0, 1);
  Color bar = p->venom > 0 ? rgb(118, 148, 77) : rgb(149, 59, 55);
  gfx_fill_rect(fb, 62, y + 58, 96 * health, 3, bar);
  gfx_fill_rect(fb, 62, y + 58, 96 * health, 1, color_shade(bar, 1.4f));
  hud_text(fb, "LIVES", 61, y + 73, 8, muted);
  for (int i = 0; i < mini(g->session->lives, 8); i++)
    ui_diamond(fb, 101 + i * 7, y + 70, 2, gold);
  hud_text(fb, "BOLTS", 195, y + 24, 8, muted);
  if (p->weapon == WEAPON_DAGGER)
    snprintf(buf, sizeof buf, "--");
  else
    snprintf(buf, sizeof buf, "%02d", p->ammo);
  hud_text(fb, buf, 194, y + 50, 25, cream);
  const Image *icon = r->assets->weapon_icons[p->weapon];
  gfx_blit(fb, icon, 0, 0, icon->w, icon->h, 245, y + 28, 27, 27, .8f);
  static const char *names[] = {"DAGGER", "CROSSBOW", "REPEATER"};
  hud_text(fb, names[p->weapon], 195, y + 71, 8, gold);
  hud_text(fb, "GATE KEYS", 309, y + 24, 8, muted);
  for (int i = 0; i < 3; i++) {
    float x = 318 + i * 25;
    Color c = i < p->keys ? gold : rgb(56, 66, 71);
    float ring[] = {x - 4, y + 36, x, y + 32, x + 4, y + 36, x, y + 40};
    gfx_stroke_poly(fb, ring, 4, 1.4f, c);
    gfx_line(fb, x, y + 40, x, y + 53, 1.5f, c);
    gfx_line(fb, x, y + 49, x + 4, y + 49, 1.5f, c);
    gfx_line(fb, x, y + 53, x + 4, y + 53, 1.5f, c);
  }
  snprintf(buf, sizeof buf, "%d / 3 COLLECTED", p->keys);
  hud_text(fb, buf, 309, y + 71, 8, p->keys == 3 ? gold : muted);
  hud_text(fb, "CHAPTER", 420, y + 24, 8, muted);
  snprintf(buf, sizeof buf, "%02d", g->level_index + 1);
  hud_text(fb, buf, 418, y + 50, 25, cream);
  snprintf(buf, sizeof buf, "OF %02d", LEVEL_COUNT);
  hud_text(fb, buf, 420, y + 71, 8, gold);
  hud_text(fb, "SCORE", 511, y + 24, 8, muted);
  snprintf(buf, sizeof buf, "%06d", g->session->score);
  hud_text(fb, buf, 510, y + 49, 20, cream);
  hud_text(fb, "ESC  PAUSE", 511, y + 71, 8, muted);
}
