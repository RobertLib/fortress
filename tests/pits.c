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
    "11111111111\n"
    "1.........1\n"
    "1P..!....g1\n"
    "1.........1\n"
    "1.........1\n"
    "11111111111\n";

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
  f->session = (Session){.next_life = 40000, .lives = 3};
  f->game = game_create(&f->level, 0, &f->session, NULL);
  f->game->enemies[0].state = ES_PAIN;
  f->game->enemies[0].timer = 100;
}

static void teardown(Fixture *f) {
  game_destroy(f->game);
  level_free(&f->level);
}

static void test_falling(void) {
  static const float directions[4][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};
  for (int d = 0; d < DIFFICULTY_COUNT; d++) {
    for (int side = 0; side < 4; side++) {
      Fixture f;
      setup(&f);
      Game *g = f.game;
      Player *p = &g->player;
      f.session.difficulty = d;
      float dx = directions[side][0], dy = directions[side][1];
      p->x = 4.5f - dx * 0.55f;
      p->y = 2.5f - dy * 0.55f;
      p->dir_x = dx;
      p->dir_y = dy;
      p->plane_x = -dy * 0.66f;
      p->plane_y = dx * 0.66f;
      g->show_map = true;
      int ammo = p->ammo;
      Input in = {.forward = true, .run = true, .fire = true, .use = true, .weapon = WEAPON_DAGGER};
      game_update(g, 0.05f, &in);
      CHECK(g->player_dead && p->health == 0 && g->pit_fall > 0 && !g->completed);
      CHECK(p->ammo == ammo && p->weapon == WEAPON_CROSSBOW && g->projectile_count == 0);
      CHECK(!g->show_map && strstr(g->message, "PIT"));
      float x = p->x, y = p->y;
      game_update(g, 1, &in);
      CHECK(p->x == x && p->y == y && g->pit_fall == PIT_FALL_TIME);
      teardown(&f);
    }
  }

  Fixture f;
  setup(&f);
  Game *g = f.game;
  Input in = {.forward = true, .run = true, .weapon = -1};
  g->player.x = 3.5f;
  game_update(g, 0.6f, &in); // could cross the entire pit without movement substeps
  CHECK(g->player_dead && g->player.x > 4 && g->player.x < 5);
  teardown(&f);

  setup(&f);
  g = f.game;
  g->player.x = 4.02f; // the visible stone lip still supports the player
  in = (Input){.weapon = -1};
  game_update(g, 0.01f, &in);
  CHECK(!g->player_dead);
  g->player.x = 4.5f;
  g->player.y = 1.5f; // a safe route beside the hole
  in.forward = true;
  game_update(g, 0.5f, &in);
  CHECK(!g->player_dead && g->player.x > 6);
  teardown(&f);
}

static void test_enemies_and_arrows(void) {
  Fixture f;
  setup(&f);
  Game *g = f.game;
  CHECK(f.level.pit_count == 1 && game_cell(g, 4, 2) == '!');
  CHECK(!game_is_blocked(g, 4.5f, 2.5f, g->player.radius, NULL));
  for (int type = 0; type < ENEMY_TYPE_COUNT; type++) {
    Enemy e;
    EnemySpawn spawn = {3.5f, 2.5f, type, true, -1};
    enemy_init(&e, &spawn);
    CHECK(game_is_blocked(g, 4.5f, 2.5f, e.radius, &e) == (type != ENEMY_BAT));
    if (type == ENEMY_SPIDER) {
      e.flying = true;
      CHECK(!game_is_blocked(g, 4.5f, 2.5f, e.radius, &e));
    }
  }
  CHECK(game_line_of_sight(g, 1.5f, 2.5f, 9.5f, 2.5f));
  CHECK(game_line_of_sight(g, 9.5f, 2.5f, 1.5f, 2.5f));
  Input in = {.weapon = -1};
  for (int trap = 0; trap < 2; trap++) {
    g->projectile_count = 1;
    g->projectiles[0] = (Projectile){3.5f, 2.5f, 1, 0, 0, 0.44f, trap ? SHOT_TRAP : SHOT_MISS, NULL, 0, 0};
    game_update(g, 0.2f, &in);
    CHECK(g->projectile_count == 1 && g->projectiles[0].x > 5);
  }
  g->projectile_count = 0;
  int hp = g->enemies[0].hp;
  game_loose_bolt(g, 1.5f, 2.5f, 1, 0, SHOT_AT_ENEMY, &g->enemies[0], 12);
  game_update(g, 0.5f, &in);
  CHECK(g->enemies[0].hp == hp - 12);

  // The use ray and weapon ray also pass across the opening.
  g->player.x = 3.5f;
  g->player.keys = 3;
  f.level.grid[2 * f.level.w + 5] = 'X';
  in.use = true;
  game_update(g, 0.01f, &in);
  CHECK(g->completed && !g->player_dead);
  g->completed = false;
  f.level.grid[2 * f.level.w + 5] = '8';
  g->wall_health[2 * f.level.w + 5] = BREAKABLE_WALL_HEALTH;
  in.fire = true;
  game_update(g, 0.01f, &in);
  in.fire = false;
  game_update(g, 0.15f, &in);
  CHECK(g->wall_health[2 * f.level.w + 5] == BREAKABLE_WALL_HEALTH - WALL_BOLT_DAMAGE);
  teardown(&f);
}

