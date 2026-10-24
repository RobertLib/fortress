#include "savegame.h"

#include <SDL3/SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(expr) do { if (!(expr)) { \
  fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #expr); exit(1); \
} } while (0)

static const char *SLOT = "test-savegame.dat";
static const char *COPY = "test-savegame-copy.dat";
static const char *BAD = "test-savegame-bad.dat";
static const char *ARENA =
    "11111111111111111\n"
    "1P..K.A.........1\n"
    "1..Q..q.........1\n"
    "1.GSCVYw........1\n"
    "1........D......1\n"
    "1L1Z11111111R1111\n"
    "1....^..........1\n"
    "1.H.T.M.~...~.X.1\n"
    "11111111111111111\n";

static Level levels[2];
static Session session;
static Game *game;
static char err[96];

static void fresh_game(int floor, Difficulty difficulty) {
  game_destroy(game);
  session = (Session){.difficulty = difficulty, .lives = 3, .next_life = 40000};
  game = game_create(&levels[floor], floor, &session, NULL);
}

static void write_file(const char *path, const void *data, size_t size) {
  CHECK(SDL_SaveFile(path, data, size));
}

static void same_files(const char *a, const char *b) {
  size_t a_size, b_size;
  void *a_data = SDL_LoadFile(a, &a_size), *b_data = SDL_LoadFile(b, &b_size);
  CHECK(a_data && b_data && a_size == b_size && !memcmp(a_data, b_data, a_size));
  SDL_free(a_data); SDL_free(b_data);
}

static void save(void) { CHECK(savegame_write(SLOT, game, err, sizeof err)); }

static void load(void) {
  CHECK(savegame_load(SLOT, levels, 2, &game, &session, err, sizeof err));
  CHECK(game->session == &session && game->level == &levels[game->level_index]);
}

