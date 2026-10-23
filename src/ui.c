#include "ui.h"
#include "common.h"

void ui_diamond(Image *im, float x, float y, float radius, Color color) {
  float points[] = {x, y - radius, x + radius, y, x, y + radius, x - radius, y};
  gfx_fill_poly(im, points, 4, color);
}

void ui_rule(Image *im, float x, float y, float width) {
  for (int i = 0; i < (int)width; i++) {
    float a = .55f * (1 - fabsf(2 * i / width - 1));
    gfx_fill_rect(im, x + i, y, 1, 1, rgba(181, 151, 101, a));
  }
  ui_diamond(im, x + width / 2, y + .5f, 2.5f, rgb(185, 154, 101));
}

void ui_panel(Image *im, float x, float y, float width, float height) {
  gfx_fill_rect(im, x + 3, y + 5, width, height, rgba(0, 0, 0, .45f));
  for (int j = 0; j < (int)height; j++) {
    float t = j / height;
    gfx_fill_rect(im, x, y + j, width, 1, rgba(18 - t * 8, 24 - t * 10, 29 - t * 12, .96f));
  }
  Color edge = rgba(163, 140, 99, .35f), light = rgb(176, 147, 99);
  gfx_fill_rect(im, x, y, width, 1, edge);
  gfx_fill_rect(im, x, y + height - 1, width, 1, edge);
  gfx_fill_rect(im, x, y, 1, height, edge);
  gfx_fill_rect(im, x + width - 1, y, 1, height, edge);
  for (int i = 0; i < 4; i++) {
    float xx = x + (i % 2 ? width - 1 : 0), yy = y + (i / 2 ? height - 1 : 0);
    float dx = i % 2 ? -1 : 1, dy = i / 2 ? -1 : 1;
    gfx_line(im, xx, yy, xx + dx * 9, yy, 1, light);
    gfx_line(im, xx, yy, xx, yy + dy * 9, 1, light);
  }
}

void ui_emblem(Image *im, float x, float y, float size) {
  Color gold = rgb(195, 165, 112);
  float s = size / 32;
  float shield[] = {x - 12 * s, y - 13 * s, x + 12 * s, y - 13 * s, x + 11 * s, y + 3 * s,  x + 6 * s,
                    y + 10 * s, x,          y + 15 * s, x - 6 * s,  y + 10 * s, x - 11 * s, y + 3 * s};
  gfx_fill_poly(im, shield, 7, rgb(18, 25, 31));
  gfx_stroke_poly(im, shield, 7, s, gold);
  gfx_line(im, x, y - 8 * s, x, y + 8 * s, 1.5f * s, gold);
  gfx_line(im, x - 6 * s, y - 2 * s, x + 6 * s, y - 2 * s, 1.5f * s, gold);
  ui_diamond(im, x, y - 2 * s, 3 * s, gold);
}
