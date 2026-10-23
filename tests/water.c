#include "render.h"
#include "savegame.h"

#include <SDL3/SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(expr) do { if (!(expr)) { \
  fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #expr); exit(1); \
} } while (0)

static const char *ARENA =
    "1111111111111\n"
    "1...........1\n"
    "1P.>>====..g1\n"
    "1....====...1\n"
    "1....}..<...1\n"
    "1....{......1\n"
    "1...........1\n"
    "1111111111111\n";

typedef struct Fixture {
  Level level;
  Session session;
  Game *game;
} Fixture;

static void setup(Fixture *f) {
  memset(f, 0, sizeof *f);
  char err[128];
  CHECK(level_parse(&f->level, ARENA, err, sizeof err));
  // Automatic dressing must leave the water clear even before the arena
  // is stripped for the isolated movement/render checks below.
  for (int i = 0; i < f->level.decoration_count; i++) {
    const Decoration *d = &f->level.decorations[i];
    CHECK(!level_is_water(f->level.grid[(int)d->y * f->level.w + (int)d->x]));
  }
  f->level.decoration_count = f->level.wall_box_count = f->level.hanging_count = 0;
  f->session = (Session){.next_life = 40000, .lives = 3};
  f->game = game_create(&f->level, 0, &f->session, NULL);
  f->game->enemies[0].state = ES_PAIN;
  f->game->enemies[0].timer = 100;
}

static void teardown(Fixture *f) {
  game_destroy(f->game);
  level_free(&f->level);
}

static void test_movement_and_combat(void) {
  Fixture f;
  setup(&f);
  Game *g = f.game;
  CHECK(f.level.water_count == 13);
  for (int type = 0; type < ENEMY_TYPE_COUNT; type++) {
    Enemy enemy;
    EnemySpawn spawn = {4.5f, 2.5f, type, true, -1};
    enemy_init(&enemy, &spawn);
    CHECK(!game_is_blocked(g, 6.5f, 2.5f, enemy.radius, &enemy));
  }
  Input in = {.forward = true, .weapon = -1};
  g->player.x = 5.5f;
  game_update(g, .2f, &in);
  float wet_distance = g->player.x - 5.5f;
  CHECK(fabsf(wet_distance - 3.4f * .2f * WATER_SPEED) < .001f);
  CHECK(!g->player_dead && g->player.health == 100);
  g->player.x = 5.5f;
  g->player.y = 1.5f;
  game_update(g, .2f, &in);
  CHECK(fabsf(wet_distance / (g->player.x - 5.5f) - WATER_SPEED) < .001f);

  // Enter and leave an entire channel in one long sprinting frame.
  g->player.x = 2.5f;
  g->player.y = 2.5f;
  in.run = true;
  game_update(g, 2.1f, &in);
  CHECK(g->player.x > 9 && !g->player_dead && g->player.health == 100);
  CHECK(g->player.x < 10.5f);

  g->player.x = 1.5f;
  CHECK(game_line_of_sight(g, 1.5f, 2.5f, 11.5f, 2.5f));
  int health = g->enemies[0].hp;
  game_loose_bolt(g, 1.5f, 2.5f, 1, 0, SHOT_AT_ENEMY, &g->enemies[0], 12);
  in = (Input){.weapon = -1};
  game_update(g, .8f, &in);
  CHECK(g->enemies[0].hp == health - 12);

  // Ground patrols wade; the same airborne patrol crosses at full speed.
  const EnemyType types[] = {ENEMY_GUARD, ENEMY_BAT};
  for (int t = 0; t < 2; t++) {
    EnemyType type = types[t];
    float distance[2];
    for (int wet = 0; wet < 2; wet++) {
      Enemy enemy;
      EnemySpawn spawn = {6.5f, wet ? 3.5f : 6.5f, type, true, -1};
      enemy_init(&enemy, &spawn);
      enemy.state = ES_PATROL;
      enemy.dir_x = 1;
      enemy.dir_y = 0;
      enemy.timer = 100;
      enemy_update(&enemy, g, .1f);
      distance[wet] = enemy.x - 6.5f;
    }
    CHECK(fabsf(distance[1] / distance[0] - (type == ENEMY_BAT ? 1 : WATER_SPEED)) < .001f);
  }
  teardown(&f);
}

static void test_save_load(void) {
  Fixture f;
  setup(&f);
  char path[1024], err[128];
  int len = snprintf(path, sizeof path, "%stest-water-save.dat", SDL_GetBasePath());
  CHECK(len > 0 && (size_t)len < sizeof path);
  f.game->player.x = 6.5f;
  f.game->time = 4.375f;
  CHECK(savegame_write(path, f.game, err, sizeof err));
  f.game->player.x = 1.5f;
  f.game->time = 8;
  CHECK(savegame_load(path, &f.level, 1, &f.game, &f.session, err, sizeof err));
  CHECK(f.game->player.x == 6.5f && f.game->time == 4.375f);
  CHECK(game_cell(f.game, 6, 2) == '=' && game_cell(f.game, 3, 2) == '>');
  Input in = {.forward = true, .weapon = -1};
  game_update(f.game, .1f, &in);
  CHECK(fabsf(f.game->player.x - 6.5f - 3.4f * .1f * WATER_SPEED) < .001f);
  CHECK(SDL_RemovePath(path));
  teardown(&f);
}

