#include "game.h"
#include <SDL3/SDL.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(expr) do { if (!(expr)) { \
  fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #expr); exit(1); \
} } while (0)

static const char *ARENA =
    "111111111111111\n"
    "1P............1\n"
    "1...Q..q......1\n"
    "1.............1\n"
    "1..g..........1\n"
    "1.............1\n"
    "111111111111111\n";

typedef struct Fixture {
  Level level;
  Session session;
  Game *game;
} Fixture;

static void setup(Fixture *f) {
  memset(f, 0, sizeof *f);
  char err[128];
  CHECK(level_parse(&f->level, ARENA, err, sizeof err));
  // Combat fixtures isolate barrel collisions from procedural furniture.
  f->level.decoration_count = 0;
  f->session.next_life = 40000;
  f->game = game_create(&f->level, 0, &f->session, NULL);
  f->game->enemies[0].state = ES_PAIN;
  f->game->enemies[0].timer = 100;
}

static void teardown(Fixture *f) {
  game_destroy(f->game);
  level_free(&f->level);
}

static void tick(Game *g, int frames) {
  Input input = {.weapon = -1};
  for (int i = 0; i < frames; i++) game_update(g, 0.01f, &input);
}

static void shoot(Game *g) {
  Input input = {.weapon = -1, .fire = true};
  game_update(g, 0.01f, &input);
}

static void aim_along_row(Game *g, float x) {
  g->player.x = x;
  g->player.y = 2.5f;
  g->player.dir_x = 1;
  g->player.dir_y = 0;
  g->player.plane_x = 0;
  g->player.plane_y = 0.66f;
}

static void test_shooting_and_reset(void) {
  Fixture f;
  setup(&f);
  Game *g = f.game;
  CHECK(g->barrel_count == 2 && g->barrels[0].powder && !g->barrels[1].powder);
  aim_along_row(g, 1.5f);
  shoot(g);
  CHECK(g->player.ammo == 15 && !g->barrels[0].destroyed);
  tick(g, 18);
  CHECK(g->barrels[0].destroyed && !g->barrels[1].destroyed);
  CHECK(g->player.health < 100 && g->barrels[0].blast_timer > 0);
  int health = g->player.health, kills = g->kills, score = f.session.score;
  game_hit_barrel(g, 0);
  CHECK(g->player.health == health && g->kills == kills && f.session.score == score);
  game_destroy(g);
  f.game = game_create(&f.level, 0, &f.session, NULL);
  CHECK(!f.game->barrels[0].destroyed && f.game->barrels[0].blast_timer == 0);
  teardown(&f);
}

static void test_ordinary_barrel_stops_bolts(void) {
  Fixture f;
  setup(&f);
  Game *g = f.game;
  g->barrels[0].powder = false;
  aim_along_row(g, 1.5f);
  g->enemies[0].x = 5.5f;
  g->enemies[0].y = 2.5f;
  int hp = g->enemies[0].hp;
  shoot(g);
  tick(g, 30);
  CHECK(!g->barrels[0].destroyed && g->barrels[0].blast_timer == 0);
  CHECK(g->enemies[0].hp == hp && g->player.health == 100 && g->projectile_count == 0);
  CHECK(game_is_blocked(g, 4.5f, 2.5f, g->player.radius, NULL));
  teardown(&f);
}

static void test_nearest_target_and_dagger(void) {
  Fixture f;
  setup(&f);
  Game *g = f.game;
  aim_along_row(g, 1.5f);
  g->enemies[0].x = 3;
  g->enemies[0].y = 2.5f;
  int hp = g->enemies[0].hp;
  shoot(g);
  tick(g, 15);
  CHECK(g->enemies[0].hp < hp && !g->barrels[0].destroyed);
  g->player.weapon = WEAPON_DAGGER;
  g->player.x = 3.7f;
  g->fire_timer = 0;
  g->fire_held = false;
  shoot(g);
  CHECK(!g->barrels[0].destroyed && g->player.ammo == 15);
  teardown(&f);
}

static void test_blast_range_and_cover(void) {
  for (int scenario = 0; scenario < 7; scenario++) {
    Fixture f;
    setup(&f);
    Game *g = f.game;
    // The blast is two tiles from both actors, with a cell between them.
    g->player.x = g->enemies[0].x = scenario == 0 ? 9.5f : 6.5f;
    g->player.y = g->enemies[0].y = 2.5f;
    char cover = scenario == 2 ? '1' : scenario == 3 || scenario == 5 ? 'D'
                 : scenario == 4 ? 'B' : scenario == 6 ? 'R' : '.';
    f.level.grid[2 * f.level.w + 5] = cover;
    if (scenario == 5) {
      g->doors[0] = (Door){5, 2, 'D', 0, 1, DOOR_OPEN, 4};
      g->door_at[2 * f.level.w + 5] = 0;
      g->door_count = 1;
    }
    int hp = g->enemies[0].hp;
    game_hit_barrel(g, 0);
    bool exposed = scenario == 1 || scenario == 4 || scenario == 5 || scenario == 6;
    CHECK((g->player.health < 100) == exposed);
    CHECK((g->enemies[0].hp < hp) == exposed);
    teardown(&f);
  }
  Fixture f;
  setup(&f);
  Game *g = f.game;
  g->player.x = 5;
  g->player.y = 2.5f;
  g->enemies[0].x = 7;
  g->enemies[0].y = 2.5f;
  g->enemies[0].hp = 100;
  game_hit_barrel(g, 0);
  CHECK(100 - g->player.health > 100 - g->enemies[0].hp);
  teardown(&f);
}

