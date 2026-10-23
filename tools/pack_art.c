// Offline atlas preparation. Requires SDL 3.4 for PNG input; the game itself
// uses SDL 3.2's BMP loader. All resizing happens in premultiplied
// coverage space and is unpremultiplied before writing BMP with real alpha.
#include <SDL3/SDL.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CELL 256
static uint32_t sample(SDL_Surface *s, float x, float y) {
  x = fmaxf(0, fminf(s->w - 1, x));
  y = fmaxf(0, fminf(s->h - 1, y));
  int ix = (int)x, iy = (int)y, xx = ix + 1 < s->w ? ix + 1 : ix, yy = iy + 1 < s->h ? iy + 1 : iy;
  float fx = x - ix, fy = y - iy, a = 0, c[3] = {0};
  uint32_t q[4] = {*(uint32_t *)((uint8_t *)s->pixels + iy * s->pitch + ix * 4),
                   *(uint32_t *)((uint8_t *)s->pixels + iy * s->pitch + xx * 4),
                   *(uint32_t *)((uint8_t *)s->pixels + yy * s->pitch + ix * 4),
                   *(uint32_t *)((uint8_t *)s->pixels + yy * s->pitch + xx * 4)};
  float weights[4] = {(1 - fx) * (1 - fy), fx * (1 - fy), (1 - fx) * fy, fx * fy};
  for (int i = 0; i < 4; i++) {
    float k = (q[i] >> 24) * weights[i];
    a += k;
    for (int j = 0; j < 3; j++)
      c[j] += ((q[i] >> (16 - j * 8)) & 255) * k;
  }
  if (a < .5f) return 0;
  return ((uint32_t)(a + .5f) << 24) | ((uint32_t)(c[0] / a + .5f) << 16) | ((uint32_t)(c[1] / a + .5f) << 8) |
         (uint32_t)(c[2] / a + .5f);
}
int main(int argc, char **argv) {
  if (argc != 7) {
    fprintf(stderr, "usage: pack_art input.png output.bmp columns rows "
                    "actor|props|animals|items|weapons|fixtures|story output.png\n");
    return 1;
  }
  SDL_Init(0);
  SDL_Surface *input = SDL_LoadPNG(argv[1]);
  if (!input) {
    fprintf(stderr, "%s\n", SDL_GetError());
    return 2;
  }
  SDL_Surface *src = SDL_ConvertSurface(input, SDL_PIXELFORMAT_ARGB8888);
  SDL_DestroySurface(input);
  int cols = atoi(argv[3]), rows = atoi(argv[4]);
  bool story = !strcmp(argv[5], "story"), material = !strcmp(argv[5], "material"),
       effects = !strcmp(argv[5], "effects");
  bool title = !strcmp(argv[5], "title");
  bool opaque = story || material || title;
  int row_edge[32] = {0};
  row_edge[rows] = src->h;
  // Generated sheets have visually aligned rows with occasional unequal
  // gutters. Find the empty horizontal gutters before any cell is cropped.
  for (int k = 1; k < rows; k++) {
    int expected = k * src->h / rows, radius = src->h / (rows * 4), best = expected;
    float score_best = 1e30f;
    for (int y = expected - radius; y <= expected + radius; y++) {
      if (y < 2 || y >= src->h - 2) continue;
      int coverage = 0;
      for (int yy = y - 1; yy <= y + 1; yy++)
        for (int x = 0; x < src->w; x++)
          if ((*(uint32_t *)((uint8_t *)src->pixels + yy * src->pitch + x * 4) >> 24) > 32) coverage++;
      float score = coverage * 1000.0f + abs(y - expected);
      if (score < score_best) {
        score_best = score;
        best = y;
      }
    }
    if (opaque) {
      float strongest = -1;
      for (int y = expected - radius; y <= expected + radius; y++) {
        if (y < 1 || y >= src->h) continue;
        float difference = 0;
        for (int x = 0; x < src->w; x++) {
          uint32_t p = *(uint32_t *)((uint8_t *)src->pixels + y * src->pitch + x * 4);
          uint32_t q = *(uint32_t *)((uint8_t *)src->pixels + (y - 1) * src->pitch + x * 4);
          for (int c = 0; c < 3; c++)
            difference += abs((int)((p >> (c * 8)) & 255) - (int)((q >> (c * 8)) & 255));
        }
        if (difference > strongest) {
          strongest = difference;
          best = y;
        }
      }
    }
    row_edge[k] = best;
  }
  int cw = story || title ? 640 : CELL, ch = story ? 360 : title ? 400 : CELL;
  SDL_Surface *out = SDL_CreateSurface(cols * cw, rows * ch, SDL_PIXELFORMAT_ARGB8888);
  SDL_ClearSurface(out, 0, 0, 0, 0);
  for (int row = 0; row < rows; row++) {
    int col_edge[32] = {0};
    col_edge[cols] = src->w;
    for (int k = 1; k < cols; k++) {
      int expected = k * src->w / cols, radius = src->w / (cols * 3), best = expected;
      float score_best = 1e30f;
      for (int x = expected - radius; x <= expected + radius; x++) {
        if (x < 2 || x >= src->w - 2) continue;
        int coverage = 0;
        for (int xx = x - 1; xx <= x + 1; xx++)
          for (int y = row_edge[row]; y < row_edge[row + 1]; y++)
            if ((*(uint32_t *)((uint8_t *)src->pixels + y * src->pitch + xx * 4) >> 24) > 32) coverage++;
        float score = coverage * 1000.0f + abs(x - expected);
        if (score < score_best) {
          score_best = score;
          best = x;
        }
      }
      if (material) {
        float strongest = -1;
        for (int x = expected - radius; x <= expected + radius; x++) {
          if (x < 1 || x >= src->w) continue;
          float difference = 0;
          for (int y = row_edge[row]; y < row_edge[row + 1]; y++) {
            uint32_t p = *(uint32_t *)((uint8_t *)src->pixels + y * src->pitch + x * 4);
            uint32_t q = *(uint32_t *)((uint8_t *)src->pixels + y * src->pitch + (x - 1) * 4);
            for (int c = 0; c < 3; c++)
              difference += abs((int)((p >> (c * 8)) & 255) - (int)((q >> (c * 8)) & 255));
          }
          if (difference > strongest) {
            strongest = difference;
            best = x;
          }
        }
      }
      col_edge[k] = story ? expected : best;
    }
    for (int col = 0; col < cols; col++) {
      int x0 = col_edge[col], x1 = col_edge[col + 1], y0 = row_edge[row], y1 = row_edge[row + 1];
      if (!opaque && !effects)
        for (int side = 0; side < 2; side++) {
          int boundary = row + side;
          if (boundary == 0 || boundary == rows) continue;
          int expected = row_edge[boundary], radius = src->h / (rows * 4), best = expected;
          float score_best = 1e30f;
          for (int yy = expected - radius; yy <= expected + radius; yy++) {
            if (yy < 1 || yy >= src->h - 1) continue;
            int coverage = 0;
            for (int x = x0; x < x1; x++)
              if ((*(uint32_t *)((uint8_t *)src->pixels + yy * src->pitch + x * 4) >> 24) > 32) coverage++;
            float score = coverage * 1000.0f + abs(yy - expected);
            if (score < score_best) {
              score_best = score;
              best = yy;
            }
          }
          if (side)
            y1 = best;
          else
            y0 = best;
        }
      int left = x1, right = x0, top = y1, bottom = y0;
      for (int y = y0; y < y1; y++)
        for (int x = x0; x < x1; x++) {
          uint32_t p = *(uint32_t *)((uint8_t *)src->pixels + y * src->pitch + x * 4);
          if ((p >> 24) > 32) {
            if (x < left) left = x;
            if (x > right) right = x;
            if (y < top) top = y;
            if (y > bottom) bottom = y;
          }
        }
      if (opaque) {
        left = x0 + 2;
        right = x1 - 3;
        top = y0 + 2;
        bottom = y1 - 3;
        float ratio = cw / (float)ch;
        if ((right - left + 1) / (float)(bottom - top + 1) > ratio) {
          int width = (int)((bottom - top + 1) * ratio);
          left += (right - left + 1 - width) / 2;
          right = left + width - 1;
        } else {
          int height = (int)((right - left + 1) / ratio);
          top += (bottom - top + 1 - height) / 2;
          bottom = top + height - 1;
        }
      }
      if (right < left || bottom < top) {
        fprintf(stderr, "empty cell %d,%d\n", col, row);
        return 3;
      }
      // Include soft antialiased edges without collecting a neighboring cell.
      left = left - 2 > x0 ? left - 2 : x0;
      right = right + 2 < x1 ? right + 2 : x1 - 1;
      top = top - 2 > y0 ? top - 2 : y0;
      bottom = bottom + 2 < y1 ? bottom + 2 : y1 - 1;
      float target_w = 234, target_h = 238, base = 250;
      if (!strcmp(argv[5], "actor")) {
        int i = row * cols + col;
        if (i == 6) target_h = 176;
        if (i == 7) target_h = 112;
        if (i == 8) target_h = 74;
      } else if (!strcmp(argv[5], "combat")) {
        if (row < 3) {
          float heights[3][4] = {{135, 90, 105, 140}, {90, 65, 75, 95}, {65, 45, 55, 65}};
          target_h = heights[row][col];
        }
      } else if (!strcmp(argv[5], "rat_death")) {
        target_h = col == 0 ? 90 : col == 1 ? 65 : 45;
      } else if (!strcmp(argv[5], "animals")) {
        target_h = row == 0 ? 174 : row == 1 ? 100 : row == 2 ? 114 : 164;
      } else if (!strcmp(argv[5], "items")) {
        target_w = 204;
        target_h = 216;
        base = 244;
      } else if (!strcmp(argv[5], "weapons")) {
        target_w = 248;
        target_h = 242;
        base = 256;
      } else if (!strcmp(argv[5], "hound_walk"))
        target_h = 174;
      else if (!strcmp(argv[5], "rat_walk"))
        target_h = 100;
      else if (!strcmp(argv[5], "spider_walk"))
        target_h = 114;
      else if (effects) {
        target_w = 240;
        target_h = 240;
        base = 248;
      } else if (!strcmp(argv[5], "fixtures")) {
        target_w = 224;
        target_h = 244;
        base = 250;
      }
      float scale = fminf(target_w / (right - left + 1), target_h / (bottom - top + 1));
      float dw = (right - left + 1) * scale, dh = (bottom - top + 1) * scale;
      int ox = (int)((cw - dw) / 2), oy = (int)(base - dh);
      if (opaque) {
        dw = cw;
        dh = ch;
        ox = oy = 0;
      }
      for (int y = 0; y < (int)dh; y++)
        for (int x = 0; x < (int)dw; x++) {
          uint32_t p = sample(src, left + (x + .5f) * (right - left + 1) / dw - .5f,
                              top + (y + .5f) * (bottom - top + 1) / dh - .5f);
          *(uint32_t *)((uint8_t *)out->pixels + (row * ch + oy + y) * out->pitch + (col * cw + ox + x) * 4) = p;
        }
      printf("%s cell %02d source [%d,%d %dx%d] -> [%d,%d %.0fx%.0f]\n", argv[5], row * cols + col, left, top,
             right - left + 1, bottom - top + 1, ox, oy, dw, dh);
    }
  }
  if (!SDL_SaveBMP(out, argv[2]) || !SDL_SavePNG(out, argv[6])) {
    fprintf(stderr, "%s\n", SDL_GetError());
    return 4;
  }
  SDL_Surface *read = SDL_LoadBMP(argv[2]), *check = read ? SDL_ConvertSurface(read, SDL_PIXELFORMAT_ARGB8888) : NULL;
  if (!check) return 5;
  int transparent = 0, fully_opaque = 0;
  for (int y = 0; y < out->h; y++)
    for (int x = 0; x < out->w; x++) {
      uint32_t a = *(uint32_t *)((uint8_t *)out->pixels + y * out->pitch + x * 4),
               b = *(uint32_t *)((uint8_t *)check->pixels + y * check->pitch + x * 4);
      if (a != b) {
        fprintf(stderr, "BMP alpha roundtrip mismatch\n");
        return 6;
      }
      if (!(a >> 24)) transparent++;
      if ((a >> 24) == 255) fully_opaque++;
    }
  printf("Verified %dx%d, %d transparent / %d opaque pixels\n", out->w, out->h, transparent, fully_opaque);
  SDL_DestroySurface(check);
  SDL_DestroySurface(read);
  SDL_DestroySurface(out);
  SDL_DestroySurface(src);
  SDL_Quit();
  return 0;
}
