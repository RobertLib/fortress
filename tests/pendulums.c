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
    "1P..........1\n"
    "1...........1\n"
    "1...........1\n"
    "1...&.......1\n"
    "1...........1\n"
    "1.......%...1\n"
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
  f->level.decoration_count = f->level.wall_box_count = f->level.hanging_count = 0;
  f->level.fire_light_count = 0;
  f->session = (Session){.next_life = 40000, .lives = 3};
  f->game = game_create(&f->level, 0, &f->session, NULL);
}

static void teardown(Fixture *f) {
  game_destroy(f->game);
  level_free(&f->level);
}

static void tick(Game *g, float dt) {
  Input in = {.weapon = -1};
  game_update(g, dt, &in);
}

static void aim(Game *g, float x, float y, float dx, float dy) {
  g->player.x = x;
  g->player.y = y;
  g->player.dir_x = dx;
  g->player.dir_y = dy;
  g->player.plane_x = -dy * .66f;
  g->player.plane_y = dx * .66f;
}

static void test_damage_and_timing(void) {
  const int damage[] = {30, 15, 45};
  for (int d = 0; d < DIFFICULTY_COUNT; d++) {
    Fixture f;
    setup(&f);
    Game *g = f.game;
    f.session.difficulty = d;
    aim(g, 4.5f, 4.5f, 1, 0);
    tick(g, .01f);
    CHECK(g->player.health == 100 - damage[d] && g->pendulums[0].hit_cooldown > 0);
    tick(g, .2f);
    CHECK(g->player.health == 100 - damage[d]); // one contact, one hit
    tick(g, 1.4f);
    CHECK(g->player.health == 100 - 2 * damage[d]); // the return sweep hits again
    teardown(&f);
  }
  Fixture f;
  setup(&f);
  Game *g = f.game;
  // At the end of the arc there is room to stand beneath the ceiling pivot.
  aim(g, 4.5f, 4.5f, 1, 0);
  g->time = PENDULUM_PERIOD / 4;
  tick(g, .01f);
  CHECK(g->player.health == 100);
  // The raised blade still hurts if the player actually walks into it.
  g->player.x += PENDULUM_LENGTH * sinf(PENDULUM_MAX_ANGLE);
  tick(g, .01f);
  CHECK(g->player.health == 70);

  // The other map symbol turns the swing by ninety degrees.
  aim(g, 8.5f, 6.5f, 1, 0);
  g->time = PENDULUM_PERIOD / 4;
  tick(g, .01f);
  CHECK(g->player.health == 40 && g->pendulums[1].hit_cooldown > 0);
  teardown(&f);
}

static void test_crossing_and_death(void) {
  for (int safe = 0; safe < 2; safe++) {
    Fixture f;
    setup(&f);
    Game *g = f.game;
    aim(g, 4.5f, 3.8f, 0, 1);
    g->time = safe ? .65f : 1.45f;
    Input in = {.weapon = -1, .forward = true, .run = true};
    game_update(g, .3f, &in);
    CHECK(g->player.y > 5.3f && !g->player_dead);
    CHECK(g->player.health == (safe ? 100 : 70));
    teardown(&f);
  }
  Fixture f;
  setup(&f);
  Game *g = f.game;
  aim(g, 4.5f, 3, 0, 1);
  g->time = PENDULUM_PERIOD / 2 - 1.5f / (3.4f * 1.55f);
  Input in = {.weapon = -1, .forward = true, .run = true};
  game_update(g, .6f, &in); // ends well beyond the trap; the path must still hit it
  CHECK(g->player.health == 70 && g->player.y > 6);
  aim(g, 4.5f, 4.5f, 1, 0);
  g->player.health = 10;
  g->pendulums[0].hit_cooldown = 0;
  g->time = PENDULUM_PERIOD / 2 - .01f;
  int ammo = g->player.ammo;
  in = (Input){.weapon = WEAPON_DAGGER, .fire = true, .use = true};
  game_update(g, .01f, &in);
  CHECK(g->player_dead && !g->completed && g->player.health == 0);
  CHECK(g->player.ammo == ammo && g->player.weapon == WEAPON_CROSSBOW && !g->projectile_count);
  float time = g->time;
  tick(g, 1);
  CHECK(g->time == time); // the trap freezes along with the dead player's world
  game_destroy(g);
  f.game = game_create(&f.level, 0, &f.session, NULL);
  CHECK(f.game->time == 0 && f.game->pendulums[0].hit_cooldown == 0);
  teardown(&f);
}

