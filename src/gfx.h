// Software raster: images, colors and anti-aliased 2D primitives. Every
// texture, sprite and screen is drawn with these into plain pixel buffers.

#ifndef FORTRESS_GFX_H
#define FORTRESS_GFX_H

#include <stdbool.h>
#include <stdint.h>

// Straight (non-premultiplied) color: r, g, b in 0..255, a in 0..1.
typedef struct Color {
  float r, g, b, a;
} Color;

// Premultiplied ARGB pixels (0xAARRGGBB). Every primitive clips against the
// image's clip rectangle, which defaults to the whole image.
typedef struct Image {
  int w, h;
  uint32_t *px;
  int clip_x0, clip_y0, clip_x1, clip_y1; // half-open [x0, x1) x [y0, y1)
  struct Image *shades[4]; // distance-darkened variants, built on demand by the renderer
} Image;

typedef enum { BLEND_OVER, BLEND_SCREEN } BlendMode;

typedef struct GradientStop {
  float pos; // 0..1
  Color color;
} GradientStop;

static inline Color rgb(float r, float g, float b) {
  Color c = {r, g, b, 1.0f};
  return c;
}

static inline Color rgba(float r, float g, float b, float a) {
  Color c = {r, g, b, a};
  return c;
}

// Parses "#rgb", "#rrggbb", "#rrggbbaa", "rgb(r,g,b)" and "rgba(r,g,b,a)".
// Unparseable input yields opaque black.
Color color_parse(const char *s);

// Each channel scaled by f, floored and capped at 255; alpha unchanged.
Color color_shade(Color c, float f);

static inline Color color_alpha(Color c, float a) {
  c.a *= a;
  return c;
}

Image *image_new(int w, int h); // fully transparent
void image_free(Image *img);
Image *image_clone(const Image *src);
Image *image_mirror(const Image *src); // flipped left to right
// RGB multiplied by floor(f * 255) / 255, alpha kept.
Image *image_darken(const Image *src, float f);
void image_set_clip(Image *img, int x0, int y0, int x1, int y1);
void image_reset_clip(Image *img);
void image_fill(Image *img, uint32_t argb); // raw fill, ignores the clip

// Source-over fill of an axis-aligned rectangle with exact fractional
// coverage on its edges.
void gfx_fill_rect(Image *dst, float x, float y, float w, float h, Color c);
// Erases to transparent, again with fractional edge coverage.
void gfx_clear_rect(Image *dst, float x, float y, float w, float h);
// Anti-aliased (supersampled) polygon fill, even-odd rule. xy holds n points.
void gfx_fill_poly(Image *dst, const float *xy, int n, Color c);
// Closed polygon outline of the given width, centered on the path.
void gfx_stroke_poly(Image *dst, const float *xy, int n, float width, Color c);
// Straight line of the given width with flat ends.
void gfx_line(Image *dst, float x0, float y0, float x1, float y1, float width, Color c);
// Fills the rectangle (x, y, w, h) with a radial gradient centered at
// (cx, cy): stop 0 at radius r0, stop 1 at radius r1, padded beyond both.
void gfx_radial(Image *dst, float x, float y, float w, float h, float cx, float cy, float r0, float r1,
                const GradientStop *stops, int n, BlendMode mode);
// Nearest-neighbour scaled copy of the source rectangle (sx, sy, sw, sh)
// onto the destination rectangle (dx, dy, dw, dh), source-over, with an
// extra opacity multiplier.
void gfx_blit(Image *dst, const Image *src, float sx, float sy, float sw, float sh, float dx, float dy, float dw,
              float dh, float alpha);
// Whole source drawn centered on (cx, cy) and rotated clockwise by angle
// radians, nearest-neighbour.
void gfx_blit_rotated(Image *dst, const Image *src, float cx, float cy, float angle);

// ------------------------------------------------------------------ pixels

static inline uint32_t pack_argb(float a, float r, float g, float b) {
  return ((uint32_t)(a + 0.5f) << 24) | ((uint32_t)(r + 0.5f) << 16) | ((uint32_t)(g + 0.5f) << 8) |
         (uint32_t)(b + 0.5f);
}

// Straight color with coverage-scaled alpha a (0..1) over a pixel.
static inline void px_blend(uint32_t *d, float r, float g, float b, float a) {
  if (a <= 0.0f) return;
  if (a >= 1.0f) {
    *d = pack_argb(255.0f, r, g, b);
    return;
  }
  uint32_t v = *d;
  float k = 1.0f - a;
  *d = pack_argb(255.0f * a + (float)(v >> 24) * k, r * a + (float)((v >> 16) & 255) * k,
                 g * a + (float)((v >> 8) & 255) * k, b * a + (float)(v & 255) * k);
}

// Premultiplied source pixel over a pixel, scaled by opacity.
static inline void px_over(uint32_t *d, uint32_t s, float opacity) {
  uint32_t sa = s >> 24;
  if (sa == 0) return;
  if (sa == 255 && opacity >= 1.0f) {
    *d = s;
    return;
  }
  float o = opacity;
  float a = (float)sa * o;
  float k = 1.0f - a / 255.0f;
  uint32_t v = *d;
  *d = pack_argb(a + (float)(v >> 24) * k, (float)((s >> 16) & 255) * o + (float)((v >> 16) & 255) * k,
                 (float)((s >> 8) & 255) * o + (float)((v >> 8) & 255) * k,
                 (float)(s & 255) * o + (float)(v & 255) * k);
}

// Screen blend of a straight color at alpha a onto an opaque pixel.
static inline void px_screen(uint32_t *d, float r, float g, float b, float a) {
  if (a <= 0.0f) return;
  uint32_t v = *d;
  float dr = (float)((v >> 16) & 255), dg = (float)((v >> 8) & 255), db = (float)(v & 255);
  dr += r * a * (1.0f - dr / 255.0f);
  dg += g * a * (1.0f - dg / 255.0f);
  db += b * a * (1.0f - db / 255.0f);
  *d = pack_argb((float)(v >> 24), dr, dg, db);
}

#endif
