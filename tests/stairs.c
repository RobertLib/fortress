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
    "@height 3 0 14 8 .125\n"
    "@height 4 0 13 8 .25\n"
    "@height 5 0 12 8 .375\n"
    "@height 6 0 11 8 .5\n"
    "@height 7 0 10 8 .625\n"
    "@height 8 0 9 8 .75\n"
    "11111111111111111\n"
    "1...............1\n"
    "1...............1\n"
    "1P..........g...1\n"
    "1...............1\n"
    "1...............1\n"
    "1...............1\n"
    "11111111111111111\n";

typedef struct Fixture { Level level; Session session; Game *game; } Fixture;

static void setup(Fixture *f) {
  memset(f, 0, sizeof *f);
  char err[160];
  CHECK(level_parse(&f->level, ARENA, err, sizeof err));
  // Stairs must stay free of automatically placed solid furniture.
  for (int i = 0; i < f->level.decoration_count; i++) {
    const Decoration *d = &f->level.decorations[i];
    CHECK(!d->radius || d->x < 2 || d->x >= 9);
  }
  f->level.decoration_count = f->level.wall_box_count = f->level.hanging_count = 0;
  f->level.torch_count = f->level.fire_light_count = 0;
  f->session = (Session){.lives = 3, .next_life = 40000};
  f->game = game_create(&f->level, 0, &f->session, NULL);
  f->game->enemies[0].state = ES_PAIN;
  f->game->enemies[0].timer = 100;
}

static void teardown(Fixture *f) { game_destroy(f->game); level_free(&f->level); }

static void test_parsing(void) {
  Level lv;
  char text[4096], err[160];
  const char *bad[] = {"-1 0 2 2 .125", "0 0 18 8 .125", "0 0 1 1 .2", "0 0 1 1 nan",
                       "0 0 1 1 4.125", "0 0 1 1", "0 0 1 1 0 extra", "0 0 0 1 .125"};
  for (size_t i = 0; i < sizeof bad / sizeof bad[0]; i++) {
    snprintf(text, sizeof text, "@height %s\n%s", bad[i], ARENA);
    CHECK(!level_parse(&lv, text, err, sizeof err));
    CHECK(strstr(err, "Invalid @height"));
    level_free(&lv);
  }
  snprintf(text, sizeof text, "%s@height 1 1 1 1 -2\n", ARENA);
  CHECK(level_parse(&lv, text, err, sizeof err));
  CHECK(lv.has_heights && level_floor_at(&lv, 1, 1) == -2 && level_floor_at(&lv, 8, 3) == .75f);
  level_free(&lv);
}

static void test_movement(void) {
  Fixture f; setup(&f);
  Game *g = f.game;
  g->enemy_count = 0;
  Input in = {.forward = true, .run = true, .weapon = -1};
  game_update(g, 1.5f, &in); // a long frame still visits every tread
  CHECK(g->player.x > 9 && level_floor(&f.level, g->player.x, g->player.y) == .75f);
  CHECK(g->player.health == 100 && !g->player_dead);
  in.forward = false; in.back = true;
  game_update(g, 1.5f, &in);
  CHECK(fabsf(g->player.x - 1.5f) < .01f && level_floor(&f.level, g->player.x, g->player.y) == 0);
  // Walk sideways up the same flight, then let the camera settle on its landing.
  g->player.dir_x = 0; g->player.dir_y = -1;
  g->player.plane_x = .66f; g->player.plane_y = 0;
  in.back = false; in.strafe_right = true;
  game_update(g, 1.5f, &in);
  CHECK(g->player.x > 9);
  in.strafe_right = false;
  game_update(g, .5f, &in);
  CHECK(fabsf(g->player.step_offset) < .001f);
  // An abrupt ledge is impassable in either direction.
  for (int y = 0; y < f.level.h; y++) f.level.heights[y * f.level.w + 3] = .75f;
  g->player.x = 2.5f;
  CHECK(game_is_blocked(g, 3.1f, 3.5f, .22f, NULL));
  g->player.x = 3.5f;
  CHECK(game_is_blocked(g, 2.9f, 3.5f, .22f, NULL));
  // A grounded patrol climbs with the same movement constraints, even at large dt.
  g->player.x = 15.5f;
  f.level.heights[3 * f.level.w + 3] = .125f;
  Enemy e;
  EnemySpawn spawn = {2.5f, 3.5f, ENEMY_GUARD, true, -1};
  enemy_init(&e, &spawn); e.dir_x = 1; e.dir_y = 0;
  enemy_update(&e, g, 5);
  CHECK(e.x > 7 && level_floor(&f.level, e.x, e.y) >= .625f);
  teardown(&f);
}