static void test_full_snapshot(void) {
  fresh_game(1, DIFFICULTY_HARD);
  CHECK(game->door_count == 3 && game->trap_count == 1 && levels[1].lever_count == 1);
  session.score = 12345;
  session.lives = 2;
  session.total_kills = 6;
  session.total_treasure = 3;
  session.total_enemies = 12;
  session.max_treasure = 5;
  session.total_secrets_found = 1;
  session.max_secrets = 2;
  session.total_time = 123.5f;
  game->player.x = 2.25f;
  game->player.health = 61;
  game->player.ammo = 37;
  game->player.weapon = WEAPON_ARBALEST;
  game->player.has_arbalest = true;
  game->player.keys = 1;
  game->player.venom = 9;
  game->player.venom_timer = 0.4f;
  game->player.speed_now = 7; // sprinting diagonally
  game->enemies[0].state = ES_DEAD;
  game->enemies[0].hp = -7;
  game->enemies[1].state = ES_PAIN;
  game->enemies[1].timer = 0.25f;
  game->enemies[2].home_x = 4.75f; // hounds remember a fallen handler's position
  game->enemies[2].home_y = 4.25f;
  game->enemies[2].dir_x = 1.16f; // the escape heading after a bite is not normalised
  game->enemies[2].has_last_seen = true;
  game->enemies[2].last_seen_x = 2.25f;
  game->enemies[2].last_seen_y = 1.5f;
  game->barrels[0].destroyed = true;
  game->barrels[0].blast_timer = 0.3f;
  game->doors[0].state = DOOR_OPENING;
  game->doors[0].open = 0.45f;
  game->doors[1].state = DOOR_OPEN;
  game->doors[1].open = 1;
  game->doors[1].timer = 3;
  game->traps[0].state = TRAP_FIRING;
  game->traps[0].timer = 0.1f;
  game->traps[0].shots = 2;
  game->traps[0].seen = true;
  game->lever_pulled[0] = true;
  game->visited[20] = 1;
  game->time = 75.25f;
  game->kills = game->treasure = game->secrets_found = 1;
  game->show_map = true;
  game->fire_timer = 0.2f;
  game->fire_held = true;
  game->burst_remaining = 2;
  game->weapon_anim_timer = 0.12f;
  game->player.weapon_frame = 1;
  game->items[0] = (Item){8.5f, 6.5f, ITEM_BOLTS_DROP};
  game->item_count = 1;
  game_say(game, "SNAPSHOT", 2);
  game_loose_bolt(game, 4, 3.5f, -1, 0, SHOT_AT_ENEMY, &game->enemies[1], 11);
  game_loose_bolt(game, 4, 3.5f, -1, 0, SHOT_AT_PLAYER, NULL, 8);
  game->projectiles[game->projectile_count++] = (Projectile){5, 6.5f, 1, 0, 1, 0.44f, SHOT_TRAP, NULL, 0, 0};
  Session expected_session = session;
  uint64_t rng = random_state();
  save();

  // A cold load: no live Game or Session survives from the saved run.
  game_destroy(game);
  game = NULL;
  memset(&session, 0, sizeof session);
  random_seed(17);
  load();
  CHECK(!memcmp(&session, &expected_session, sizeof session));
  CHECK(random_state() == rng && game->level_index == 1);
  CHECK(game->player.health == 61 && game->player.ammo == 37 && game->player.keys == 1);
  CHECK(game->player.weapon == WEAPON_ARBALEST && game->player.has_arbalest);
  CHECK(game->player.venom == 9 && game->player.venom_timer == 0.4f && game->player.speed_now == 7);
  CHECK(game->enemies[0].state == ES_DEAD && game->enemies[0].hp == -7);
  CHECK(game->enemies[1].stats && game->enemies[1].state == ES_PAIN);
  CHECK(game->enemies[2].home_x == 4.75f && game->enemies[2].home_y == 4.25f);
  CHECK(game->enemies[2].dir_x == 1.16f);
  CHECK(game->barrels[0].destroyed && game->barrels[0].blast_timer == 0.3f);
  CHECK(game->doors[0].state == DOOR_OPENING && game->doors[0].open == 0.45f);
  CHECK(game_door_at(game, game->doors[1].x, game->doors[1].y) == &game->doors[1]);
  CHECK(game->traps[0].spawn == &levels[1].traps[0] && game->traps[0].shots == 2);
  CHECK(game->lever_pulled[0] && game->visited[20] && game->show_map);
  CHECK(game->item_count == 1 && game->items[0].kind == ITEM_BOLTS_DROP);
  CHECK(game->projectile_count == 3 && game->projectiles[0].target == &game->enemies[1]);
  CHECK(game->time == 75.25f && game->kills == 1 && game->secrets_found == 1);
  CHECK(!strcmp(game->message, "SNAPSHOT") && game->message_timer == 2);
  CHECK(game->burst_remaining == 2 && game->fire_held && game->fire_timer == 0.2f);
  CHECK(savegame_write(COPY, game, err, sizeof err));
  same_files(SLOT, COPY);

  // Advancing a restored combat snapshot must make exactly the same choices.
  Input input = {.weapon = -1};
  for (int i = 0; i < 70; i++) game_update(game, 0.01f, &input);
  CHECK(game->player.ammo == 35 && game->burst_remaining == 0);
  CHECK(savegame_write(COPY, game, err, sizeof err));
  load();
  for (int i = 0; i < 70; i++) game_update(game, 0.01f, &input);
  save();
  same_files(SLOT, COPY);
}

static void reject_load(const char *path) {
  Game *previous = game;
  Session previous_session = session;
  Player previous_player = game->player;
  uint64_t rng = random_state();
  CHECK(!savegame_load(path, levels, 2, &game, &session, err, sizeof err));
  CHECK(err[0] && game == previous && random_state() == rng);
  CHECK(!memcmp(&session, &previous_session, sizeof session));
  CHECK(game->player.health == previous_player.health && game->player.x == previous_player.x);
}

static void rehash(uint8_t *data, size_t size) {
  uint32_t hash = 2166136261u;
  for (size_t i = 0; i < size - 4; i++) hash = (hash ^ data[i]) * 16777619u;
  for (int i = 0; i < 4; i++) data[size - 4 + i] = (uint8_t)(hash >> (8 * i));
}

static void test_legacy_repeater_save(void) {
  fresh_game(0, DIFFICULTY_NORMAL);
  game->player.weapon = WEAPON_ARBALEST;
  game->player.has_arbalest = true;
  game->fire_held = true;
  save();
  size_t size;
  uint8_t *data = SDL_LoadFile(SLOT, &size);
  CHECK(data && size > 8 && data[8] == 6);
  // Version 5 ends before the new pending-burst count.
  data[8] = 5;
  size -= 4;
  rehash(data, size);
  write_file(SLOT, data, size);
  SDL_free(data);
  load();
  CHECK(game->burst_remaining == 0 && game->player.weapon == WEAPON_ARBALEST);
  Input input = {.weapon = -1, .fire = true};
  for (int i = 0; i < 30; i++) game_update(game, 0.01f, &input);
  CHECK(game->player.ammo == 16);
}

