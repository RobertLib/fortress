// Cinematic painted panels use the same art direction as the playable world.
#include "comic.h"
#include "common.h"
#include "font.h"
#include "ui.h"
#include <stdio.h>
#include <string.h>
#define ART_W 640
#define ART_H 360

static const struct {
  const char *title, *line[2];
  float duration;
} PANELS[COMIC_KIND_COUNT][COMIC_PANELS] = {
    {{"BETRAYAL", {"You opened the lord's grain stores to a starving village.", "Your reward was a cell. Tonight, the village needs you again."}, 6.5f},
     {"A WEAK LINK",
      {"Tonight, a rusted link gives way. You slip your chains.",
       "Below the stones, something answers the sound of iron."},
      7},
     {"THE WAY OUT",
      {"Village, abbey, gardens, graves: the whole estate is a prison.", "Open five gates. Three keys to each. White candles bar the Jailer."},
      8}},
    {{"THE LAST GATE",
      {"The last three keys turn. Iron groans against stone.", "The final gate can no longer hold you."},
      6.5f},
     {"INTO THE NIGHT",
      {"The chains fall silent as you carry the keys across the bridge.", "Behind you, five open gates release the keeper from his oath."},
      7},
     {"FREE",
      {"The abbey bell sounds once. The village doors open.",
       "The road is yours. The people you helped have a future again."},
      8}},
};

bool comic_art_build(ComicArt *art) {
  memset(art, 0, sizeof *art);
  art->cover = image_load_art("title.bmp");
  Image *atlas = image_load_art("story.bmp");
  if (!art->cover || art->cover->w != 640 || art->cover->h != 400 || !atlas || atlas->w != 1280 || atlas->h != 1080) {
    image_free(atlas);
    return false;
  }
  for (int k = 0; k < COMIC_KIND_COUNT; k++)
    for (int p = 0; p < COMIC_PANELS; p++) {
      int i = k * COMIC_PANELS + p;
      art->scene[k][p] = image_new(ART_W, ART_H);
      gfx_blit(art->scene[k][p], atlas, (i % 2) * ART_W, (i / 2) * ART_H, ART_W, ART_H, 0, 0, ART_W, ART_H, 1);
    }
  image_free(atlas);
  art->scratch = image_new(ART_W, ART_H);
  return true;
}
void comic_art_free(ComicArt *art) {
  for (int k = 0; k < COMIC_KIND_COUNT; k++)
    for (int p = 0; p < COMIC_PANELS; p++)
      image_free(art->scene[k][p]);
  image_free(art->cover);
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
    for (int i = 0; i <= comic->panel; i++)
      comic->age[i] += step;
    dt -= step;
    if (step >= remaining) {
      if (comic_next(comic)) return true;
    } else
      break;
  }
  return false;
}

static void cinematic_image(Image *fb, const Image *source, float time, float y, float height) {
  if (!source) return;
  float zoom = 1.025f + .012f * sinf(time * .14f), sw = source->w / zoom, sh = source->h / zoom;
  float x = (source->w - sw) * (.5f + sinf(time * .12f) * .28f);
  gfx_blit(fb, source, x, (source->h - sh) * .5f, sw, sh, 0, y, fb->w, height, 1);
}
static void motes(Image *fb, float time, bool rain) {
  for (int i = 0; i < (rain ? 44 : 26); i++) {
    float x = fmodf(i * 91.7f + time * (rain ? 28 : 2.4f), 640);
    float y = fmodf(i * 47.3f + time * (rain ? 115 : -4.0f) + 10000, 340);
    if (rain)
      gfx_line(fb, x, y, x - 2, y + 8, .5f, rgba(170, 190, 196, .18f));
    else
      gfx_fill_rect(fb, x, y, 1, 1, rgba(222, 201, 155, .14f + .1f * sinf(time + i)));
  }
}
void comic_draw_cover(const ComicArt *art, Image *fb, float time) {
  image_fill(fb, 0xff080f16u);
  if (!art->cover) return;
  cinematic_image(fb, art->cover, time, 0, fb->h);
  for (int x = 0; x < fb->w; x++)
    gfx_fill_rect(fb, x, 0, 1, fb->h, rgba(3, 8, 13, .63f * maxf(0, 1 - x / (fb->w * .62f))));
  for (int y = 0; y < 65; y++)
    gfx_fill_rect(fb, 0, fb->h - 65 + y, fb->w, 1, rgba(3, 7, 11, y / 65.0f * .7f));
  motes(fb, time, false);
}
void comic_draw(ComicArt *art, Image *fb, const Comic *comic) {
  image_reset_clip(fb);
  float time = comic->age[comic->panel];
  image_fill(fb, 0xff080f16u);
  cinematic_image(fb, art->scene[comic->kind][comic->panel], time, 0, 350);
  image_set_clip(fb, 0, 0, fb->w, 350);
  motes(fb, time, comic->panel == 0);
  image_reset_clip(fb);
  for (int y = 0; y < 70; y++)
    gfx_fill_rect(fb, 0, y, fb->w, 1, rgba(3, 8, 13, .8f * (1 - y / 70.0f)));
  for (int y = 0; y < 90; y++)
    gfx_fill_rect(fb, 0, 260 + y, fb->w, 1, rgba(3, 8, 13, y / 90.0f * .96f));
  font_draw(fb, comic->kind == COMIC_INTRO ? "PROLOGUE / THE LONG NIGHT" : "EPILOGUE / BEYOND THE WALLS", 24, 25, 10,
            FONT_BOLD, ALIGN_LEFT, rgb(196, 173, 128));
  char page[32];
  snprintf(page, sizeof page, "%02d / %02d", comic->panel + 1, COMIC_PANELS);
  font_draw(fb, page, 616, 25, 10, FONT_BOLD, ALIGN_RIGHT, rgb(185, 195, 197));
  font_draw(fb, PANELS[comic->kind][comic->panel].title, 24, 284, 22, FONT_BOLD, ALIGN_LEFT, rgb(237, 224, 199));
  for (int i = 0; i < 2; i++)
    font_draw(fb, PANELS[comic->kind][comic->panel].line[i], 24, 309 + i * 18, 12, FONT_REGULAR, ALIGN_LEFT,
              rgb(203, 211, 209));
  for (int i = 0; i < COMIC_PANELS; i++) {
    float x = 24 + i * 200;
    gfx_fill_rect(fb, x, 348, 192, 2, rgb(39, 53, 62));
    float p = i < comic->panel ? 1 : i == comic->panel ? clampf(time / PANELS[comic->kind][i].duration, 0, 1) : 0;
    gfx_fill_rect(fb, x, 348, 192 * p, 2, rgb(188, 158, 106));
  }
  gfx_fill_rect(fb, 0, 363, fb->w, 1, rgba(169, 151, 116, .25f));
}