static void test_combat(void) {
  Fixture f; setup(&f);
  Game *g = f.game;
  Enemy *e = &g->enemies[0];
  CHECK(game_line_of_sight(g, 1.5f, 3.5f, e->x, e->y));
  int hp = e->hp;
  Input in = {.fire = true, .weapon = -1};
  game_update(g, .01f, &in); // horizontal aim automatically selects an uphill enemy
  CHECK(g->projectile_count == 1 && g->projectiles[0].kind == SHOT_AT_ENEMY);
  in.fire = false;
  game_update(g, .65f, &in);
  CHECK(e->hp < hp);
  // The descending shot also reaches its mark.
  game_loose_bolt(g, e->x, e->y, -1, 0, SHOT_AT_PLAYER, NULL, 10);
  CHECK(g->projectiles[g->projectile_count - 1].z > 1);
  game_update(g, .65f, &in);
  CHECK(g->player.health == 90);
  // A level shot runs into a riser rather than following the floor upward.
  game_loose_bolt(g, 1.5f, 3.5f, 1, 0, SHOT_MISS, NULL, 0);
  game_update(g, .4f, &in);
  CHECK(g->projectile_count == 0);
  // A raised intervening platform blocks both sight and a homing bolt.
  f.level.heights[3 * f.level.w + 5] = 1.5f;
  CHECK(!game_line_of_sight(g, 1.5f, 3.5f, e->x, e->y));
  hp = e->hp;
  game_loose_bolt(g, 1.5f, 3.5f, 1, 0, SHOT_AT_ENEMY, e, 12);
  game_update(g, .65f, &in);
  CHECK(e->hp == hp && g->projectile_count == 0);
  teardown(&f);

  // Powder barrels are auto-aimed at their actual height in both directions.
  for (int descending = 0; descending < 2; descending++) {
    setup(&f); g = f.game; g->enemy_count = 0;
    g->barrel_count = 1;
    g->barrels[0] = (Barrel){.x = descending ? 1.5f : 12.5f, .y = 3.5f, .powder = true};
    if (descending) { g->player.x = 12.5f; g->player.dir_x = -1; g->player.plane_y = -.66f; }
    in.fire = true;
    game_update(g, .01f, &in);
    CHECK(g->projectile_count == 1 && (descending ? g->projectiles[0].slope < 0 : g->projectiles[0].slope > 0));
    in.fire = false;
    game_update(g, .65f, &in);
    CHECK(g->barrels[0].destroyed);
    teardown(&f);
  }
}

static void test_save_load(void) {
  Fixture f; setup(&f);
  char path[1024], err[160];
  snprintf(path, sizeof path, "%stest-stairs-save.dat", SDL_GetBasePath());
  f.game->player.x = 9.5f;
  f.game->player.step_offset = -.08f;
  game_loose_bolt(f.game, 9.5f, 3.5f, 1, 0, SHOT_MISS, NULL, 0);
  // A second missile below the original z=0 floor exercises the new save bounds.
  f.game->projectiles[f.game->projectile_count++] = (Projectile){2, 2, 1, 0, 0, -1.5f, SHOT_MISS, NULL, 0, -.125f, WEAPON_CROSSBOW};
  CHECK(savegame_write(path, f.game, err, sizeof err));
  f.game->player.x = 1.5f;
  CHECK(savegame_load(path, &f.level, 1, &f.game, &f.session, err, sizeof err));
  CHECK(f.game->player.x == 9.5f && f.game->player.step_offset == -.08f);
  CHECK(f.game->projectile_count == 2 && f.game->projectiles[0].z > 1 && f.game->projectiles[1].z == -1.5f && f.game->projectiles[1].slope == -.125f);
  f.level.heights[0] += LEVEL_STEP;
  Game *original = f.game;
  CHECK(!savegame_load(path, &f.level, 1, &f.game, &f.session, err, sizeof err));
  CHECK(f.game == original && strstr(err, "DIFFERENT LEVEL"));
  CHECK(SDL_RemovePath(path));
  teardown(&f);
}

static void screenshot(const Image *fb, const char *path) {
  SDL_Surface *surface = SDL_CreateSurfaceFrom(fb->w, fb->h, SDL_PIXELFORMAT_ARGB8888,
                                              fb->px, fb->w * (int)sizeof(uint32_t));
  CHECK(surface && SDL_SaveBMP(surface, path));
  SDL_DestroySurface(surface);
}

