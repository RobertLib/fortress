#include "game.h"

#include <stdio.h>
#include <stdlib.h>

#define CHECK(expr) do { if (!(expr)) { \
  fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #expr); exit(1); \
} } while (0)

static Level level;
static Session session;
static Game *game;

static void fresh_game(WeaponKind weapon, int ammo) {
  game_destroy(game);
  session = (Session){.lives = 3, .next_life = 40000};
  game = game_create(&level, 0, &session, NULL);
  game->player.weapon = weapon;
  game->player.has_arbalest = true;
  game->player.ammo = ammo;
}

static void tick(int frames, bool fire) {
  Input input = {.weapon = -1, .fire = fire};
  for (int i = 0; i < frames; i++) game_update(game, 0.01f, &input);
}

static void switch_weapon(WeaponKind weapon) {
  Input input = {.weapon = weapon};
  game_update(game, 0.01f, &input);
}

static void test_three_separate_bolts(void) {
  fresh_game(WEAPON_ARBALEST, 16);
  tick(1, true);
  CHECK(game->player.ammo == 15 && game->projectile_count == 1);
  tick(35, false);
  CHECK(game->player.ammo == 15);
  tick(7, false);
  CHECK(game->player.ammo == 14);
  tick(35, false);
  CHECK(game->player.ammo == 14);
  tick(7, false);
  CHECK(game->player.ammo == 13);
  tick(100, false);
  CHECK(game->player.ammo == 13);

  tick(1, true);
  CHECK(game->player.ammo == 12);
  tick(150, true);
  CHECK(game->player.ammo == 10);
}

static void test_retrigger_and_recovery(void) {
  fresh_game(WEAPON_ARBALEST, 16);
  tick(1, true);
  tick(1, false);
  tick(1, true); // another press cannot restart or extend an active burst
  tick(85, false);
  CHECK(game->player.ammo == 13);
  tick(1, true); // still recovering after the third bolt
  CHECK(game->player.ammo == 13);
  tick(60, true);
  CHECK(game->player.ammo == 13);
  tick(1, false);
  tick(1, true);
  CHECK(game->player.ammo == 12);
  tick(85, false);
  CHECK(game->player.ammo == 10);
}

static void test_low_ammunition(void) {
  for (int ammo = 0; ammo < 3; ammo++) {
    fresh_game(WEAPON_ARBALEST, ammo);
    tick(100, true);
    CHECK(game->player.ammo == 0 && game->burst_remaining == 0);
    game->player.ammo = 3;
    tick(100, true); // replenishing ammo cannot resume a finished burst
    CHECK(game->player.ammo == 3);
    tick(1, false);
    tick(1, true);
    tick(85, false);
    CHECK(game->player.ammo == 0 && game->burst_remaining == 0);
  }
}

static void test_weapon_switch_cancels_burst(void) {
  fresh_game(WEAPON_ARBALEST, 16);
  tick(1, true);
  switch_weapon(WEAPON_CROSSBOW);
  tick(100, false);
  CHECK(game->player.weapon == WEAPON_CROSSBOW && game->player.ammo == 15);
  switch_weapon(WEAPON_ARBALEST);
  tick(100, false);
  CHECK(game->player.ammo == 15);
  tick(1, true);
  tick(85, false);
  CHECK(game->player.ammo == 12);

  fresh_game(WEAPON_ARBALEST, 16);
  tick(1, true);
  switch_weapon(WEAPON_ARBALEST); // selecting the same weapon keeps its burst
  tick(100, false);
  CHECK(game->player.ammo == 13);
}

static void test_single_shot_weapons(void) {
  fresh_game(WEAPON_CROSSBOW, 16);
  tick(100, true);
  CHECK(game->player.ammo == 15);
  tick(1, false);
  tick(1, true);
  CHECK(game->player.ammo == 14);

  fresh_game(WEAPON_DAGGER, 16);
  tick(100, true);
  CHECK(game->player.ammo == 16 && game->projectile_count == 0);
  CHECK(game->fire_timer == 0);
  tick(1, false);
  tick(1, true);
  CHECK(game->fire_timer > 0);
}

int main(void) {
  char err[128];
  CHECK(level_parse(&level,
      "1111111111\n"
      "1P.......1\n"
      "1........1\n"
      "1111111111\n", err, sizeof err));
  level.decoration_count = 0;
  test_three_separate_bolts();
  test_retrigger_and_recovery();
  test_low_ammunition();
  test_weapon_switch_cancels_burst();
  test_single_shot_weapons();
  game_destroy(game);
  level_free(&level);
  puts("Three-bolt bursts, trigger recovery, ammunition and weapon switching checks passed.");
  return 0;
}