static void test_save_load(void) {
  Fixture f;
  setup(&f);
  char path[1024], err[128];
  int len = snprintf(path, sizeof path, "%stest-pit-save.dat", SDL_GetBasePath());
  CHECK(len > 0 && (size_t)len < sizeof path);
  f.game->player.x = 3.5f;
  CHECK(savegame_write(path, f.game, err, sizeof err));
  Input in = {.weapon = -1, .forward = true};
  game_update(f.game, 0.5f, &in);
  CHECK(f.game->player_dead && !savegame_write(path, f.game, err, sizeof err));
  CHECK(savegame_load(path, &f.level, 1, &f.game, &f.session, err, sizeof err));
  CHECK(!f.game->player_dead && f.game->pit_fall == 0 && f.game->player.health == 100);
  CHECK(f.game->player.x == 3.5f && game_cell(f.game, 4, 2) == '!');
  CHECK(SDL_RemovePath(path));
  teardown(&f);
}

static void screenshot(const Image *image, const char *path) {
  SDL_Surface *surface = SDL_CreateSurfaceFrom(image->w, image->h, SDL_PIXELFORMAT_ARGB8888,
                                              image->px, image->w * (int)sizeof(uint32_t));
  CHECK(surface && SDL_SaveBMP(surface, path));
  SDL_DestroySurface(surface);
}

static void test_rendering(bool screenshots) {
  Fixture f;
  setup(&f);
  f.game->player.weapon = WEAPON_DAGGER;
  Assets assets;
  assets_build(&assets);
  Renderer renderer;
  renderer_init(&renderer, &assets);
  Image *fb = image_new(SCREEN_W, SCREEN_H);
  Image *floor = image_new(SCREEN_W, SCREEN_H);
  render_view(&renderer, fb, f.game);
  CHECK(fabsf(renderer.zbuffer[SCREEN_W / 2] - 8.5f) < 0.01f);
  if (screenshots) {
    render_hud(&renderer, fb, f.game);
    screenshot(fb, "build/pit-view.bmp");
  }
  f.level.grid[2 * f.level.w + 4] = '.';
  render_view(&renderer, floor, f.game);
  int changed = 0;
  for (int y = 190; y < 265; y++)
    for (int x = 240; x < 380; x++)
      if (floor->px[y * SCREEN_W + x] != fb->px[y * SCREEN_W + x]) changed++;
  CHECK(changed > 500);
  f.level.grid[2 * f.level.w + 3] = '1';
  render_view(&renderer, floor, f.game);
  f.level.grid[2 * f.level.w + 4] = '!';
  render_view(&renderer, fb, f.game);
  CHECK(!memcmp(floor->px, fb->px, sizeof(uint32_t) * SCREEN_W * VIEW_H)); // hidden behind masonry
  f.level.grid[2 * f.level.w + 3] = '.';
  f.game->visited[2 * f.level.w + 4] = 1;
  render_map(&renderer, fb, f.game, NULL);
  int cs = 8;
  int mx = SCREEN_W / 2 - f.level.w * cs / 2 + 4 * cs + cs / 2;
  int my = (VIEW_H - f.level.h * cs) / 2 + 2 * cs + cs / 2;
  CHECK(fb->px[my * SCREEN_W + mx] == 0xff080604u);

  f.game->pit_fall = PIT_FALL_TIME * 0.5f;
  render_view(&renderer, fb, f.game);
  if (screenshots) screenshot(fb, "build/pit-fall.bmp");
  f.game->pit_fall = PIT_FALL_TIME;
  render_view(&renderer, fb, f.game);
  CHECK(fb->px[SCREEN_W * 100 + 100] == 0xff000000u);
  image_free(floor);
  image_free(fb);
  renderer_free(&renderer);
  teardown(&f);
}

