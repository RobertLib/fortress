// Software raster primitives. See gfx.h.

#include "gfx.h"

#include <ctype.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

// ------------------------------------------------------------------ colors

static int hexval(int ch) {
  if (ch >= '0' && ch <= '9') return ch - '0';
  ch = tolower(ch);
  if (ch >= 'a' && ch <= 'f') return ch - 'a' + 10;
  return 0;
}

Color color_parse(const char *s) {
  Color c = {0, 0, 0, 1};
  if (!s) return c;
  if (s[0] == '#') {
    size_t n = strlen(s + 1);
    const char *h = s + 1;
    if (n == 3) {
      c.r = (float)(hexval(h[0]) * 17);
      c.g = (float)(hexval(h[1]) * 17);
      c.b = (float)(hexval(h[2]) * 17);
    } else if (n >= 6) {
      c.r = (float)(hexval(h[0]) * 16 + hexval(h[1]));
      c.g = (float)(hexval(h[2]) * 16 + hexval(h[3]));
      c.b = (float)(hexval(h[4]) * 16 + hexval(h[5]));
      if (n >= 8) c.a = (float)(hexval(h[6]) * 16 + hexval(h[7])) / 255.0f;
    }
    return c;
  }
  const char *p = strchr(s, '(');
  if (!p) return c;
  float v[4] = {0, 0, 0, 1};
  int count = 0;
  p++;
  while (*p && count < 4) {
    char *end;
    float f = strtof(p, &end);
    if (end == p) break;
    v[count++] = f;
    p = end;
    while (*p == ',' || *p == ' ') p++;
  }
  c.r = v[0];
  c.g = v[1];
  c.b = v[2];
  c.a = count >= 4 ? v[3] : 1.0f;
  return c;
}

Color color_shade(Color c, float f) {
  c.r = fminf(255.0f, floorf(c.r * f));
  c.g = fminf(255.0f, floorf(c.g * f));
  c.b = fminf(255.0f, floorf(c.b * f));
  return c;
}

// ------------------------------------------------------------------ images

Image *image_new(int w, int h) {
  Image *img = calloc(1, sizeof(Image));
  img->w = w;
  img->h = h;
  img->px = calloc((size_t)w * (size_t)h, sizeof(uint32_t));
  image_reset_clip(img);
  return img;
}

void image_free(Image *img) {
  if (!img) return;
  for (int i = 0; i < 4; i++) image_free(img->shades[i]);
  free(img->px);
  free(img);
}

Image *image_clone(const Image *src) {
  Image *img = image_new(src->w, src->h);
  memcpy(img->px, src->px, (size_t)src->w * (size_t)src->h * sizeof(uint32_t));
  return img;
}

Image *image_mirror(const Image *src) {
  Image *img = image_new(src->w, src->h);
  for (int y = 0; y < src->h; y++) {
    for (int x = 0; x < src->w; x++) img->px[y * src->w + x] = src->px[y * src->w + (src->w - 1 - x)];
  }
  return img;
}

Image *image_darken(const Image *src, float f) {
  Image *img = image_new(src->w, src->h);
  float k = floorf(f * 255.0f) / 255.0f;
  size_t n = (size_t)src->w * (size_t)src->h;
  for (size_t i = 0; i < n; i++) {
    uint32_t v = src->px[i];
    img->px[i] = pack_argb((float)(v >> 24), (float)((v >> 16) & 255) * k, (float)((v >> 8) & 255) * k,
                           (float)(v & 255) * k);
  }
  return img;
}

void image_set_clip(Image *img, int x0, int y0, int x1, int y1) {
  img->clip_x0 = x0 < 0 ? 0 : x0;
  img->clip_y0 = y0 < 0 ? 0 : y0;
  img->clip_x1 = x1 > img->w ? img->w : x1;
  img->clip_y1 = y1 > img->h ? img->h : y1;
}

void image_reset_clip(Image *img) {
  img->clip_x0 = 0;
  img->clip_y0 = 0;
  img->clip_x1 = img->w;
  img->clip_y1 = img->h;
}

void image_fill(Image *img, uint32_t argb) {
  size_t n = (size_t)img->w * (size_t)img->h;
  for (size_t i = 0; i < n; i++) img->px[i] = argb;
}

// ------------------------------------------------------------------ rects

// Coverage of pixel [p, p+1) by the span [a, b).
static inline float span_cover(int p, float a, float b) {
  float lo = fmaxf((float)p, a);
  float hi = fminf((float)(p + 1), b);
  return hi > lo ? hi - lo : 0.0f;
}

