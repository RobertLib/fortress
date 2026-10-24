#include "game.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(expr) do { if (!(expr)) { \
  fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #expr); exit(1); \
} } while (0)

static const char *ARENA =
    "111111111111111\n"
    "1P............1\n"
    "1...Q.........1\n"
    "1GSOVCYW......1\n"
    "1.............1\n"
    "111111111111111\n";

typedef struct Fixture {
  Level level;
  Session session;
  Game *game;
} Fixture;

static void setup(Fixture *f, Difficulty difficulty) {
  memset(f, 0, sizeof *f);
  char err[128];
  CHECK(level_parse(&f->level, ARENA, err, sizeof err));
  f->level.decoration_count = 0;
  f->session.difficulty = difficulty;
  f->session.next_life = 40000;
  f->game = game_create(&f->level, 0, &f->session, NULL);
  // Isolate damage and pickups from enemy movement and attacks.
  for (int i = 0; i < f->game->enemy_count; i++) {
    f->game->enemies[i].state = ES_PAIN;
    f->game->enemies[i].timer = 100;
  }
}

static void teardown(Fixture *f) {
  game_destroy(f->game);
  level_free(&f->level);
}

static void tick(Game *g, int frames) {
  Input input = {.weapon = -1};
  for (int i = 0; i < frames; i++) game_update(g, 0.01f, &input);
}

static void test_starting_balance(void) {
  static const int hp[DIFFICULTY_COUNT][ENEMY_TYPE_COUNT] = {
      [DIFFICULTY_NORMAL] = {25, 60, 45, 16, 18, 8, 20},
      [DIFFICULTY_EASY] = {19, 45, 34, 12, 14, 6, 15},
      [DIFFICULTY_HARD] = {31, 75, 56, 20, 23, 10, 25},
  };
  static const int ammo[] = {16, 24, 12};
  Session defaults = {0};
  CHECK(defaults.difficulty == DIFFICULTY_NORMAL);
  for (int d = 0; d < DIFFICULTY_COUNT; d++) {
    Fixture f;
    setup(&f, (Difficulty)d);
    Game *g = f.game;
    CHECK(g->player.health == 100 && g->player.ammo == ammo[d]);
    CHECK(g->player.weapon == WEAPON_CROSSBOW && !g->player.has_arbalest);
    CHECK(g->enemy_count == ENEMY_TYPE_COUNT);
    for (int i = 0; i < g->enemy_count; i++)
      CHECK(g->enemies[i].hp == hp[d][g->enemies[i].type]);
    teardown(&f);
  }
}

static void test_damage(void) {
  static const int damage[] = {20, 10, 30};
  static const int minimum[] = {1, 1, 2};
  for (int d = 0; d < DIFFICULTY_COUNT; d++) {
    Fixture f;
    setup(&f, (Difficulty)d);
    Game *g = f.game;
    game_hurt_player(g, 0);
    game_hurt_player(g, -10);
    CHECK(g->player.health == 100 && g->damage_flash == 0);
    game_hurt_player(g, 20);
    CHECK(g->player.health == 100 - damage[d]);
    game_hurt_player(g, 1);
    CHECK(g->player.health == 100 - damage[d] - minimum[d]);
    g->completed = true;
    game_hurt_player(g, 1000);
    CHECK(!g->player_dead);
    g->completed = false;
    game_hurt_player(g, 1000);
    CHECK(g->player_dead && g->player.health == 0);
    teardown(&f);
  }
}