static void test_rendering(const Assets *assets, bool screenshots) {
  Fixture f; setup(&f);
  Game *g = f.game;
  Renderer r; renderer_init(&r, assets);
  Image *fb = image_new(SCREEN_W, SCREEN_H), *other = image_new(SCREEN_W, SCREEN_H);
  render_view(&r, fb, g);
  // The first riser really occupies the ray at x=3, rather than a painted floor stripe.
  int pixel = 250 * SCREEN_W + SCREEN_W / 2;
  CHECK(fabsf(r.depth[pixel] - 1.5f) < .01f && !r.floor_mask[pixel]);
  if (screenshots) { render_hud(&r, fb, g); screenshot(fb, "build/stairs-up.bmp"); }
  g->player.x = 10.5f; g->player.dir_x = -1; g->player.plane_y = -.66f;
  render_view(&r, fb, g);
  if (screenshots) { render_hud(&r, fb, g); screenshot(fb, "build/stairs-down.bmp"); }
  // A half-height barrier hides the lower body while the head remains visible.
  memset(f.level.heights, 0, sizeof(float) * f.level.w * f.level.h);
  for (int y = 0; y < f.level.h; y++) f.level.heights[y * f.level.w + 5] = .5f;
  g->player.x = 1.5f; g->player.dir_x = 1; g->player.plane_y = .66f;
  g->enemies[0].x = 8.5f;
  render_view(&r, fb, g);
  g->enemy_count = 0;
  render_view(&r, other, g);
  int above = 0, below = 0;
  for (int y = 0; y < VIEW_H; y++) {
    for (int x = 0; x < SCREEN_W; x++) {
      if (fb->px[y * SCREEN_W + x] == other->px[y * SCREEN_W + x]) continue;
      if (y < VIEW_H / 2) above++; else below++;
    }
  }
  CHECK(above > 20 && below == 0);
  if (screenshots) screenshot(fb, "build/stairs-ledge.bmp");
  image_free(fb); image_free(other); renderer_free(&r); teardown(&f);
}

static void test_raised_features(const Assets *assets) {
  Fixture f; setup(&f);
  Game *g = f.game; g->enemy_count = 0;
  Renderer r; renderer_init(&r, assets);
  Image *fb = image_new(SCREEN_W, SCREEN_H), *other = image_new(SCREEN_W, SCREEN_H);
  // Animated water on a raised tread only repaints that tread's visible floor.
  int cell = 3 * f.level.w + 4;
  f.level.grid[cell] = '='; f.level.water_count = 1;
  render_view(&r, fb, g);
  g->time = .125f;
  render_view(&r, other, g);
  int changed = 0;
  for (int i = 0; i < SCREEN_W * VIEW_H; i++) {
    if (fb->px[i] == other->px[i]) continue;
    CHECK(r.floor_mask[i]); changed++;
  }
  CHECK(changed > 20);
  f.level.grid[cell] = '.'; f.level.water_count = 0;
  render_view(&r, fb, g);
  f.level.grid[cell] = '!'; f.level.pit_count = 1;
  render_view(&r, other, g);
  changed = 0;
  for (int i = 0; i < SCREEN_W * VIEW_H; i++) {
    if (fb->px[i] == other->px[i]) continue;
    CHECK(r.floor_mask[i]); changed++;
  }
  CHECK(changed > 20);
  f.level.grid[cell] = '.'; f.level.pit_count = 0;
  // A door on the high landing stays at that landing's height and opens normally.
  for (int y = 1; y < f.level.h - 1; y++) f.level.grid[y * f.level.w + 10] = '1';
  f.level.grid[3 * f.level.w + 10] = 'D';
  g->doors[0] = (Door){.x = 10, .y = 3, .kind = 'D'};
  g->door_count = 1; g->door_at[3 * f.level.w + 10] = 0;
  render_view(&r, fb, g);
  CHECK(fabsf(r.zbuffer[SCREEN_W / 2] - 9) < .01f);
  g->doors[0].open = 1;
  render_view(&r, other, g);
  CHECK(r.zbuffer[SCREEN_W / 2] > 14);
  changed = 0;
  for (int i = 0; i < SCREEN_W * VIEW_H; i++) if (fb->px[i] != other->px[i]) changed++;
  CHECK(changed > 200);
  // Only visited cells reveal stair marks; plateau shading shows their elevation.
  memset(g->visited, 0, (size_t)f.level.w * f.level.h);
  image_fill(fb, 0xff000000u); render_map(&r, fb, g, NULL);
  g->visited[3 * f.level.w + 3] = 1;
  image_fill(other, 0xff000000u); render_map(&r, other, g, NULL);
  int ox = SCREEN_W / 2 - f.level.w * 4, oy = (VIEW_H - f.level.h * 8) / 2;
  int mx = ox + 3 * 8 + 4, my = oy + 3 * 8 + 4;
  CHECK(fb->px[my * SCREEN_W + mx] != other->px[my * SCREEN_W + mx]);
  g->visited[f.level.w + 1] = g->visited[f.level.w + 12] = 1;
  render_map(&r, other, g, NULL);
  CHECK(other->px[(oy + 12) * SCREEN_W + ox + 12 * 8 + 4] >
        other->px[(oy + 12) * SCREEN_W + ox + 12]);
  // Crossing a tread, hugging a corner and turning through axis-aligned rays
  // must remain valid even with camera easing and near-plane clipping.
  for (int a = 0; a < 8; a++) {
    g->player.x = 2.99f; g->player.y = 1.23f;
    float angle = a * (float)M_PI / 4;
    g->player.dir_x = cosf(angle); g->player.dir_y = sinf(angle);
    g->player.plane_x = -g->player.dir_y * .66f; g->player.plane_y = g->player.dir_x * .66f;
    g->player.step_offset = a % 2 ? -.12f : .12f;
    render_view(&r, fb, g);
    for (int i = 0; i < SCREEN_W * VIEW_H; i++) CHECK(!isnan(r.depth[i]) && r.depth[i] > 0);
  }
  image_free(fb); image_free(other); renderer_free(&r); teardown(&f);
}