void gfx_fill_rect(Image *dst, float x, float y, float w, float h, Color c) {
  if (w < 0) {
    x += w;
    w = -w;
  }
  if (h < 0) {
    y += h;
    h = -h;
  }
  if (w <= 0 || h <= 0 || c.a <= 0) return;
  float x1 = x + w, y1 = y + h;
  int ix0 = (int)floorf(x), iy0 = (int)floorf(y);
  int ix1 = (int)ceilf(x1), iy1 = (int)ceilf(y1);
  if (ix0 < dst->clip_x0) ix0 = dst->clip_x0;
  if (iy0 < dst->clip_y0) iy0 = dst->clip_y0;
  if (ix1 > dst->clip_x1) ix1 = dst->clip_x1;
  if (iy1 > dst->clip_y1) iy1 = dst->clip_y1;
  for (int py = iy0; py < iy1; py++) {
    float cy = span_cover(py, y, y1);
    if (cy <= 0) continue;
    uint32_t *row = dst->px + (size_t)py * dst->w;
    for (int px = ix0; px < ix1; px++) {
      float cov = cy * span_cover(px, x, x1);
      if (cov > 0) px_blend(row + px, c.r, c.g, c.b, c.a * cov);
    }
  }
}

void gfx_clear_rect(Image *dst, float x, float y, float w, float h) {
  if (w <= 0 || h <= 0) return;
  float x1 = x + w, y1 = y + h;
  int ix0 = (int)floorf(x), iy0 = (int)floorf(y);
  int ix1 = (int)ceilf(x1), iy1 = (int)ceilf(y1);
  if (ix0 < dst->clip_x0) ix0 = dst->clip_x0;
  if (iy0 < dst->clip_y0) iy0 = dst->clip_y0;
  if (ix1 > dst->clip_x1) ix1 = dst->clip_x1;
  if (iy1 > dst->clip_y1) iy1 = dst->clip_y1;
  for (int py = iy0; py < iy1; py++) {
    float cy = span_cover(py, y, y1);
    uint32_t *row = dst->px + (size_t)py * dst->w;
    for (int px = ix0; px < ix1; px++) {
      float keep = 1.0f - cy * span_cover(px, x, x1);
      if (keep >= 1.0f) continue;
      uint32_t v = row[px];
      row[px] = keep <= 0.0f ? 0
                             : pack_argb((float)(v >> 24) * keep, (float)((v >> 16) & 255) * keep,
                                         (float)((v >> 8) & 255) * keep, (float)(v & 255) * keep);
    }
  }
}

// ------------------------------------------------------------------ shapes

#define SS 5 // supersampling grid per pixel axis for anti-aliased shapes

typedef bool (*InsideFn)(const void *shape, float x, float y);

static void fill_shape(Image *dst, float bx0, float by0, float bx1, float by1, InsideFn inside, const void *shape,
                       Color c) {
  if (c.a <= 0) return;
  int ix0 = (int)floorf(bx0), iy0 = (int)floorf(by0);
  int ix1 = (int)ceilf(bx1), iy1 = (int)ceilf(by1);
  if (ix0 < dst->clip_x0) ix0 = dst->clip_x0;
  if (iy0 < dst->clip_y0) iy0 = dst->clip_y0;
  if (ix1 > dst->clip_x1) ix1 = dst->clip_x1;
  if (iy1 > dst->clip_y1) iy1 = dst->clip_y1;
  for (int py = iy0; py < iy1; py++) {
    for (int px = ix0; px < ix1; px++) {
      int hits = 0;
      for (int sy = 0; sy < SS; sy++) {
        float fy = (float)py + ((float)sy + 0.5f) / SS;
        for (int sx = 0; sx < SS; sx++) {
          float fx = (float)px + ((float)sx + 0.5f) / SS;
          if (inside(shape, fx, fy)) hits++;
        }
      }
      if (hits) px_blend(dst->px + (size_t)py * dst->w + px, c.r, c.g, c.b, c.a * (float)hits / (SS * SS));
    }
  }
}

typedef struct {
  const float *xy;
  int n;
} Poly;

static bool inside_poly(const void *shape, float x, float y) {
  const Poly *p = shape;
  bool in = false;
  for (int i = 0, j = p->n - 1; i < p->n; j = i++) {
    float xi = p->xy[i * 2], yi = p->xy[i * 2 + 1];
    float xj = p->xy[j * 2], yj = p->xy[j * 2 + 1];
    if ((yi > y) != (yj > y) && x < (xj - xi) * (y - yi) / (yj - yi) + xi) in = !in;
  }
  return in;
}

