#include "render.h"

#include <SDL3/SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(expr) do { if (!(expr)) { \
  fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #expr); exit(1); \
} } while (0)

static const char *ARENA =
    "11111111111111111\n"
    "1...............1\n"
    "1...............1\n"
    "1...............1\n"
    "1...............1\n"
    "1.P.............1\n"
    "1...............1\n"
    "1...............1\n"
    "1...............1\n"
    "1...............1\n"
    "11111111111111111\n";

static int differences(const Image *a, const Image *b, int y0, int y1) {
  int changed = 0;
  for (int y = y0; y < y1; y++)
    for (int x = 0; x < SCREEN_W; x++)
      if (a->px[y * SCREEN_W + x] != b->px[y * SCREEN_W + x]) changed++;
  return changed;
}

static void test_parsing(void) {
  Level lv;
  char err[128];
  CHECK(level_parse(&lv, ARENA, err, sizeof err));
  CHECK(lv.surface_theme == SURFACE_GUARDHOUSE);
  level_free(&lv);
  char text[1024];
  snprintf(text, sizeof text, "@surfaces unknown\n%s", ARENA);
  CHECK(!level_parse(&lv, text, err, sizeof err));
  CHECK(strstr(err, "Unknown surface theme"));
  level_free(&lv);
}

static void test_rendering(const Assets *assets) {
  Level lv;
  char err[128];
  CHECK(level_parse(&lv, ARENA, err, sizeof err));
  lv.decoration_count = lv.wall_box_count = lv.hanging_count = 0;
  lv.torch_count = lv.fire_light_count = 0;
  Session session = {.next_life = 40000, .lives = 3};
  Game *g = game_create(&lv, 0, &session, NULL);
  g->player.weapon = WEAPON_DAGGER;
  Renderer renderer;
  renderer_init(&renderer, assets);
  Image *base = image_new(SCREEN_W, SCREEN_H), *other = image_new(SCREEN_W, SCREEN_H);
  image_fill(base, 0xff123456u);
  render_view(&renderer, base, g);
  for (int i = SCREEN_W * VIEW_H; i < SCREEN_W * SCREEN_H; i++) CHECK(base->px[i] == 0xff123456u);
  float depths[SCREEN_W];
  memcpy(depths, renderer.zbuffer, sizeof depths);

  // Every material changes both planes while leaving geometry untouched.
  // Reusing the same Level and Renderer must not retain a previous theme.
  for (int theme = 1; theme < SURFACE_THEME_COUNT; theme++) {
    lv.surface_theme = (SurfaceTheme)theme;
    render_view(&renderer, other, g);
    CHECK(differences(base, other, 0, 80) > 10000);
    CHECK(differences(base, other, 240, VIEW_H) > 10000);
    CHECK(!memcmp(depths, renderer.zbuffer, sizeof depths));
  }
  lv.surface_theme = SURFACE_GUARDHOUSE;
  // Time alone cannot move a dry surface; translation and rotation must.
  g->time = 1;
  render_view(&renderer, other, g);
  CHECK(!differences(base, other, 0, VIEW_H));
  lv.torches[0] = (Torch){g->player.x + 1, g->player.y, 0};
  lv.torch_count = 1;
  render_view(&renderer, other, g);
  CHECK(differences(base, other, 0, 80) > 10000);
  CHECK(differences(base, other, 240, VIEW_H) > 10000);
  lv.torch_count = 0;
  g->player.x += .2f;
  render_view(&renderer, other, g);
  CHECK(differences(base, other, 0, 80) > 10000);
  CHECK(differences(base, other, 240, VIEW_H) > 10000);
  g->player.x -= .2f;
  g->player.dir_x = 0; g->player.dir_y = 1;
  g->player.plane_x = -.66f; g->player.plane_y = 0;
  render_view(&renderer, other, g);
  CHECK(differences(base, other, 0, 80) > 10000);

  // A nearby solid wall hides both planes completely. Neither a changed
  // material nor a changed palette can paint through it.
  g->player.dir_x = 1; g->player.dir_y = 0;
  g->player.plane_x = 0; g->player.plane_y = .66f;
  for (int y = 1; y < lv.h - 1; y++) lv.grid[y * lv.w + 3] = '1';
  render_view(&renderer, base, g);
  lv.surface_theme = SURFACE_KEEP;
  lv.floor_color = rgb(0, 0, 0); lv.ceil_color = rgb(255, 255, 255);
  render_view(&renderer, other, g);
  CHECK(!differences(base, other, 0, VIEW_H));

  // A closed door in that wall also hides the planes, while its opening
  // reveals them without changing their depth or their texture choice.
  lv.grid[5 * lv.w + 3] = 'D';
  g->doors[0] = (Door){.x = 3, .y = 5, .kind = 'D'};
  g->door_at[5 * lv.w + 3] = 0;
  g->door_count = 1;
  lv.surface_theme = SURFACE_GUARDHOUSE;
  render_view(&renderer, base, g);
  lv.surface_theme = SURFACE_KEEP;
  render_view(&renderer, other, g);
  CHECK(!differences(base, other, 0, VIEW_H));
  g->doors[0].open = 1;
  render_view(&renderer, other, g);
  CHECK(differences(base, other, 0, 80) > 1000);

  image_free(base); image_free(other);
  renderer_free(&renderer);
  game_destroy(g);
  level_free(&lv);
}