static void test_shipped_levels(const Assets *assets, bool screenshots) {
  for (int n = 1; n <= LEVEL_COUNT; n++) {
    char path[96], err[160];
    snprintf(path, sizeof path, "levels/%d.txt", n);
    char *text = SDL_LoadFile(path, NULL);
    Level lv;
    CHECK(text && level_parse(&lv, text, err, sizeof err)); SDL_free(text);
    CHECK(lv.has_heights && lv.key_count == 3);
    int steps = 0;
    for (int y = 1; y < lv.h - 1; y++) {
      for (int x = 1; x < lv.w - 1; x++) {
        if (!level_is_open(lv.grid[y * lv.w + x])) continue;
        const int dirs[2][2] = {{1, 0}, {0, 1}};
        for (int d = 0; d < 2; d++) {
          int nx = x + dirs[d][0], ny = y + dirs[d][1];
          if (!level_is_open(lv.grid[ny * lv.w + nx])) continue;
          float dz = fabsf(level_floor_at(&lv, nx, ny) - level_floor_at(&lv, x, y));
          CHECK(dz <= LEVEL_STEP + .001f);
          if (dz > 0) steps++;
        }
      }
    }
    CHECK(steps >= 8);
    if (screenshots) {
      Session session = {.lives = 3, .next_life = 40000};
      Game *g = game_create(&lv, n - 1, &session, NULL);
      const float views[LEVEL_COUNT][4] = {{5.5f, 11.5f, 0, -1}, {20.5f, 7.5f, 0, 1},
        {15.5f, 10.5f, 1, 0}, {9.5f, 22.5f, 0, 1}, {24.5f, 24.5f, 0, -1}};
      const float *v = views[n - 1];
      g->player.x = v[0]; g->player.y = v[1]; g->player.dir_x = v[2]; g->player.dir_y = v[3];
      g->player.plane_x = -v[3] * .66f; g->player.plane_y = v[2] * .66f;
      Renderer r; renderer_init(&r, assets); Image *fb = image_new(SCREEN_W, SCREEN_H);
      render_view(&r, fb, g); render_hud(&r, fb, g);
      snprintf(path, sizeof path, "build/stairs-floor-%d.bmp", n); screenshot(fb, path);
      Uint64 start = SDL_GetPerformanceCounter();
      for (int i = 0; i < 30; i++) render_view(&r, fb, g);
      printf("Floor %d: %.2f ms per view\n", n, (SDL_GetPerformanceCounter() - start) * 1000.0 / SDL_GetPerformanceFrequency() / 30);
      image_free(fb); renderer_free(&r); game_destroy(g);
    }
    level_free(&lv);
  }
}

int main(int argc, char **argv) {
  CHECK(SDL_Init(0)); random_seed(12345);
  test_parsing(); test_movement(); test_combat(); test_save_load();
  Assets assets; assets_build(&assets);
  bool screenshots = argc > 1 && !strcmp(argv[1], "--screenshots");
  test_rendering(&assets, screenshots); test_raised_features(&assets); test_shipped_levels(&assets, screenshots);
  SDL_Quit(); puts("Vertical levels and stairs checks passed."); return 0;
}