static void test_bad_files_and_failed_save(void) {
  fresh_game(0, DIFFICULTY_NORMAL);
  save();
  CHECK(savegame_write(COPY, game, err, sizeof err));
  reject_load("no-such-savegame-file.dat");
  size_t size;
  uint8_t *original = SDL_LoadFile(SLOT, &size);
  CHECK(original);
  uint8_t *data = malloc(size);
  CHECK(data);
  // Unsupported version, invalid level, difficulty, score, NaN position,
  // invalid item and burst counts. Recompute checksums to exercise field validation too.
  const size_t offsets[] = {8, 12, 28, 32, 72, 140, size - 8};
  for (size_t i = 0; i < sizeof offsets / sizeof offsets[0]; i++) {
    memcpy(data, original, size);
    memset(data + offsets[i], 0xff, 4);
    rehash(data, size);
    write_file(BAD, data, size);
    reject_load(BAD);
  }
  memcpy(data, original, size);
  data[size / 2] ^= 0x40;
  write_file(BAD, data, size);
  reject_load(BAD);
  write_file(BAD, original, 7);
  reject_load(BAD);
  // Valid checksum but a truncated body: failure occurs after staging a Game.
  memcpy(data, original, size);
  rehash(data, size - 8);
  write_file(BAD, data, size - 8);
  reject_load(BAD);

  char old = levels[0].grid[0];
  levels[0].grid[0] = '2';
  reject_load(SLOT);
  levels[0].grid[0] = old;
  ItemKind kind = levels[0].items[0].kind;
  levels[0].items[0].kind = ITEM_BREAD;
  reject_load(SLOT);
  levels[0].items[0].kind = kind;

  CHECK(SDL_CreateDirectory("test-savegame.dat.tmp"));
  game->player.health = 52;
  CHECK(!savegame_write(SLOT, game, err, sizeof err));
  CHECK(game->player.health == 52);
  same_files(SLOT, COPY);
  CHECK(SDL_RemovePath("test-savegame.dat.tmp"));
  game->player_dead = true;
  CHECK(!savegame_write(SLOT, game, err, sizeof err));
  same_files(SLOT, COPY);
  game->player_dead = false;
  game->completed = true;
  CHECK(!savegame_write(SLOT, game, err, sizeof err));
  same_files(SLOT, COPY);
  load(); // recover the previously saved living run
  CHECK(game->player.health == 100 && !game->player_dead && !game->completed);
  game->player.health = 42;
  save(); // replacing an existing slot must also work
  load();
  CHECK(game->player.health == 42);
  SDL_free(original);
  free(data);
}

static void test_all_floors(void) {
  for (int floor = 0; floor < LEVEL_COUNT; floor++) {
    char path[64], parse_err[128];
    snprintf(path, sizeof path, "levels/%d.txt", floor + 1);
    char *text = SDL_LoadFile(path, NULL);
    Level level;
    CHECK(text && level_parse(&level, text, parse_err, sizeof parse_err));
    SDL_free(text);
    for (int d = 0; d < DIFFICULTY_COUNT; d++) {
      Session s = {.difficulty = (Difficulty)d, .lives = 3, .next_life = 40000};
      Game *g = game_create(&level, 0, &s, NULL);
      CHECK(savegame_write(SLOT, g, err, sizeof err));
      CHECK(savegame_load(SLOT, &level, 1, &g, &s, err, sizeof err));
      CHECK(s.difficulty == (Difficulty)d && g->player.health == 100);
      game_destroy(g);
    }
    level_free(&level);
  }
}

int main(void) {
  random_seed(12345);
  for (int i = 0; i < 2; i++) CHECK(level_parse(&levels[i], ARENA, err, sizeof err));
  test_full_snapshot();
  test_legacy_repeater_save();
  test_bad_files_and_failed_save();
  game_destroy(game);
  game = NULL;
  for (int i = 0; i < 2; i++) level_free(&levels[i]);
  test_all_floors();
  SDL_RemovePath(SLOT); SDL_RemovePath(COPY); SDL_RemovePath(BAD);
  puts("Save/load snapshot, replay, validation and atomic replacement checks passed.");
  return 0;
}