static void test_save_load(void) {
  Fixture f;
  setup(&f);
  char path[1024], err[128];
  int len = snprintf(path, sizeof path, "%stest-pendulum-save.dat", SDL_GetBasePath());
  CHECK(len > 0 && (size_t)len < sizeof path);
  aim(f.game, 4.5f, 4.5f, 1, 0);
  tick(f.game, .01f);
  float cooldown = f.game->pendulums[0].hit_cooldown, time = f.game->time;
  CHECK(savegame_write(path, f.game, err, sizeof err));
  tick(f.game, 1);
  CHECK(savegame_load(path, &f.level, 1, &f.game, &f.session, err, sizeof err));
  CHECK(f.game->time == time && f.game->pendulums[0].hit_cooldown == cooldown && f.game->player.health == 70);
  tick(f.game, .05f);
  CHECK(f.game->player.health == 70);
  f.game->pendulums[0].hit_cooldown = -1;
  CHECK(!savegame_write(path, f.game, err, sizeof err));
  CHECK(savegame_load(path, &f.level, 1, &f.game, &f.session, err, sizeof err));
  Game *previous = f.game;
  f.level.pendulums[0].dx = 0;
  f.level.pendulums[0].dy = 1;
  CHECK(!savegame_load(path, &f.level, 1, &f.game, &f.session, err, sizeof err));
  CHECK(f.game == previous && f.game->player.health == 70);
  CHECK(SDL_RemovePath(path));
  teardown(&f);
}

static void screenshot(const Image *image, const char *path) {
  SDL_Surface *surface = SDL_CreateSurfaceFrom(image->w, image->h, SDL_PIXELFORMAT_ARGB8888,
                                              image->px, image->w * (int)sizeof(uint32_t));
  CHECK(surface && SDL_SaveBMP(surface, path));
  SDL_DestroySurface(surface);
}

static int changed_pixels(const Image *a, const Image *b) {
  int changed = 0;
  for (int i = 0; i < SCREEN_W * VIEW_H; i++) if (a->px[i] != b->px[i]) changed++;
  return changed;
}

static void test_rendering(bool screenshots) {
  Fixture f;
  setup(&f);
  Game *g = f.game;
  Assets assets;
  assets_build(&assets);
  Renderer renderer;
  renderer_init(&renderer, &assets);
  Image *fb = image_new(SCREEN_W, SCREEN_H), *other = image_new(SCREEN_W, SCREEN_H);
  aim(g, 4.5f, 2.2f, 0, 1);
  render_view(&renderer, fb, g);
  int count = f.level.pendulum_count;
  f.level.pendulum_count = 0;
  render_view(&renderer, other, g);
  CHECK(changed_pixels(fb, other) > 500);
  f.level.pendulum_count = count;
  if (screenshots) {
    render_hud(&renderer, fb, g);
    screenshot(fb, "build/pendulum-centre.bmp");
  }
  g->time = PENDULUM_PERIOD / 4;
  render_view(&renderer, other, g);
  CHECK(changed_pixels(fb, other) > 500);
  if (screenshots) {
    render_hud(&renderer, other, g);
    screenshot(other, "build/pendulum-raised.bmp");
  }
  // The blade has thickness, so it also reads from along the swing plane.
  aim(g, 2.2f, 4.5f, 1, 0);
  g->time = 0;
  render_view(&renderer, fb, g);
  f.level.pendulum_count = 0;
  render_view(&renderer, other, g);
  CHECK(changed_pixels(fb, other) > 50);
  f.level.pendulum_count = count;
  if (screenshots) screenshot(fb, "build/pendulum-edge.bmp");

  // Solid sides follow the painted silhouette throughout the swing and
  // across the former edge-view cutoff (atan(.25), about 14 degrees).
  const float views[] = {0, 13, 15, 45, 90, 180, 270};
  float *depth = malloc(sizeof(float) * SCREEN_W * VIEW_H);
  CHECK(depth);
  Image *sheet = screenshots ? image_new(7 * 320, 3 * 200) : NULL;
  g->player.weapon = WEAPON_DAGGER;
  f.level.pendulum_count = 1;
  for (int axis = 0; axis < 2; axis++) {
    f.level.pendulums[0].dx = !axis;
    f.level.pendulums[0].dy = axis;
    for (int phase = 0; phase < 3; phase++) {
      g->time = phase == 0 ? 0 : phase == 1 ? PENDULUM_PERIOD / 4 : PENDULUM_PERIOD * .75f;
      for (int view = 0; view < 7; view++) {
        float angle = (views[view] + axis * 90) * (float)M_PI / 180;
        float dx = cosf(angle), dy = sinf(angle);
        aim(g, 4.5f - dx * 1.7f, 4.5f - dy * 1.7f, dx, dy);
        render_view(&renderer, fb, g);
        memcpy(depth, renderer.depth, sizeof(float) * SCREEN_W * VIEW_H);
        f.level.pendulum_count = 0;
        render_view(&renderer, other, g);
        f.level.pendulum_count = 1;
        int solid = 0;
        for (int i = 0; i < SCREEN_W * VIEW_H; i++) {
          if (depth[i] >= renderer.depth[i]) continue;
          CHECK(isfinite(depth[i]) && depth[i] > .05f && depth[i] < 2.6f);
          solid++;
        }
        CHECK(solid > 100 && changed_pixels(fb, other) > 100);
        if (sheet && !axis) {
          image_reset_clip(fb);
          gfx_blit(sheet, fb, 0, 0, SCREEN_W, SCREEN_H, view * 320, phase * 200, 320, 200, 1);
        }
      }
    }
  }
  if (sheet) screenshot(sheet, "build/pendulum-views.bmp");
  image_free(sheet);
  free(depth);
  f.level.pendulums[0].dx = 1;
  f.level.pendulums[0].dy = 0;
  f.level.pendulum_count = count;
  g->time = 0;

  // Full masonry hides the whole trap, including its ceiling attachment.
  aim(g, 4.5f, 2.2f, 0, 1);
  for (int x = 1; x < f.level.w - 1; x++) f.level.grid[3 * f.level.w + x] = '1';
  render_view(&renderer, fb, g);
  f.level.pendulum_count = 0;
  render_view(&renderer, other, g);
  CHECK(changed_pixels(fb, other) == 0);
  f.level.pendulum_count = count;

  // The automap reveals blades only in explored cells.
  image_fill(fb, 0xff000000u);
  render_map(&renderer, fb, g, NULL);
  g->visited[4 * f.level.w + 4] = 1;
  image_fill(other, 0xff000000u);
  render_map(&renderer, other, g, NULL);
  int x = SCREEN_W / 2 - f.level.w * 4 + 4 * 8 + 4;
  int y = (VIEW_H - f.level.h * 8) / 2 + 4 * 8 + 4;
  CHECK(fb->px[y * SCREEN_W + x] != other->px[y * SCREEN_W + x]);
  CHECK(other->px[y * SCREEN_W + x] == 0xffd48751u);
  image_free(fb);
  image_free(other);
  renderer_free(&renderer);
  assets_free(&assets);
  teardown(&f);
}