static void test_projectile_and_blast_damage(void) {
  static const int bolt_damage[] = {20, 10, 30};
  static const int blast_damage[] = {62, 31, 93};
  static const int trap_damage[] = {15, 8, 23};
  for (int d = 0; d < DIFFICULTY_COUNT; d++) {
    Fixture f;
    setup(&f, (Difficulty)d);
    Game *g = f.game;
    game_loose_bolt(g, 1.8f, 1.5f, -1, 0, SHOT_AT_PLAYER, NULL, 20);
    tick(g, 1);
    CHECK(g->projectile_count == 0 && g->player.health == 100 - bolt_damage[d]);

    g->player.health = 100;
    g->projectiles[g->projectile_count++] =
        (Projectile){1.8f, 1.5f, -1, 0, 0, 0.44f, SHOT_TRAP, NULL, 0, 0, WEAPON_CROSSBOW};
    random_seed(12345); // first roll produces a base trap hit of 15
    tick(g, 1);
    CHECK(g->projectile_count == 0 && g->player.health == 100 - trap_damage[d]);

    g->player.x = 4.5f;
    g->player.health = 100;
    game_hit_barrel(g, 0);
    CHECK(g->player.health == 100 - blast_damage[d]);
    teardown(&f);
  }
}

static void test_venom(void) {
  static const int venom[] = {8, 4, 12};
  for (int d = 0; d < DIFFICULTY_COUNT; d++) {
    Fixture f;
    setup(&f, (Difficulty)d);
    Game *g = f.game;
    game_poison_player(g, 8);
    CHECK(g->player.venom == venom[d]);
    tick(g, 600);
    CHECK(g->player.venom == 0 && g->player.health == 100 - venom[d]);
    g->player.health = 2;
    game_poison_player(g, 100);
    tick(g, 200);
    CHECK(g->player.health == 1 && !g->player_dead && g->player.venom == 0);
    teardown(&f);
  }
}

static void place_pickup(Game *g, ItemKind kind) {
  g->items[0] = (Item){g->player.x, g->player.y, kind};
  g->item_count = 1;
}

static void test_ammo_pickups(void) {
  static const ItemKind kinds[] = {ITEM_BOLTS, ITEM_BOLTS_DROP, ITEM_ARBALEST};
  static const int ammo[DIFFICULTY_COUNT][3] = {{8, 4, 20}, {12, 6, 30}, {6, 3, 15}};
  for (int d = 0; d < DIFFICULTY_COUNT; d++) {
    Fixture f;
    setup(&f, (Difficulty)d);
    Game *g = f.game;
    for (int k = 0; k < 3; k++) {
      g->player.ammo = 0;
      place_pickup(g, kinds[k]);
      tick(g, 1);
      CHECK(g->player.ammo == ammo[d][k] && g->item_count == 0);
      if (kinds[k] == ITEM_ARBALEST)
        CHECK(g->player.has_arbalest && g->player.weapon == WEAPON_ARBALEST);

      g->player.ammo = 98;
      place_pickup(g, kinds[k]);
      tick(g, 1);
      CHECK(g->player.ammo == 99);
    }
    place_pickup(g, ITEM_BOLTS);
    tick(g, 1);
    CHECK(g->player.ammo == 99 && g->item_count == 1);
    teardown(&f);
  }
}

static void test_retries_and_later_floors(void) {
  static const int ammo[] = {16, 24, 12};
  for (int d = 0; d < DIFFICULTY_COUNT; d++) {
    Fixture f;
    setup(&f, (Difficulty)d);
    int hp = f.game->enemies[0].hp;
    Persist persist = {61, 37, WEAPON_ARBALEST, true};
    game_destroy(f.game);
    f.game = game_create(&f.level, 1, &f.session, &persist);
    CHECK(f.game->player.health == 61 && f.game->player.ammo == 37);
    CHECK(f.game->player.weapon == WEAPON_ARBALEST && f.game->player.has_arbalest);
    CHECK(f.game->enemies[0].hp == hp);
    game_destroy(f.game);
    f.game = game_create(&f.level, 1, &f.session, NULL);
    CHECK(f.session.difficulty == (Difficulty)d);
    CHECK(f.game->player.health == 100 && f.game->player.ammo == ammo[d]);
    CHECK(f.game->player.weapon == WEAPON_CROSSBOW && !f.game->player.has_arbalest);
    CHECK(f.game->enemies[0].hp == hp);
    teardown(&f);
  }
}

int main(void) {
  random_seed(12345);
  test_starting_balance();
  test_damage();
  test_projectile_and_blast_damage();
  test_venom();
  test_ammo_pickups();
  test_retries_and_later_floors();
  puts("Difficulty checks passed.");
  return 0;
}