static void screenshot(const Image *image, const char *path) {
  SDL_Surface *surface = SDL_CreateSurfaceFrom(image->w, image->h, SDL_PIXELFORMAT_ARGB8888,
                                              image->px, image->w * (int)sizeof(uint32_t));
  CHECK(surface && SDL_SaveBMP(surface, path));
  SDL_DestroySurface(surface);
}

static void test_rendering(const Assets *assets, bool screenshots) {
  Fixture f;
  setup(&f);
  Game *g = f.game;
  g->enemy_count = 0;
  g->player.weapon = WEAPON_DAGGER;
  f.level.torch_count = f.level.fire_light_count = 0;
  Renderer renderer;
  renderer_init(&renderer, assets);
  Image *fb = image_new(SCREEN_W, SCREEN_H);
  Image *other = image_new(SCREEN_W, SCREEN_H);
  render_view(&renderer, fb, g);
  CHECK(fabsf(renderer.zbuffer[SCREEN_W / 2] - 10.5f) < .01f);
  g->time = .125f;
  render_view(&renderer, other, g);
  int changed = 0;
  for (int y = VIEW_H / 2; y < VIEW_H; y++)
    for (int x = 0; x < SCREEN_W; x++)
      if (fb->px[y * SCREEN_W + x] != other->px[y * SCREEN_W + x]) changed++;
  CHECK(changed > 500);
  CHECK(!memcmp(fb->px, other->px, sizeof(uint32_t) * SCREEN_W * (VIEW_H / 2)));
  if (screenshots) {
    render_hud(&renderer, fb, g);
    screenshot(fb, "build/water-view.bmp");
  }

  // Water fully hidden behind masonry must not paint over that wall.
  for (int y = 1; y < f.level.h - 1; y++) f.level.grid[y * f.level.w + 2] = '1';
  render_view(&renderer, fb, g);
  for (int i = 0; i < f.level.w * f.level.h; i++)
    if (level_is_water(f.level.grid[i])) f.level.grid[i] = '.';
  render_view(&renderer, other, g);
  CHECK(!memcmp(fb->px, other->px, sizeof(uint32_t) * SCREEN_W * VIEW_H));

  // Only explored water appears on the automap.
  f.level.grid[2 * f.level.w + 6] = '=';
  g->visited[2 * f.level.w + 6] = 1;
  render_map(&renderer, fb, g, NULL);
  int mx = SCREEN_W / 2 - f.level.w * 4 + 6 * 8 + 4;
  int my = (VIEW_H - f.level.h * 8) / 2 + 2 * 8 + 4;
  CHECK(fb->px[my * SCREEN_W + mx] == 0xff28535bu);
  image_free(other);
  image_free(fb);
  renderer_free(&renderer);
  teardown(&f);
}

static void test_shipped_levels(const Assets *assets, bool screenshots) {
  static const float viewpoints[LEVEL_COUNT][4] = {
    {20.5f, 10.5f, .8f, -.6f}, {17.5f, 17.5f, 1, 0}, {14.5f, 29.5f, 1, 0},
    {3.5f, 10.5f, 1, 0}, {11.5f, 29.5f, 1, 0}
  };
  for (int n = 1; n <= LEVEL_COUNT; n++) {
    char path[64], err[128];
    snprintf(path, sizeof path, "levels/%d.txt", n);
    size_t size;
    char *text = SDL_LoadFile(path, &size);
    CHECK(text);
    Level level;
    CHECK(level_parse(&level, text, err, sizeof err));
    SDL_free(text);
    CHECK(level.water_count >= 6 && level.key_count == 3);
    int pools = 0, streams = 0;
    for (int i = 0; i < level.w * level.h; i++) {
      if (level.grid[i] == '=') pools++;
      else if (level_is_water(level.grid[i])) streams++;
    }
    CHECK(pools > 0 && streams > 0);
    for (int i = 0; i < level.decoration_count; i++) {
      const Decoration *d = &level.decorations[i];
      CHECK(!level_is_water(level.grid[(int)d->y * level.w + (int)d->x]));
    }
    if (screenshots) {
      Session session = {.next_life = 40000, .lives = 3};
      Game *g = game_create(&level, n - 1, &session, NULL);
      const float *v = viewpoints[n - 1];
      g->player.x = v[0]; g->player.y = v[1];
      g->player.dir_x = v[2]; g->player.dir_y = v[3];
      g->player.plane_x = -v[3] * .66f; g->player.plane_y = v[2] * .66f;
      g->time = .75f;
      Renderer renderer;
      renderer_init(&renderer, assets);
      Image *fb = image_new(SCREEN_W, SCREEN_H);
      render_view(&renderer, fb, g);
      render_hud(&renderer, fb, g);
      snprintf(path, sizeof path, "build/water-floor-%d.bmp", n);
      screenshot(fb, path);
      image_free(fb);
      renderer_free(&renderer);
      game_destroy(g);
    }
    level_free(&level);
  }
}

int main(int argc, char **argv) {
  CHECK(SDL_Init(0));
  random_seed(12345);
  test_movement_and_combat();
  test_save_load();
  Assets assets;
  assets_build(&assets);
  bool screenshots = argc > 1 && !strcmp(argv[1], "--screenshots");
  test_rendering(&assets, screenshots);
  test_shipped_levels(&assets, screenshots);
  SDL_Quit();
  puts("Shallow water checks passed.");
  return 0;
}