static void screenshot(const Image *image, const char *path) {
  SDL_Surface *surface = SDL_CreateSurfaceFrom(image->w, image->h, SDL_PIXELFORMAT_ARGB8888,
                                              image->px, image->w * (int)sizeof(uint32_t));
  CHECK(surface && SDL_SaveBMP(surface, path));
  SDL_DestroySurface(surface);
}

static void test_shipped_levels(const Assets *assets, bool screenshots) {
  static const float viewpoints[LEVEL_COUNT][4] = {
    {20.5f, 10.5f, .8f, -.6f}, {17.5f, 17.5f, 1, 0}, {23.5f, 16.5f, 0, -1},
    {17.5f, 22.5f, 0, -1}, {23.5f, 19.5f, 0, -1}
  };
  Renderer renderer;
  renderer_init(&renderer, assets);
  Image *fb = image_new(SCREEN_W, SCREEN_H);
  for (int n = 0; n < LEVEL_COUNT; n++) {
    char path[64], err[128];
    snprintf(path, sizeof path, "levels/%d.txt", n + 1);
    char *text = SDL_LoadFile(path, NULL);
    CHECK(text);
    Level lv;
    CHECK(level_parse(&lv, text, err, sizeof err));
    SDL_free(text);
    CHECK(lv.surface_theme == (SurfaceTheme)n);
    Session session = {.next_life = 40000, .lives = 3};
    Game *g = game_create(&lv, n, &session, NULL);
    render_view(&renderer, fb, g); // initial view and changing map dimensions
    if (screenshots) {
      render_hud(&renderer, fb, g);
      snprintf(path, sizeof path, "build/surfaces-start-%d.bmp", n + 1);
      screenshot(fb, path);
    }
    const float *v = viewpoints[n];
    g->player.x = v[0]; g->player.y = v[1];
    g->player.dir_x = v[2]; g->player.dir_y = v[3];
    g->player.plane_x = -v[3] * .66f; g->player.plane_y = v[2] * .66f;
    g->time = .75f;
    render_view(&renderer, fb, g);
    if (screenshots) {
      render_hud(&renderer, fb, g);
      snprintf(path, sizeof path, "build/surfaces-floor-%d.bmp", n + 1);
      screenshot(fb, path);
      Uint64 start = SDL_GetPerformanceCounter();
      for (int frame = 0; frame < 60; frame++) render_view(&renderer, fb, g);
      printf("Floor %d: %.2f ms per view\n", n + 1,
             (SDL_GetPerformanceCounter() - start) * 1000.0 / SDL_GetPerformanceFrequency() / 60);
    }
    game_destroy(g);
    level_free(&lv);
  }
  image_free(fb);
  renderer_free(&renderer);
}

int main(int argc, char **argv) {
  CHECK(SDL_Init(0));
  random_seed(12345);
  test_parsing();
  Assets assets;
  assets_build(&assets);
  test_rendering(&assets);
  test_shipped_levels(&assets, argc > 1 && !strcmp(argv[1], "--screenshots"));
  SDL_Quit();
  puts("Floor and ceiling checks passed.");
  return 0;
}