static void test_shipped_levels(void) {
  static const int dirs[4][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};
  for (int n = 1; n <= LEVEL_COUNT; n++) {
    char path[32], text[8192], err[128];
    snprintf(path, sizeof path, "levels/%d.txt", n);
    FILE *file = fopen(path, "rb");
    CHECK(file);
    size_t len = fread(text, 1, sizeof text - 1, file);
    CHECK(!ferror(file) && feof(file));
    fclose(file);
    text[len] = 0;
    Level lv;
    CHECK(level_parse(&lv, text, err, sizeof err));
    CHECK(lv.pit_count >= 1 && lv.key_count == 3);
    Session session = {.next_life = 40000};
    Game *g = game_create(&lv, n - 1, &session, NULL);
    g->enemy_count = 0;
    for (int i = 0; i < g->door_count; i++) g->doors[i].open = 1;
    for (int i = 0; i < g->barrel_count; i++)
      if (g->barrels[i].powder) g->barrels[i].destroyed = true;
    int *queue = malloc(sizeof(int) * lv.w * lv.h);
    uint8_t *seen = calloc((size_t)lv.w * lv.h, 1);
    int start = (int)g->player.y * lv.w + (int)g->player.x, count = 1;
    queue[0] = start;
    seen[start] = 1;
    for (int next = 0; next < count; next++) {
      int at = queue[next], x = at % lv.w, y = at / lv.w;
      g->player.x = x + .5f; g->player.y = y + .5f;
      for (int d = 0; d < 4; d++) {
        int nx = x + dirs[d][0], ny = y + dirs[d][1];
        if (nx < 0 || ny < 0 || nx >= lv.w || ny >= lv.h) continue;
        int cell = ny * lv.w + nx;
        if (seen[cell] || lv.grid[cell] == '!' || game_is_blocked(g, nx + 0.5f, ny + 0.5f, g->player.radius, NULL)) continue;
        seen[cell] = 1;
        queue[count++] = cell;
      }
    }
    for (int i = 0; i < lv.item_count; i++) {
      if (lv.items[i].kind != ITEM_KEY) continue;
      int cell = (int)lv.items[i].y * lv.w + (int)lv.items[i].x;
      if (!seen[cell]) fprintf(stderr, "Unreachable key on floor %d at %d,%d\n", n, cell % lv.w, cell / lv.w);
      CHECK(seen[cell]);
    }
    for (int y = 0; y < lv.h; y++) {
      for (int x = 0; x < lv.w; x++) {
        char c = lv.grid[y * lv.w + x];
        if (c != '!' && c != 'X') continue;
        bool accessible = false;
        for (int d = 0; d < 4; d++) {
          int nx = x + dirs[d][0], ny = y + dirs[d][1];
          if (nx >= 0 && ny >= 0 && nx < lv.w && ny < lv.h && seen[ny * lv.w + nx]) accessible = true;
        }
        if (!accessible) fprintf(stderr, "Unreachable %c on floor %d at %d,%d\n", c, n, x, y);
        CHECK(accessible);
        if (c == '!') {
          for (int i = 0; i < lv.decoration_count; i++) {
            const Decoration *deco = &lv.decorations[i];
            CHECK((int)floor(deco->x) != x || (int)floor(deco->y) != y);
          }
        }
      }
    }
    free(queue);
    free(seen);
    game_destroy(g);
    level_free(&lv);
  }
}

int main(int argc, char **argv) {
  CHECK(SDL_Init(0));
  random_seed(12345);
  test_falling();
  test_enemies_and_arrows();
  test_save_load();
  test_shipped_levels();
  test_rendering(argc > 1 && !strcmp(argv[1], "--screenshots"));
  SDL_Quit();
  puts("Pit trap checks passed.");
  return 0;
}