static void test_maps_and_projectiles(void) {
  Fixture f;
  setup(&f);
  CHECK(f.level.pendulum_count == 2 && f.level.pendulums[0].dx == 1 && f.level.pendulums[1].dy == 1);
  CHECK(game_cell(f.game, 4, 4) == '.' && !game_is_blocked(f.game, 4.5f, 4.5f, .22f, NULL));
  CHECK(game_line_of_sight(f.game, 1.5f, 4.5f, 10.5f, 4.5f));
  f.game->projectiles[f.game->projectile_count++] = (Projectile){1.5f, 4.5f, 1, 0, 0, .44f, SHOT_TRAP, NULL, 0, 0, WEAPON_CROSSBOW};
  tick(f.game, .6f);
  CHECK(f.game->projectile_count == 1 && f.game->projectiles[0].x > 6);
  teardown(&f);
  for (int n = 1; n <= LEVEL_COUNT; n++) {
    char path[32], err[128];
    snprintf(path, sizeof path, "levels/%d.txt", n);
    char *text = SDL_LoadFile(path, NULL);
    Level lv;
    CHECK(text && level_parse(&lv, text, err, sizeof err));
    SDL_free(text);
    CHECK(lv.pendulum_count >= 1 && lv.key_count == 3);
    for (int i = 0; i < lv.pendulum_count; i++) {
      const PendulumSpawn *p = &lv.pendulums[i];
      CHECK(lv.grid[p->y * lv.w + p->x] == '.');
      for (int j = 0; j < lv.decoration_count; j++) {
        const Decoration *d = &lv.decorations[j];
        CHECK(!d->radius || hypot(d->x - (p->x + .5), d->y - (p->y + .5)) > 1);
      }
    }
    level_free(&lv);
  }
}

int main(int argc, char **argv) {
  CHECK(SDL_Init(0));
  random_seed(12345);
  test_damage_and_timing();
  test_crossing_and_death();
  test_save_load();
  test_maps_and_projectiles();
  test_rendering(argc > 1 && !strcmp(argv[1], "--screenshots"));
  SDL_Quit();
  puts("Pendulum collision, timing, save/load, maps and rendering checks passed.");
  return 0;
}