static void poly_bounds(const float *xy, int n, float pad, float *x0, float *y0, float *x1, float *y1) {
  *x0 = *x1 = xy[0];
  *y0 = *y1 = xy[1];
  for (int i = 1; i < n; i++) {
    *x0 = fminf(*x0, xy[i * 2]);
    *x1 = fmaxf(*x1, xy[i * 2]);
    *y0 = fminf(*y0, xy[i * 2 + 1]);
    *y1 = fmaxf(*y1, xy[i * 2 + 1]);
  }
  *x0 -= pad;
  *y0 -= pad;
  *x1 += pad;
  *y1 += pad;
}

void gfx_fill_poly(Image *dst, const float *xy, int n, Color c) {
  if (n < 3) return;
  Poly p = {xy, n};
  float x0, y0, x1, y1;
  poly_bounds(xy, n, 0, &x0, &y0, &x1, &y1);
  fill_shape(dst, x0, y0, x1, y1, inside_poly, &p, c);
}

// A set of line segments of one width; `ext` extends every segment past
// both ends (half the width closes the joints of an outline).
typedef struct {
  const float *xy;
  int n;
  bool closed;
  float half, ext;
} Strokes;

static bool inside_strokes(const void *shape, float x, float y) {
  const Strokes *s = shape;
  int segs = s->closed ? s->n : s->n - 1;
  for (int i = 0; i < segs; i++) {
    int j = (i + 1) % s->n;
    float ax = s->xy[i * 2], ay = s->xy[i * 2 + 1];
    float dx = s->xy[j * 2] - ax, dy = s->xy[j * 2 + 1] - ay;
    float len = sqrtf(dx * dx + dy * dy);
    if (len < 1e-6f) continue;
    dx /= len;
    dy /= len;
    float t = (x - ax) * dx + (y - ay) * dy;
    if (t < -s->ext || t > len + s->ext) continue;
    float nrm = (x - ax) * -dy + (y - ay) * dx;
    if (fabsf(nrm) <= s->half) return true;
  }
  return false;
}

void gfx_stroke_poly(Image *dst, const float *xy, int n, float width, Color c) {
  if (n < 2) return;
  Strokes s = {xy, n, true, width / 2, width / 2};
  float x0, y0, x1, y1;
  poly_bounds(xy, n, width, &x0, &y0, &x1, &y1);
  fill_shape(dst, x0, y0, x1, y1, inside_strokes, &s, c);
}

void gfx_line(Image *dst, float x0, float y0, float x1, float y1, float width, Color c) {
  float xy[4] = {x0, y0, x1, y1};
  Strokes s = {xy, 2, false, width / 2, 0};
  float bx0, by0, bx1, by1;
  poly_bounds(xy, 2, width, &bx0, &by0, &bx1, &by1);
  fill_shape(dst, bx0, by0, bx1, by1, inside_strokes, &s, c);
}

// ------------------------------------------------------------------ gradient

static Color gradient_at(const GradientStop *stops, int n, float t) {
  if (t <= stops[0].pos) return stops[0].color;
  for (int i = 1; i < n; i++) {
    if (t <= stops[i].pos) {
      const GradientStop *a = &stops[i - 1], *b = &stops[i];
      float span = b->pos - a->pos;
      float k = span > 0 ? (t - a->pos) / span : 1.0f;
      Color c = {a->color.r + (b->color.r - a->color.r) * k, a->color.g + (b->color.g - a->color.g) * k,
                 a->color.b + (b->color.b - a->color.b) * k, a->color.a + (b->color.a - a->color.a) * k};
      return c;
    }
  }
  return stops[n - 1].color;
}

