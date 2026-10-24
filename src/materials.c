// High-resolution material family shared by every room, prop face and trap.
// All detail is attached to material space: no frame-random noise or old
// low-resolution overlays survive in the finished world art.
#include "art.h"
#include "horror.h"
#include <stdlib.h>
#include <string.h>

static float hash(int x, int y, int seed) {
  uint32_t n = (uint32_t)x * 374761393u + (uint32_t)y * 668265263u + (uint32_t)seed * 2246822519u;
  n = (n ^ (n >> 13)) * 1274126177u;
  return (n ^ (n >> 16)) / 4294967295.0f;
}
static float noise(float x, float y, int seed) {
  int ix = (int)floorf(x), iy = (int)floorf(y);
  float a = x - ix, b = y - iy;
  a = a * a * (3 - 2 * a);
  b = b * b * (3 - 2 * b);
  return (hash(ix, iy, seed) * (1 - a) + hash(ix + 1, iy, seed) * a) * (1 - b) +
         (hash(ix, iy + 1, seed) * (1 - a) + hash(ix + 1, iy + 1, seed) * a) * b;
}
static float grain(float x, float y, int seed) {
  return (noise(x * .17f, y * .17f, seed) - .5f) * 8 + (noise(x * .7f, y * .7f, seed + 1) - .5f) * 5 +
         (hash((int)x, (int)y, seed) - .5f) * 4;
}
static uint32_t color_pixel(Color c) {
  return pack_argb(255, clampf(c.r, 0, 255), clampf(c.g, 0, 255), clampf(c.b, 0, 255));
}
static Color iron(float u, float v, float edge, int seed) {
  float n = grain(u * 4, v * 4, seed), rust = maxf(0, noise(u * .6f, v * .5f, seed + 2) - .58f) * 40;
  float sheen = powf(maxf(0, 1 - fabsf(fmodf(u * .06f + v * .01f, 1) - .4f) * 3), 7) * 23;
  float base = 36 + n + sheen + maxf(0, 1 - edge * 4) * 27;
  return rgb(base + rust, base * 1.08f + rust * .4f, base * 1.17f);
}
static void rivet(Image *im, float x, float y, float radius) {
  art_oval(im, x + .13f, y + .18f, radius + .12f, radius + .12f, 0, rgba(0, 0, 0, .7f));
  art_oval(im, x, y, radius, radius, 0, rgb(78, 82, 78));
  art_oval(im, x - .12f, y - .13f, radius * .55f, radius * .45f, 0, rgb(151, 151, 132));
}
static void rim(Image *im, float x, float y, float w, float h) {
  art_rect(im, x, y, w, h, rgb(28, 33, 35));
  art_line(im, x + .2f, y + .2f, x + w - .2f, y + .2f, .32f, rgb(116, 120, 113));
  art_line(im, x, y, x, y + h, .22f, rgb(97, 103, 101));
  art_line(im, x + w, y, x + w, y + h, .35f, rgb(12, 18, 20));
}
static Image *stone_base(const Image *source, int seed, float brightness, bool moss) {
  Image *im = image_new(TEX, TEX);
  for (int y = 0; y < TEX; y++)
    for (int x = 0; x < TEX; x++) {
      int sx = (x + seed % 4 * (TEX / 4)) % TEX;
      uint32_t p = source->px[(y % source->h) * source->w + sx % source->w];
      float shade = brightness, red = ((p >> 16) & 255) * shade * .90f, green = ((p >> 8) & 255) * shade * .95f,
            blue = (p & 255) * shade;
      if (moss) {
        float k = clampf((noise(x * .045f, y * .025f, seed) - .48f) * 2.3f, 0, .55f);
        red *= 1 - k * .5f;
        green *= 1 - k * .05f;
        blue *= 1 - k * .55f;
      }
      im->px[y * TEX + x] = pack_argb(255, red, green, blue);
    }
  return im;
}
static Image *bars(bool door) {
  Image *im = image_new(TEX, TEX);
  for (int y = 0; y < TEX; y++)
    for (int x = 0; x < TEX; x++) {
      float u = (x + .5f) * 64 / TEX, v = (y + .5f) * 64 / TEX, nearest = 100, across = 0;
      for (int i = 0; i < 5; i++) {
        float d = u - (10 + i * 11) - sinf(v * .2f + i) * .055f;
        if (fabsf(d) < nearest) {
          nearest = fabsf(d);
          across = d;
        }
      }
      float radius = .65f;
      bool solid = nearest < radius && v > 1 && v < 63;
      for (int rail = 0; rail < 2; rail++) {
        float d = v - (20 + rail * 20);
        if (fabsf(d) < .85f) {
          solid = true;
          across = d;
          radius = .85f;
        }
      }
      bool frame = v < 1.7f || v > 62.3f || (door && (u < 1.7f || u > 62.3f));
      if (frame) {
        solid = true;
        across = fmodf(u < 2 || u > 62 ? u : v, 1.7f) - .85f;
        radius = 1.2f;
      }
      if (solid) {
        float curve = sqrtf(maxf(0, 1 - sq(across / radius)));
        Color c = iron(u, v, .5f, 83);
        float k = .68f + curve * .5f - maxf(0, across) * .1f;
        if (frame) k *= .72f;
        im->px[y * TEX + x] = color_pixel(color_shade(c, k));
      }
    }
  for (int i = 0; i < 5; i++) {
    rivet(im, 10 + i * 11, 20, .36f);
    rivet(im, 10 + i * 11, 40, .36f);
  }
  if (door) {
    rim(im, 47.5f, 28, 6, 8.5f);
    for (int y = 28 * TEX / 64; y < 36 * TEX / 64; y++)
      for (int x = 48 * TEX / 64; x < 53 * TEX / 64; x++) {
        float u = (x + .5f) * 64 / TEX, v = (y + .5f) * 64 / TEX;
        im->px[y * TEX + x] = color_pixel(iron(u, v, minf(minf(u - 48, 53 - u), minf(v - 28, 36 - v)), 91));
      }
    art_oval(im, 50.5f, 32.7f, 1.65f, 2.15f, .55f, rgba(0, 0, 0, .6f));
    art_oval(im, 50.5f, 31.2f, .45f, .55f, 0, rgb(8, 13, 15));
    art_rect(im, 50.3f, 31.4f, .4f, 1.4f, rgb(8, 13, 15));
    for (int k = 0; k < 48; k++) {
      float a = k * (float)M_PI / 24, b = (k + 1) * (float)M_PI / 24;
      float light = .65f + .35f * cosf(a + 2.1f);
      art_line(im, 50.5f + cosf(a) * 1.45f, 32.5f + sinf(a) * 1.9f, 50.5f + cosf(b) * 1.45f, 32.5f + sinf(b) * 1.9f,
               .44f, rgb(180 * light, 165 * light, 130 * light));
    }
    rivet(im, 48.4f, 29, .25f);
    rivet(im, 52.4f, 35.5f, .25f);
  }
  return im;
}
static Image *lever_wall(const Image *stone, const Image *atlas, bool pulled) {
  Image *im = stone_base(stone, 0, 1, false);
  Image *source = art_cell(atlas, 4, pulled ? 3 : 2);
  // Keep the painted plate and handle at half scale, centred at hand height.
  // Mask their source bounds so the surrounding masonry keeps its texel scale.
  const float scale = .5f;
  for (int y = 0; y < TEX; y++)
    for (int x = 0; x < TEX; x++) {
      float u = ((x + .5f) * 64 / TEX - 32) / scale + 32;
      float v = ((y + .5f) * 64 / TEX - 32) / scale + 32;
      float plate = minf(minf(u - 19, 45.5f - u), minf(v - 16, 52.5f - v));
      float handle = minf(minf(u - 28, 37.5f - u),
                          minf(v - (pulled ? 49 : 1), (pulled ? 61.5f : 18) - v));
      float alpha = clampf(maxf(plate, handle) / .8f, 0, 1);
      if (alpha > 0)
        px_over(&im->px[y * TEX + x], art_sample(source, u * TEX / 64 - .5f, v * TEX / 64 - .5f), alpha);
    }
  image_free(source);
  return im;
}
static Image *painted_wall(const Image *stone, const Image *atlas, WallTex kind) {
  static const int cell[WT_COUNT] = {-1, -1, 8, 6, 12, 1, 7, -1, 0, -1, 4, 2, 3, -1, -1, 9, 10, 11, 5, 5, 5, 5};
  if (kind == WT_BARS || kind == WT_BAR_DOOR) return bars(kind == WT_BAR_DOOR);
  if (kind == WT_LEVER || kind == WT_LEVER_PULLED) return lever_wall(stone, atlas, kind == WT_LEVER_PULLED);
  if (cell[kind] >= 0) {
    Image *im = art_cell(atlas, 4, cell[kind]);
    if (kind >= WT_PORTAL && kind <= WT_PORTAL_LAST) {
      Image *source = image_clone(im);
      float phase = (kind - WT_PORTAL) * (float)M_PI * .5f;
      for (int y = 0; y < TEX; y++)
        for (int x = 0; x < TEX; x++) {
          float dx = (x - TEX * .48f) / (TEX * .215f), dy = (y - TEX * .51f) / (TEX * .36f), r = hypotf(dx, dy);
          if (r >= .92f) continue;
          float weight = (1 - r * r) * 1.8f;
          im->px[y * TEX + x] =
              art_sample(source, x + sinf(phase + y * .05f) * weight, y + cosf(phase + x * .04f) * weight);
        }
      image_free(source);
    }
    return im;
  }
  Image *im = stone_base(stone, 0, kind == WT_NICHE ? .62f : kind == WT_DUNGEON_BARE ? .8f : 1, kind == WT_MOSS);
  if (kind == WT_NICHE)
    for (int y = 0; y < TEX; y++)
      for (int x = 0; x < TEX; x++) {
        float edge = minf(minf(x, TEX - 1 - x), minf(y, TEX - 1 - y)) / (float)TEX;
        uint32_t p = im->px[y * TEX + x];
        float k = .55f + .45f * minf(1, edge * 7);
        im->px[y * TEX + x] = pack_argb(255, ((p >> 16) & 255) * k, ((p >> 8) & 255) * k, (p & 255) * k);
      }
  return im;
}
static Image *surface(const Image *atlas, SurfaceTheme theme, bool ceiling) {
  int cell =
      ceiling ? (theme == SURFACE_CELLARS || theme == SURFACE_VAULT ? 14 : 15) : (theme == SURFACE_BARRACKS ? 13 : 14);
  Image *im = art_cell(atlas, 4, cell);
  if (ceiling && theme == SURFACE_BARRACKS) {
    Image *source = image_clone(im);
    for (int y = 0; y < TEX; y++)
      for (int x = 0; x < TEX; x++)
        im->px[y * TEX + x] = source->px[((x + TEX / 4) % TEX) * TEX + y];
    image_free(source);
  }
  for (int y = 0; y < TEX; y++)
    for (int x = 0; x < TEX; x++) {
      uint32_t p = im->px[y * TEX + x];
      float r = (p >> 16) & 255, g = (p >> 8) & 255, b = p & 255;
      float k = ceiling ? 1.32f : 1.20f;
      if (theme == SURFACE_CELLARS) {
        k *= .94f - noise(x * .025f, y * .025f, 44) * .13f;
        r *= .93f;
        b *= .94f;
      }
      if (theme == SURFACE_BARRACKS && ceiling) {
        r *= .91f;
        g *= .96f;
        b *= 1.05f;
      }
      if (theme == SURFACE_VAULT) {
        r *= .89f;
        b *= 1.06f;
      }
      if (theme == SURFACE_KEEP) {
        r *= 1.06f;
        b *= .93f;
      }
      im->px[y * TEX + x] = pack_argb(255, clampf(r * k, 0, 255), clampf(g * k, 0, 255), clampf(b * k, 0, 255));
    }
  return im;
}
static Image *ground_material(GroundKind kind) {
  Image *im = image_new(TEX, TEX);
  for (int y = 0; y < TEX; y++) for (int x = 0; x < TEX; x++) {
    float u = (x + .5f) / TEX, v = (y + .5f) / TEX;
    // Periodic noise: filtering and wrapping never expose seams.
    float n = 0;
    for (int octave = 0; octave < 3; octave++) {
      float f = (float)(2 << octave), px = u * f, py = v * f;
      int ix = (int)px, iy = (int)py, period = (int)f;
      float a = px - ix, b = py - iy;
      a = a * a * (3 - 2 * a); b = b * b * (3 - 2 * b);
      float aa = hash(ix % period, iy % period, 721 + kind);
      float ab = hash((ix + 1) % period, iy % period, 721 + kind);
      float ba = hash(ix % period, (iy + 1) % period, 721 + kind);
      float bb = hash((ix + 1) % period, (iy + 1) % period, 721 + kind);
      n += ((aa * (1 - a) + ab * a) * (1 - b) + (ba * (1 - a) + bb * a) * b - .5f) * (20.0f / (octave + 1));
    }
    float speck = hash(x, y, 821 + kind), r, g, b;
    if (kind == GROUND_GRASS) {
      float blade = hash(x / 2, y / 7, 715) > .68f ? 9 : 0;
      r = 49 + n + blade; g = 64 + n * 1.3f + blade; b = 36 + n * .7f;
    } else if (kind == GROUND_EARTH) {
      r = 77 + n + speck * 8; g = 62 + n * .8f; b = 45 + n * .65f;
    } else if (kind == GROUND_LEAVES) {
      float leaf = hash(x / 4, y / 6, 133);
      float litter = leaf > .62f ? 16 : leaf < .25f ? -12 : 0;
      r = 62 + n + litter; g = 57 + n * .9f + litter * .75f; b = 32 + n * .65f + litter * .35f;
    } else if (kind == GROUND_GRAVEL) {
      float pebble = hash(x / 3, y / 3, 129) * 22;
      r = 85 + n + pebble; g = 83 + n + pebble; b = 72 + n + pebble;
    } else {
      float row = floorf(v * 5), px = fmodf(u * 5 + fmodf(row, 2) * .5f, 1), py = fmodf(v * 5, 1);
      float edge = minf(minf(px, 1 - px), minf(py, 1 - py));
      float mortar = clampf(edge * 20, 0, 1), stone = hash((int)floorf(u * 5 + fmodf(row, 2) * .5f) % 5, (int)row, 92) * 18;
      r = 40 + mortar * (48 + stone) + n; g = 42 + mortar * (46 + stone) + n; b = 39 + mortar * (40 + stone) + n;
    }
    im->px[y * TEX + x] = pack_argb(255, clampf(r, 0, 255), clampf(g, 0, 255), clampf(b, 0, 255));
  }
  return im;
}
static Image *night_sky(void) {
  Image *im = image_new(1024, 256);
  for (int y = 0; y < im->h; y++) for (int x = 0; x < im->w; x++) {
    float u = x / (float)im->w, v = y / (float)im->h;
    float angle = u * (float)M_PI * 2;
    float cloud = noise(12 + cosf(angle) * 4, 17 + sinf(angle) * 4 + v * 3, 170);
    float veil = clampf((cloud - .38f) * 2, 0, .75f) * (1 - v * .45f);
    float r = 13 + v * 13 + veil * 27, g = 23 + v * 15 + veil * 30, b = 34 + v * 16 + veil * 29;
    float moon = hypotf((u - .19f) * 5.2f, (v - .30f));
    float glow = expf(-moon * moon * 320) * 24;
    r += glow; g += glow; b += glow;
    if (moon < .032f) { float craters = noise(x * .32f, y * .32f, 531) * 24; r = 193 - craters; g = 202 - craters; b = 193 - craters; }
    if (v < .65f && hash(x, y, 712) > .9991f && veil < .28f) { r += 42; g += 47; b += 48; }
    float ridge = .91f + sinf(angle * 7) * .013f + sinf(angle * 13 + 2) * .009f;
    if (v > ridge) { r = 13; g = 23; b = 27; }
    im->px[y * im->w + x] = pack_argb(255, r, g, b);
  }
  return im;
}
static void mips(Image **list, Image *base) {
  list[0] = base;
  for (int m = 1; m < SURFACE_MIPS; m++) {
    Image *p = list[m - 1], *im = list[m] = image_new(p->w / 2, p->h / 2);
    for (int y = 0; y < im->h; y++)
      for (int x = 0; x < im->w; x++) {
        float c[3] = {0};
        for (int dy = 0; dy < 2; dy++)
          for (int dx = 0; dx < 2; dx++) {
            uint32_t q = p->px[(y * 2 + dy) * p->w + x * 2 + dx];
            for (int k = 0; k < 3; k++)
              c[k] += ((q >> (16 - k * 8)) & 255) * .25f;
          }
        im->px[y * im->w + x] = pack_argb(255, c[0], c[1], c[2]);
      }
  }
}
static Image *water(int frame, bool flowing) {
  Image *im = image_new(TEX, TEX);
  float t = 2 * (float)M_PI * frame / WATER_FRAMES;
  for (int y = 0; y < TEX; y++)
    for (int x = 0; x < TEX; x++) {
      float u = x * 2 * (float)M_PI / TEX, v = y * 2 * (float)M_PI / TEX;
      float a = sinf(u * 3 + sinf(v * 2 + t) * .6f - t), b = sinf(v * (flowing ? 7 : 4) + sinf(u * 2 - t) * .7f + t),
            wave = (a + b) * .5f;
      float shine = powf(maxf(0, 1 - fabsf(a - b) * 6), 4) * maxf(0, wave),
            bed = sinf(u * 9 + v * 3) * sinf(v * 7 - u) * 2;
      im->px[y * TEX + x] = pack_argb(255, 20 + wave * 7 + shine * 64 + bed, 47 + wave * 12 + shine * 81 + bed,
                                      52 + wave * 15 + shine * 83 + bed);
    }
  return im;
}
static Image *box_face(const Image *atlas, const Image *props, BoxKind kind, int face) {
  Image *im = art_cell(atlas, 4, 13);
  if (face == 2) {
    Image *source = image_clone(im);
    for (int y = 0; y < TEX; y++)
      for (int x = 0; x < TEX; x++)
        im->px[y * TEX + x] = source->px[x * TEX + y];
    image_free(source);
  }
  if (face == 0) {
    Image *source = art_cell(props, 4, kind == BOX_WARDROBE ? 2 : 3);
    // Rectify the painted front face into a material for the actual 3D box.
    const float wardrobe[] = {90, 43, 196, 49, 196, 222, 90, 211};
    const float chest[] = {99, 155, 235, 135, 235, 218, 99, 236};
    const float *q = kind == BOX_WARDROBE ? wardrobe : chest;
    for (int y = 0; y < TEX; y++)
      for (int x = 0; x < TEX; x++) {
        float u = (x + .5f) / TEX, v = (y + .5f) / TEX;
        float sx = (q[0] * (1 - u) + q[2] * u) * (1 - v) + (q[6] * (1 - u) + q[4] * u) * v;
        float sy = (q[1] * (1 - u) + q[3] * u) * (1 - v) + (q[7] * (1 - u) + q[5] * u) * v;
        uint32_t p = art_sample(source, sx, sy), base = im->px[y * TEX + x];
        float k = 1 - (p >> 24) / 255.0f;
        im->px[y * TEX + x] = pack_argb(255, ((p >> 16) & 255) + ((base >> 16) & 255) * k,
                                        ((p >> 8) & 255) + ((base >> 8) & 255) * k, (p & 255) + (base & 255) * k);
      }
    image_free(source);
  } else {
    for (int band = 0; band < 2; band++) {
      float x = 10 + band * 42;
      rim(im, x, 0, 2.2f, 64);
      for (int y = 5; y < 64; y += 12)
        rivet(im, x + 1.1f, y, .4f);
    }
    if (kind == BOX_CHEST) art_line(im, 0, face == 2 ? 32 : 17, 64, face == 2 ? 32 : 17, .65f, rgb(19, 26, 28));
  }

  return im;
}
static Image *trace(int kind) {
  Image *im = image_new(TEX, TEX);
  Lcg rng = lcg(344 + kind);
  if (kind == TRACE_BLOOD) {
    for (int i = 0; i < 70; i++) {
      float y = (float)lcg_next(&rng) * 64, x = 32 + sinf(y * .13f) * 7 + ((float)lcg_next(&rng) - .5f) * 15;
      art_oval(im, x, y, .2f + (float)lcg_next(&rng) * 1.5f, .4f + (float)lcg_next(&rng) * 2, 0,
               rgba(83, 28, 28, .35f + (float)lcg_next(&rng) * .35f));
    }
  } else if (kind == TRACE_SCRATCH) {
    for (int i = 0; i < 4; i++) {
      art_line(im, 15 + i * 6, 8 + i, 26 + i * 6, 55 - i, .37f, rgba(188, 179, 150, .7f));
      art_line(im, 15.4f + i * 6, 8 + i, 26.4f + i * 6, 55 - i, .23f, rgba(13, 22, 25, .9f));
    }
  } else
    for (int i = 0; i < 12; i++) {
      float x = 28 + sinf(i * .5f) * 6, y = i * 5;
      art_oval(im, x, y, 1.4f, 2.5f, .42f, rgb(93, 94, 82));
      art_line(im, x - .9f, y - 1, x - .9f, y + 1, .16f, rgb(168, 163, 136));
    }
  return im;
}
void materials_build(Assets *a, const Image *limestone, const Image *atlas, const Image *props) {
  for (int i = 0; i < WT_COUNT; i++) {
    a->walls[i] = painted_wall(limestone, atlas, (WallTex)i);
    a->walls_dark[i] = image_darken(a->walls[i], .70f);
  }
  for (int theme = 0; theme < SURFACE_THEME_COUNT; theme++) {
    mips(a->floors[theme], surface(atlas, (SurfaceTheme)theme, false));
    mips(a->ceilings[theme], surface(atlas, (SurfaceTheme)theme, true));
  }
  for (int kind = 1; kind < GROUND_COUNT; kind++) mips(a->grounds[kind - 1], ground_material((GroundKind)kind));
  a->sky = night_sky();
  for (int kind = 0; kind < BOX_KIND_COUNT; kind++) {
    BoxTextures *b = &a->boxes[kind];
    b->front = box_face(atlas, props, (BoxKind)kind, 0);
    b->side = box_face(atlas, props, (BoxKind)kind, 1);
    b->lid = box_face(atlas, props, (BoxKind)kind, 2);
    b->front_dark = image_darken(b->front, .7f);
    b->side_dark = image_darken(b->side, .7f);
  }
  for (int f = 0; f < WATER_FRAMES; f++) {
    a->water[0][f] = water(f, false);
    a->water[1][f] = water(f, true);
  }
  for (int i = 0; i < 3; i++)
    a->horror_traces[i] = trace(i);
}