static void test_chain_kills_and_debris(void) {
  Fixture f;
  setup(&f);
  Game *g = f.game;
  g->barrels[1].powder = true;
  g->barrels[1].x = 6.5f;
  g->enemies[0].x = 5.5f;
  g->enemies[0].y = 2.5f;
  CHECK(game_is_blocked(g, 4.5f, 2.5f, g->player.radius, NULL));
  game_hit_barrel(g, 0);
  CHECK(g->barrels[0].destroyed && g->barrels[1].destroyed);
  CHECK(enemy_dead(&g->enemies[0]) && g->kills == 1 && f.session.total_kills == 1);
  CHECK(f.session.score == g->enemies[0].stats->score);
  CHECK(!game_is_blocked(g, 4.5f, 2.5f, g->player.radius, NULL));
  g->player.x = 5;
  g->player.y = 2.5f;
  g->player.health = 10;
  // Reset just one test barrel to check that a close blast can be fatal.
  g->barrels[0].destroyed = false;
  game_hit_barrel(g, 0);
  CHECK(g->player_dead && g->player.health == 0);
  tick(g, 100);
  CHECK(g->barrels[0].blast_timer == 0 && g->barrels[1].blast_timer == 0);
  teardown(&f);
}

static void test_other_arrows(void) {
  for (int trap = 0; trap < 2; trap++) {
    Fixture f;
    setup(&f);
    Game *g = f.game;
    aim_along_row(g, 6.5f);
    if (trap)
      g->projectiles[g->projectile_count++] = (Projectile){1.5f, 2.5f, 1, 0, 0, 0.44f, SHOT_TRAP, NULL, 0, 0};
    else
      game_loose_bolt(g, 1.5f, 2.5f, 1, 0, SHOT_AT_PLAYER, NULL, 15);
    tick(g, 40);
    CHECK(g->barrels[0].destroyed && g->projectile_count == 0);
    teardown(&f);
  }
}

static void test_full_projectile_pool(void) {
  Fixture f;
  setup(&f);
  Game *g = f.game;
  aim_along_row(g, 1.5f);
  g->projectile_count = MAX_PROJECTILES;
  for (int i = 0; i < MAX_PROJECTILES; i++)
    g->projectiles[i] = (Projectile){1.5f, 1.5f, 0, -1, 0, 0.44f, SHOT_MISS, NULL, 0, 0};
  shoot(g);
  CHECK(g->barrels[0].destroyed);
  teardown(&f);
}

// A permanent ordinary barrel must never cut off an existing route to loot
// or keys. Use the real movement collision with enemies cleared and doors open.
static void reachable_cells(Game *g, uint8_t *seen) {
  const Level *lv = g->level;
  float original_x = g->player.x, original_y = g->player.y;
  int *queue = malloc(sizeof(int) * lv->w * lv->h);
  int start = (int)g->player.y * lv->w + (int)g->player.x;
  int count = 1;
  queue[0] = start;
  seen[start] = 1;
  static const int dirs[4][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};
  for (int next = 0; next < count; next++) {
    int at = queue[next], x = at % lv->w, y = at / lv->w;
    g->player.x = x + .5f; g->player.y = y + .5f;
    for (int d = 0; d < 4; d++) {
      int nx = x + dirs[d][0], ny = y + dirs[d][1];
      if (nx < 0 || ny < 0 || nx >= lv->w || ny >= lv->h) continue;
      int cell = ny * lv->w + nx;
      if (seen[cell] || game_is_blocked(g, nx + 0.5f, ny + 0.5f, g->player.radius, NULL)) continue;
      seen[cell] = 1;
      queue[count++] = cell;
    }
  }
  g->player.x = original_x; g->player.y = original_y;
  free(queue);
}

static void test_shipped_levels(void) {
  for (int n = 1; n <= LEVEL_COUNT; n++) {
    char path[32];
    snprintf(path, sizeof path, "levels/%d.txt", n);
    char err[128];
    char *text = SDL_LoadFile(path, NULL); CHECK(text);
    Level lv;
    CHECK(level_parse(&lv, text, err, sizeof err));
    SDL_free(text);
    int powder = 0, ordinary = 0;
    for (int i = 0; i < lv.barrel_count; i++) {
      const BarrelSpawn *b = &lv.barrels[i];
      if (b->powder) powder++; else ordinary++;
      for (int j = 0; j < lv.decoration_count; j++) {
        const Decoration *d = &lv.decorations[j];
        CHECK(!d->radius || hypot(d->x - b->x, d->y - b->y) > d->radius + BARREL_RADIUS);
      }
    }
    CHECK(powder > 0 && ordinary > 0 && lv.key_count == 3);
    Session session = {.next_life = 40000};
    Game *g = game_create(&lv, n - 1, &session, NULL);
    g->enemy_count = 0;
    for (int i = 0; i < g->door_count; i++) g->doors[i].open = 1;
    uint8_t *before = calloc((size_t)lv.w * lv.h, 1), *after = calloc((size_t)lv.w * lv.h, 1);
    int count = g->barrel_count;
    g->barrel_count = 0;
    reachable_cells(g, before);
    g->barrel_count = count;
    reachable_cells(g, after);
    for (int i = 0; i < lv.item_count; i++) {
      int cell = (int)lv.items[i].y * lv.w + (int)lv.items[i].x;
      CHECK(!before[cell] || after[cell]);
    }
    free(before);
    free(after);
    game_destroy(g);
    level_free(&lv);
  }
}

int main(void) {
  random_seed(12345);
  test_shooting_and_reset();
  test_ordinary_barrel_stops_bolts();
  test_nearest_target_and_dagger();
  test_blast_range_and_cover();
  test_chain_kills_and_debris();
  test_other_arrows();
  test_full_projectile_pool();
  test_shipped_levels();
  puts("Barrel combat checks passed.");
  return 0;
}