void gfx_radial(Image *dst, float x, float y, float w, float h, float cx, float cy, float r0, float r1,
                const GradientStop *stops, int n, BlendMode mode) {
  if (w <= 0 || h <= 0 || n < 1) return;
  float x1 = x + w, y1 = y + h;
  int ix0 = (int)floorf(x), iy0 = (int)floorf(y);
  int ix1 = (int)ceilf(x1), iy1 = (int)ceilf(y1);
  if (ix0 < dst->clip_x0) ix0 = dst->clip_x0;
  if (iy0 < dst->clip_y0) iy0 = dst->clip_y0;
  if (ix1 > dst->clip_x1) ix1 = dst->clip_x1;
  if (iy1 > dst->clip_y1) iy1 = dst->clip_y1;
  float span = r1 - r0;
  for (int py = iy0; py < iy1; py++) {
    float cov_y = span_cover(py, y, y1);
    float dy = (float)py + 0.5f - cy;
    uint32_t *row = dst->px + (size_t)py * dst->w;
    for (int px = ix0; px < ix1; px++) {
      float cov = cov_y * span_cover(px, x, x1);
      if (cov <= 0) continue;
      float dx = (float)px + 0.5f - cx;
      float d = sqrtf(dx * dx + dy * dy);
      float t = span > 0 ? (d - r0) / span : 1.0f;
      if (t < 0) t = 0;
      if (t > 1) t = 1;
      Color c = gradient_at(stops, n, t);
      if (mode == BLEND_SCREEN)
        px_screen(row + px, c.r, c.g, c.b, c.a * cov);
      else
        px_blend(row + px, c.r, c.g, c.b, c.a * cov);
    }
  }
}

// ------------------------------------------------------------------ blits

void gfx_blit(Image *dst, const Image *src, float sx, float sy, float sw, float sh, float dx, float dy, float dw,
              float dh, float alpha) {
  if (dw <= 0 || dh <= 0 || sw <= 0 || sh <= 0 || alpha <= 0) return;
  int ix0 = (int)ceilf(dx - 0.5f), iy0 = (int)ceilf(dy - 0.5f);
  int ix1 = (int)ceilf(dx + dw - 0.5f), iy1 = (int)ceilf(dy + dh - 0.5f);
  if (ix0 < dst->clip_x0) ix0 = dst->clip_x0;
  if (iy0 < dst->clip_y0) iy0 = dst->clip_y0;
  if (ix1 > dst->clip_x1) ix1 = dst->clip_x1;
  if (iy1 > dst->clip_y1) iy1 = dst->clip_y1;
  if (ix0 >= ix1 || iy0 >= iy1) return;
  int sx_lo = (int)floorf(sx), sx_hi = (int)ceilf(sx + sw) - 1;
  int sy_lo = (int)floorf(sy), sy_hi = (int)ceilf(sy + sh) - 1;
  if (sx_lo < 0) sx_lo = 0;
  if (sy_lo < 0) sy_lo = 0;
  if (sx_hi > src->w - 1) sx_hi = src->w - 1;
  if (sy_hi > src->h - 1) sy_hi = src->h - 1;
  float kx = sw / dw, ky = sh / dh;
  for (int py = iy0; py < iy1; py++) {
    int ty = (int)floorf(sy + ((float)py + 0.5f - dy) * ky);
    if (ty < sy_lo) ty = sy_lo;
    if (ty > sy_hi) ty = sy_hi;
    const uint32_t *srow = src->px + (size_t)ty * src->w;
    uint32_t *drow = dst->px + (size_t)py * dst->w;
    for (int px = ix0; px < ix1; px++) {
      int tx = (int)floorf(sx + ((float)px + 0.5f - dx) * kx);
      if (tx < sx_lo) tx = sx_lo;
      if (tx > sx_hi) tx = sx_hi;
      px_over(drow + px, srow[tx], alpha);
    }
  }
}

void gfx_blit_rotated(Image *dst, const Image *src, float cx, float cy, float angle) {
  float c = cosf(angle), s = sinf(angle);
  float hw = src->w / 2.0f, hh = src->h / 2.0f;
  float reach = sqrtf(hw * hw + hh * hh) + 1;
  int x0 = (int)floorf(cx - reach), x1 = (int)ceilf(cx + reach);
  int y0 = (int)floorf(cy - reach), y1 = (int)ceilf(cy + reach);
  if (x0 < dst->clip_x0) x0 = dst->clip_x0;
  if (y0 < dst->clip_y0) y0 = dst->clip_y0;
  if (x1 > dst->clip_x1) x1 = dst->clip_x1;
  if (y1 > dst->clip_y1) y1 = dst->clip_y1;
  for (int py = y0; py < y1; py++) {
    for (int px = x0; px < x1; px++) {
      float rx = (float)px + 0.5f - cx, ry = (float)py + 0.5f - cy;
      // inverse rotation back into source space
      float ux = rx * c + ry * s + hw;
      float uy = -rx * s + ry * c + hh;
      int tx = (int)floorf(ux), ty = (int)floorf(uy);
      if (tx < 0 || ty < 0 || tx >= src->w || ty >= src->h) continue;
      px_over(dst->px + (size_t)py * dst->w + px, src->px[(size_t)ty * src->w + tx], 1.0f);
    }
  }
}
