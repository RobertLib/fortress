// Raycasting renderer: walls via DDA (with sliding doors), billboard
// sprites clipped against a z-buffer, POV weapon, HUD and automap.
// Torchlit atmosphere: everything fades into darkness with distance.

#include "render.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "font.h"

#define W SCREEN_W
#define H SCREEN_H
#define MAX_STEPS 96
#define NEAR_PLANE 0.05f

static const Color FOG = {10, 8, 6, 1}; // the darkness everything sinks into

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
    [HANG_FIREPLACE] = {0, 0.62f, 0.03f},
};

// distance buckets for pre-darkened sprite variants
static const float SPRITE_SHADES[4] = {1, 0.78f, 0.58f, 0.42f};

static int shade_bucket(float dist) { return dist < 3.5f ? 0 : dist < 6 ? 1 : dist < 9 ? 2 : 3; }

static float torch_flicker(float time, float phase) {
  return 0.84f + sinf(time * 17 + phase) * 0.1f + sinf(time * 29 + phase * 1.7f) * 0.06f;
}

static float torch_light(const Level *lv, float x, float y, float time) {
  float light = 0;
  for (int i = 0; i < lv->torch_count; i++) {
    const Torch *t = &lv->torches[i];
    float dist = hypotf((float)t->x - x, (float)t->y - y);
    if (dist >= 3.4f) continue;
    light = maxf(light, (1 - dist / 3.4f) * torch_flicker(time, (float)t->phase));
  }
  // the hearths burn like oversized torches and reach a little further
  for (int i = 0; i < lv->fire_light_count; i++) {
    const Torch *f = &lv->fire_lights[i];
    float dist = hypotf((float)f->x - x, (float)f->y - y);
    if (dist >= 4.2f) continue;
    light = maxf(light, (1 - dist / 4.2f) * torch_flicker(time, (float)f->phase));
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
typedef struct {
  float k, r, g, b;
} Tint;

static void tint_add(Tint *t, float r, float g, float b, float a) {
  t->k *= 1 - a;
  t->r = t->r * (1 - a) + r * a;
  t->g = t->g * (1 - a) + g * a;
  t->b = t->b * (1 - a) + b * a;
}

static Tint surface_tint(const Game *g, float x, float y, float dist) {
  Tint t = {1, 0, 0, 0};
  float light = torch_light(g->level, x, y, g->time);
  if (light > 0.015f) tint_add(&t, 255, 132, 32, minf(0.16f, light * 0.14f));
  float glow = key_light(g, x, y);
  if (glow > 0.02f) tint_add(&t, 255, 214, 92, minf(0.1f, glow * 0.1f));
  float fog = maxf(0, fog_amount(dist) - light * 0.42f - glow * 0.3f);
  if (fog > 0.02f) tint_add(&t, FOG.r, FOG.g, FOG.b, fog);
  return t;
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
static void draw_column(Image *fb, const Image *tex, int tex_x, int x, float top, float height, const Tint *tint) {
  int y0, y1;
  if (height <= 0 || x < fb->clip_x0 || x >= fb->clip_x1 || !column_rows(fb, top, height, &y0, &y1)) return;
  float ky = (float)tex->h / height;
  for (int py = y0; py < y1; py++) {
    int ty = (int)floorf(((float)py + 0.5f - top) * ky);
    if (ty < 0) ty = 0;
    if (ty >= tex->h) ty = tex->h - 1;
    uint32_t *d = fb->px + (size_t)py * fb->w + x;
    px_over(d, tex->px[ty * tex->w + tex_x], 1);
    if (tint) {
      uint32_t v = *d;
      *d = pack_argb(255, (float)((v >> 16) & 255) * tint->k + tint->r, (float)((v >> 8) & 255) * tint->k + tint->g,
                     (float)(v & 255) * tint->k + tint->b);
    }
  }
}

static int tex_column(float u) {
  int t = (int)floorf(u * TEX);
  return t < 0 ? 0 : t > TEX - 1 ? TEX - 1 : t;
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
  // static vignette overlay for the 3D view
  r->vignette = image_new(W, VIEW_H);
  GradientStop stops[2] = {{0, {0, 0, 0, 0}}, {1, {6, 4, 2, 0.5f}}};
  gfx_radial(r->vignette, 0, 0, W, VIEW_H, W / 2.0f, VIEW_H / 2.0f, VIEW_H * 0.45f, W * 0.62f, stops, 2, BLEND_OVER);
}

void renderer_free(Renderer *r) {
  image_free(r->vignette);
  free(r->shake_buf);
  r->vignette = NULL;
  r->shake_buf = NULL;
}

// ceiling and floor sink into darkness at the horizon
static void build_sky(Renderer *r, const Level *lv) {
  r->sky_level = lv;
  for (int y = 0; y < VIEW_H; y++) {
    Color a, b;
    float t;
    if (y < VIEW_H / 2) {
      a = lv->ceil_color;
      b = FOG;
      t = ((float)y + 0.5f) / (VIEW_H / 2.0f);
    } else {
      float f = ((float)y + 0.5f - VIEW_H / 2.0f) / (VIEW_H / 2.0f);
      a = FOG;
      b = lv->floor_color;
      t = minf(1, f / 0.25f);
    }
    r->sky_rows[y] = pack_argb(255, a.r + (b.r - a.r) * t, a.g + (b.g - a.g) * t, a.b + (b.b - a.b) * t);
  }
}

// ------------------------------------------------------------------ walls

typedef struct {
  float dist;
  int side;
  int tex;
  float wall_x;
} Hit;

static void push_overlay(Renderer *r, int column, float dist, int side, int tex, float wall_x) {
  if (r->overlay_count[column] >= MAX_OVERLAYS) return;
  r->overlays[column][r->overlay_count[column]++] = (Overlay){dist, side, tex, wall_x};
}

static Hit cast_ray(Renderer *r, const Game *g, int column, float pos_x, float pos_y, float ray_x, float ray_y) {
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
        if (coord >= 0 && coord < 1) return (Hit){dist_mid, side, WT_NICHE, coord};
        continue;
      }
      float open = game_door_open_amount(g, map_x, map_y);
      if (coord >= 0 && coord < 1 && coord >= open) {
        if (cell == 'R') {
          push_overlay(r, column, dist_mid, side, WT_BAR_DOOR, coord - open);
          continue;
        }
        return (Hit){dist_mid, side, WT_DOOR, coord - open};
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
      push_overlay(r, column, dist, side, WT_BARS, wall_x);
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
    return (Hit){dist, side, tex, wall_x};
  }
  return (Hit){1e9f, 0, WT_STONE, 0};
}

// Cast the floor plane into the map. A pit replaces its patch of floor
// with a stone lip and the inside of a shaft, shaded down into darkness.
// It never writes the column z-buffer: sight and arrows pass over a hole.
static void render_pits(Renderer *r, Image *fb, const Game *g) {
  if (!g->level->pit_count) return;
  const Player *p = &g->player;
  const Image *stone = r->assets->walls[WT_STONE];
  for (int x = 0; x < W; x++) {
    float camera_x = 2.0f * x / W - 1;
    float rx = p->dir_x + p->plane_x * camera_x;
    float ry = p->dir_y + p->plane_y * camera_x;
    int last_cell = -1;
    Tint tint = {1, 0, 0, 0};
    for (int y = VIEW_H / 2; y < VIEW_H; y++) {
      float slope = ((float)y + 0.5f - VIEW_H / 2.0f) / VIEW_H;
      float dist = 0.5f / slope;
      if (dist >= r->zbuffer[x]) continue;
      float wx = p->x + rx * dist, wy = p->y + ry * dist;
      int cx = (int)floorf(wx), cy = (int)floorf(wy);
      if (game_cell(g, cx, cy) != '!') continue;
      int cell = cy * g->level->w + cx;
      if (cell != last_cell) {
        float depth = (cx + 0.5f - p->x) * p->dir_x + (cy + 0.5f - p->y) * p->dir_y;
        tint = surface_tint(g, cx + 0.5f, cy + 0.5f, maxf(0, depth));
        last_cell = cell;
      }
      float u = wx - cx, v = wy - cy;
      float edge = minf(minf(u, 1 - u), minf(v, 1 - v));
      uint32_t color;
      if (edge < PIT_RIM) {
        // The rim's lighter bevel makes the opening readable in the gloom.
        color = stone->px[tex_column(v) * stone->w + tex_column(u)];
        float k = edge > PIT_RIM - 0.016f ? 1.18f : edge < 0.012f ? 0.55f : 0.85f;
        color = pack_argb(255, minf(255, ((color >> 16) & 255) * k),
                          minf(255, ((color >> 8) & 255) * k), minf(255, (color & 255) * k));
      } else {
        // Continue the same ray until it meets the inner wall of the shaft.
        float ex = rx != 0 ? ((rx > 0 ? cx + 1 - PIT_RIM : cx + PIT_RIM) - p->x) / rx : INFINITY;
        float ey = ry != 0 ? ((ry > 0 ? cy + 1 - PIT_RIM : cy + PIT_RIM) - p->y) / ry : INFINITY;
        float exit = minf(ex, ey);
        float depth = maxf(0, exit * slope - 0.5f);
        float along = ex < ey ? p->y + ry * exit : p->x + rx * exit;
        along -= floorf(along);
        float tz = depth - floorf(depth);
        color = stone->px[tex_column(tz) * stone->w + tex_column(along)];
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
      float line_h = VIEW_H / o->dist;
      const Image *tex = (o->side == 1 ? r->assets->walls_dark : r->assets->walls)[o->tex];
      draw_column(fb, tex, tex_column(o->wall_x), x, VIEW_H / 2.0f - line_h / 2, line_h, NULL);
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
    if (dist >= r->zbuffer[x]) continue;
    float line_h = VIEW_H / dist;
    float top = VIEW_H / 2.0f + line_h * (0.5f - height);
    Tint tint = surface_tint(g, x0 + (x1 - x0) * u, y0 + (y1 - y0) * u, dist);
    draw_column(fb, tex, tex_column(u), x, top, line_h * height, &tint);
    r->zbuffer[x] = dist;
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
    if (s_far <= s_near || s_near >= r->zbuffer[x]) continue;

    float rise = (0.5f - spec->height) * VIEW_H; // eye sits above the lid
    float y_top = VIEW_H / 2.0f + rise / s_far;
    float y_bot = VIEW_H / 2.0f + rise / s_near;
    if (y_bot - y_top < 0.05f) continue;
    float s_mid = (s_near + s_far) / 2;
    float u = (pt + s_mid * rt + spec->half_w) / (spec->half_w * 2);
    Tint tint = surface_tint(g, p->x + rdx * s_mid, p->y + rdy * s_mid, s_mid);
    draw_column(fb, tex, tex_column(u), x, y_top, y_bot - y_top, &tint);
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
    if (spec->height < 0.5f && texs->lid) draw_box_lid(r, fb, g, wd, spec, texs->lid);
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

static void draw_hanging_quad(Renderer *r, Image *fb, const Game *g, float x0, float y0, float x1, float y1,
                              const Image *tex, const HangingSpec *spec) {
  QuadSpan q;
  if (!quad_span(&g->player, x0, y0, x1, y1, &q)) return;
  for (int x = q.x_start; x <= q.x_end; x++) {
    float dist, u;
    quad_at(&q, x, &dist, &u);
    if (dist >= r->zbuffer[x]) continue;
    float line_h = VIEW_H / dist;
    float top = VIEW_H / 2.0f + line_h * (0.5f - spec->z_top);
    draw_column(fb, tex, tex_column(u), x, top, line_h * (spec->z_top - spec->z_bot), NULL);
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
    float light = torch_light(lv, fx, fy, g->time);
    if (light > 0.5f)
      bucket = maxi(0, bucket - 2);
    else if (light > 0.15f)
      bucket = maxi(0, bucket - 1);
    float ax = fx + tx * half_w, ay = fy + ty * half_w;
    float bx = fx - tx * half_w, by = fy - ty * half_w;
    draw_hanging_quad(r, fb, g, ax, ay, bx, by, shaded(img, bucket), spec);
    if (hg->kind == HANG_FIREPLACE) {
      // the blaze rides the same quad unshaded, so it registers with the
      // firebox exactly and keeps burning through the distance fog; each
      // hearth flickers on its own schedule
      int frame = ((int)floorf(g->time * 10) + ((hg->x * 31 + hg->y * 17) & 3)) & 3;
      draw_hanging_quad(r, fb, g, ax, ay, bx, by, as->fire_flames[frame], spec);
    }
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
    add_sprite(b->x, b->y, as->barrels[b->destroyed ? 2 : b->powder ? 1 : 0], 0.78f);
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
  for (int i = 0; i < g->trap_count; i++) {
    const Trap *t = &g->traps[i];
    add_sprite(t->spawn->x + 0.5f, t->spawn->y + 0.5f, as->trap[t->state == TRAP_ARMED ? TRAP_PLATE : TRAP_PRESSED],
               0.6f);
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
    s->z_center = a->z - 0.03f; // the shaft is painted just above the texture's middle
  }
  for (int i = 0; i < lv->decoration_count; i++) {
    const Decoration *d = &lv->decorations[i];
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
      s->z_center = 1 - 0.55f / 2;
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
    float scale = d->kind == DECO_TABLE    ? 0.7f
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
  }
  for (int i = 0; i < sprite_count; i++) sprites[i].dist = sq(p->x - sprites[i].x) + sq(p->y - sprites[i].y);
  qsort(sprites, (size_t)sprite_count, sizeof(Sprite), cmp_sprite);
}

static void pendulum_quad(Renderer *r, Image *fb, const Game *g, const Image *tex,
                          float x0, float y0, float x1, float y1, float bottom, float top) {
  QuadSpan q;
  if (!quad_span(&g->player, x0, y0, x1, y1, &q)) return;
  for (int x = q.x_start; x <= q.x_end; x++) {
    float dist, u;
    quad_at(&q, x, &dist, &u);
    if (dist >= r->zbuffer[x]) continue;
    float h = VIEW_H / dist;
    const Image *img = shaded(tex, shade_bucket(dist));
    draw_column(fb, img, tex_column(u), x, VIEW_H / 2.0f + h * (0.5f - top), h * (top - bottom), NULL);
    render_transparent_walls(r, fb, x, x, dist);
  }
}

static void render_pendulum(Renderer *r, Image *fb, const Game *g, const PendulumSpawn *s) {
  float angle = game_pendulum_angle(s, g->time);
  int frame = clampi((int)round_half_up((angle / PENDULUM_MAX_ANGLE + 1) * 0.5f * (PENDULUM_FRAMES - 1)),
                     0, PENDULUM_FRAMES - 1);
  float cx = s->x + 0.5f, cy = s->y + 0.5f;
  float half = PENDULUM_VIEW_WIDTH / 2;
  pendulum_quad(r, fb, g, r->assets->pendulum[frame], cx - s->dx * half, cy - s->dy * half,
                cx + s->dx * half, cy + s->dy * half, 0, 1);

  // A narrow edge and shaft remain visible when looking along the swing's
  // plane. In a face-on view the painted blade already covers these parts.
  float facing = fabsf((cx - g->player.x) * s->dy - (cy - g->player.y) * s->dx);
  float along = fabsf((cx - g->player.x) * s->dx + (cy - g->player.y) * s->dy);
  if (facing >= along * 0.25f) return;
  float sn = sinf(angle), cs = cosf(angle);
  for (int i = 0; i < 12; i++) {
    float radius = PENDULUM_LENGTH * (i + 0.5f) / 12;
    float bx = cx + s->dx * radius * sn, by = cy + s->dy * radius * sn;
    float z = PENDULUM_PIVOT - radius * cs;
    pendulum_quad(r, fb, g, r->assets->pendulum_edge, bx - s->dy * .015f, by + s->dx * .015f,
                  bx + s->dy * .015f, by - s->dx * .015f, z - .025f, z + .025f);
  }
  float bx = cx + s->dx * PENDULUM_LENGTH * sn, by = cy + s->dy * PENDULUM_LENGTH * sn;
  float z = PENDULUM_PIVOT - PENDULUM_LENGTH * cs;
  float height = PENDULUM_BLADE_HEIGHT * cs + PENDULUM_BLADE_WIDTH * fabsf(sn);
  pendulum_quad(r, fb, g, r->assets->pendulum_edge, bx - s->dy * .035f, by + s->dx * .035f,
                bx + s->dy * .035f, by - s->dx * .035f, z - height, z + height);
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
    float top = !s->has_z ? VIEW_H / 2.0f + full_h / 2 - spr_h
                          : VIEW_H / 2.0f + full_h * (0.5f - s->z_center) - spr_h / 2;
    int start_x = (int)floorf(screen_x - spr_w / 2);
    int end_x = (int)ceilf(screen_x + spr_w / 2);
    if (end_x < 0 || start_x >= W || spr_w < 1) continue;

    // draw in vertical stripes, clipped by the wall z-buffer, darkened with
    // distance to match the wall fog
    const Image *img = s->img;
    int bucket = 0;
    if (!s->fullbright) {
      bucket = shade_bucket(transform_y);
      float light = torch_light(g->level, s->x, s->y, g->time);
      if (light > 0.5f)
        bucket = maxi(0, bucket - 2);
      else if (light > 0.15f)
        bucket = maxi(0, bucket - 1);
      img = shaded(s->img, bucket);
    }
    // the dropline goes in first, so the spider hangs in front of its own
    // thread; a wall in front of the middle column hides all of it
    int mid = (int)floorf(screen_x);
    float ceil_y = VIEW_H / 2.0f - full_h / 2;
    if (s->dropline && mid >= 0 && mid < W && transform_y < r->zbuffer[mid] && top > ceil_y) {
      float lw = maxf(1, full_h / 240);
      float f = SPRITE_SHADES[bucket];
      gfx_fill_rect(fb, screen_x - lw / 2, ceil_y, lw, top + 1 - ceil_y, rgba(216 * f, 211 * f, 198 * f, 0.5f));
    }
    int x0 = maxi(0, start_x), x1 = mini(W - 1, end_x);
    int run_start = -1;
    for (int x = x0; x <= x1 + 1; x++) {
      bool visible = x <= x1 && transform_y < r->zbuffer[x];
      if (visible && run_start < 0) run_start = x;
      if (!visible && run_start >= 0) {
        float tex_x0 = ((float)(run_start - start_x) / spr_w) * TEX;
        float tex_x1 = ((float)(x - start_x) / spr_w) * TEX;
        gfx_blit(fb, img, tex_x0, 0, maxf(0.01f, tex_x1 - tex_x0), TEX, (float)run_start, top,
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
  if (transform_y > r->zbuffer[zi] + 0.15f) return;
  float full_h = VIEW_H / transform_y;
  float screen_y = VIEW_H / 2.0f + full_h * (0.5f - gs->height);
  float radius = maxf(gs->min_r, minf(gs->max_r, full_h * gs->r_scale));
  gfx_radial(fb, screen_x - radius, screen_y - radius, radius * 2, radius * 2, screen_x, screen_y, 0, radius, gs->stops,
             3, BLEND_SCREEN);
}

static GradientStop stop(float pos, float r, float g, float b, float a) {
  GradientStop s = {pos, {r, g, b, a}};
  return s;
}

static void render_glows(Renderer *r, Image *fb, const Game *g) {
  const Player *p = &g->player;
  const Level *lv = g->level;
  for (int i = 0; i < g->barrel_count; i++) {
    const Barrel *b = &g->barrels[i];
    float heat = maxf(0, b->blast_timer / BARREL_EXPLOSION_TIME * 2 - 1);
    if (heat <= 0) continue;
    GlowSpec gs = {b->x, b->y, 0.5f, 220, 12, 180, 1.2f,
                   {stop(0, 255, 190, 80, 0.5f * heat), stop(0.3f, 255, 100, 20, 0.2f * heat),
                    stop(1, 255, 70, 0, 0)}};
    draw_glow(r, fb, p, &gs);
  }
  for (int i = 0; i < lv->torch_count; i++) {
    const Torch *t = &lv->torches[i];
    float flicker = torch_flicker(g->time, (float)t->phase);
    GlowSpec gs = {(float)t->x, (float)t->y, 0.72f, 180, 16, 150, 0.62f,
                   {stop(0, 255, 174, 55, 0.2f * flicker), stop(0.28f, 255, 112, 22, 0.1f * flicker),
                    stop(1, 255, 80, 0, 0)}};
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
    draw_glow(r, fb, p, &gs);
  }
}

// ------------------------------------------------------------------ weapon

static void render_weapon(Renderer *r, Image *fb, const Game *g) {
  const Player *p = &g->player;
  const Image *img = r->assets->weapons[p->weapon][clampi(p->weapon_frame, 0, 2)];
  const float size = 300;
  float bob_x = sinf(p->bob_phase) * 10 * p->bob_amount;
  float bob_y = fabsf(cosf(p->bob_phase)) * 8 * p->bob_amount + 4;
  gfx_blit(fb, img, 0, 0, (float)img->w, (float)img->h, W / 2.0f - size / 2 + bob_x, VIEW_H - size + 28 + bob_y, size,
           size, 1);
  // crosshair
  gfx_fill_rect(fb, W / 2.0f - 1, VIEW_H / 2.0f - 1, 2, 2, rgba(255, 255, 255, 0.6f));
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

// ------------------------------------------------------------------ 3D view

void render_view(Renderer *r, Image *fb, const Game *g) {
  const Level *lv = g->level;
  const Player *p = &g->player;
  const Assets *as = r->assets;
  image_set_clip(fb, 0, 0, W, VIEW_H);

  if (r->sky_level != lv) build_sky(r, lv);
  for (int y = 0; y < VIEW_H; y++) {
    uint32_t *row = fb->px + (size_t)y * fb->w;
    uint32_t c = r->sky_rows[y];
    for (int x = 0; x < W; x++) row[x] = c;
  }

  for (int x = 0; x < W; x++) {
    float camera_x = 2.0f * x / W - 1;
    float ray_x = p->dir_x + p->plane_x * camera_x;
    float ray_y = p->dir_y + p->plane_y * camera_x;
    Hit hit = cast_ray(r, g, x, p->x, p->y, ray_x, ray_y);
    r->zbuffer[x] = hit.dist;
    if (hit.dist <= 0.0001f) continue;
    float line_h = VIEW_H / hit.dist;
    const Image *tex = (hit.side == 1 ? as->walls_dark : as->walls)[hit.tex];
    Tint tint = surface_tint(g, p->x + ray_x * hit.dist, p->y + ray_y * hit.dist, hit.dist);
    draw_column(fb, tex, tex_column(hit.wall_x), x, VIEW_H / 2.0f - line_h / 2, line_h, &tint);
  }

  render_pits(r, fb, g);

  // Wall boxes (wardrobes, chests) are opaque geometry: drawn right after
  // the walls, z-tested and written per column, so bars and sprites clip
  // against them too.
  render_wall_boxes(r, fb, g);
  render_wall_hangings(r, fb, g);
  render_transparent_walls(r, fb, 0, W - 1, INFINITY);
  render_glows(r, fb, g);
  render_sprites(r, fb, g);
  gfx_blit(fb, r->vignette, 0, 0, W, VIEW_H, 0, 0, W, VIEW_H, 1);
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
  image_reset_clip(fb);
}

// ------------------------------------------------------------------ automap

void render_map(Renderer *r, Image *fb, const Game *g) {
  const Level *lv = g->level;
  int cs = mini(8, (VIEW_H - 40) / lv->h);
  float mw = (float)(lv->w * cs), mh = (float)(lv->h * cs);
  float ox = W / 2.0f - mw / 2, oy = (VIEW_H - mh) / 2;
  gfx_fill_rect(fb, ox - 10, oy - 10, mw + 20, mh + 20, rgba(16, 10, 4, 0.8f));
  Color gold = color_parse("#c9a24a");
  gfx_fill_rect(fb, ox - 10, oy - 10, mw + 20, 1, gold);
  gfx_fill_rect(fb, ox - 10, oy + mh + 9, mw + 20, 1, gold);
  Color wall = color_parse("#655743"), door = color_parse("#8a5a28");
  for (int y = 0; y < lv->h; y++) {
    for (int x = 0; x < lv->w; x++) {
      if (!g->visited[y * lv->w + x]) continue;
      char cell = game_cell(g, x, y);
      Color c;
      switch (cell) {
      case '.': c = color_parse("#241c12"); break;
      case '!': c = color_parse("#8c4028"); break;
      case 'D': c = door; break;
      case 'B': c = color_parse("#59636a"); break;
      case 'R': c = color_parse("#76828a"); break;
      case 'X': c = gold; break;
      // secret walls and unpulled levers masquerade as plain wall on the
      // map; once worked they show up in door brown / lever gold
      case 'Z': c = game_door_open_amount(g, x, y) > 0.05f ? door : wall; break;
      case 'L': c = game_lever_pulled(g, x, y) ? gold : wall; break;
      case '~': c = color_parse("#8a4fc0"); break;
      case 'N': c = color_parse("#57493a"); break;
      case '8': c = color_parse("#a58a66"); break;
      default: c = wall; break;
      }
      gfx_fill_rect(fb, ox + x * cs, oy + y * cs, cs - 0.5f, cs - 0.5f, c);
      if (cell == '!')
        gfx_fill_rect(fb, ox + x * cs + 1, oy + y * cs + 1, cs - 2.5f, cs - 2.5f, color_parse("#080604"));
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
  font_draw(fb, "MAP — TAB TO CLOSE", W / 2.0f, oy + mh + 6, 10, FONT_REGULAR, ALIGN_CENTER, color_parse("#a89878"));
}

// ------------------------------------------------------------------ HUD bar

static void hud_frame(Image *fb, float x, float w) {
  const float y0 = VIEW_H, hud_h = H - VIEW_H;
  gfx_fill_rect(fb, x, y0 + 8, w, hud_h - 16, color_parse("#1a1208"));
  gfx_fill_rect(fb, x, y0 + 8, w, 1, rgba(201, 162, 74, 0.35f));
  gfx_fill_rect(fb, x, y0 + hud_h - 9, w, 1, rgba(201, 162, 74, 0.35f));
}

static void hud_label(Image *fb, float cx, const char *label) {
  font_draw(fb, label, cx, VIEW_H + 22, 9, FONT_REGULAR, ALIGN_CENTER, color_parse("#b08c46"));
}

static void hud_panel(Image *fb, float x, float w, const char *label, const char *value, Color color) {
  hud_frame(fb, x, w);
  hud_label(fb, x + w / 2, label);
  font_draw(fb, value, x + w / 2, VIEW_H + 52, 22, FONT_BOLD, ALIGN_CENTER, color);
}

void render_hud(Renderer *r, Image *fb, const Game *g) {
  const Assets *as = r->assets;
  const float y0 = VIEW_H, hud_h = H - VIEW_H;
  const Player *p = &g->player;

  // oak planks bound with iron
  gfx_fill_rect(fb, 0, y0, W, hud_h, color_parse("#332412"));
  for (int x = 40; x < W; x += 160) gfx_fill_rect(fb, (float)x, y0, 1, hud_h, rgba(0, 0, 0, 0.25f));
  gfx_fill_rect(fb, 0, y0 + hud_h / 2, W, 1, rgba(90, 60, 26, 0.5f));
  gfx_fill_rect(fb, 0, y0, W, 4, color_parse("#23262b")); // iron rim
  gfx_fill_rect(fb, 0, H - 4, W, 4, color_parse("#23262b"));
  gfx_fill_rect(fb, 0, y0, W, 1, color_parse("#565c66"));
  for (int x = 14; x < W; x += 52) { // rivets
    gfx_fill_rect(fb, (float)x, y0 + 1, 2, 2, color_parse("#7c828e"));
    gfx_fill_rect(fb, (float)x, H - 3, 2, 2, color_parse("#7c828e"));
  }

  Color cream = color_parse("#ead9b0");
  char buf[32];
  snprintf(buf, sizeof buf, "%d", g->level_index + 1);
  hud_panel(fb, 10, 70, "FLOOR", buf, cream);
  snprintf(buf, sizeof buf, "%d", g->session->score);
  hud_panel(fb, 88, 110, "SCORE", buf, cream);
  snprintf(buf, sizeof buf, "%d", g->session->lives);
  hud_panel(fb, 206, 62, "LIVES", buf, cream);

  // heraldic crest that shatters as the player weakens
  int hp = p->health;
  CrestState crest = hp <= 0 ? CREST_DEAD : hp >= 75 ? CREST_HEALTHY : hp >= 50 ? CREST_OK : hp >= 25 ? CREST_HURT : CREST_BAD;
  hud_frame(fb, 276, 62);
  const Image *ci = as->crests[crest];
  gfx_blit(fb, ci, 0, 0, (float)ci->w, (float)ci->h, 283, y0 + 12, 48, 48, 1);

  snprintf(buf, sizeof buf, "%d%%", maxi(0, hp));
  // the figure turns venom green while the poison is at work
  Color hp_color = p->venom > 0 ? color_parse("#9ad04a") : hp > 25 ? cream : color_parse("#e05050");
  hud_panel(fb, 346, 84, "HEALTH", buf, hp_color);
  if (p->weapon == WEAPON_DAGGER)
    snprintf(buf, sizeof buf, "-");
  else
    snprintf(buf, sizeof buf, "%d", p->ammo);
  hud_panel(fb, 438, 70, "BOLTS", buf, cream);

  // keys
  hud_frame(fb, 516, 60);
  hud_label(fb, 546, "KEYS");
  for (int i = 0; i < 3; i++)
    gfx_blit(fb, as->items[ITEM_KEY], 16, 16, 32, 40, 522.0f + i * 18, y0 + 26, 14, 22, i < p->keys ? 1 : 0.18f);

  // weapon icon panel
  hud_frame(fb, 584, 48);
  hud_label(fb, 608, "ARMS");
  const Image *icon = as->weapon_icons[p->weapon];
  gfx_blit(fb, icon, 0, 0, (float)icon->w, (float)icon->h, 591, y0 + 24, 34, 34, 1);
}
