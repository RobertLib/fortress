// In-level gameplay: player movement & combat, doors, pickups, keys/exit
// logic, line-of-sight service and the level statistics.

#ifndef FORTRESS_GAME_H
#define FORTRESS_GAME_H

#include "audio.h"
#include "common.h"
#include "enemy.h"
#include "level.h"

#define MAX_PROJECTILES 256

// Totals carried across the floors of one run.
typedef struct Session {
  int score, lives, next_life;
  int total_kills, total_treasure, total_enemies, max_treasure;
  int total_secrets_found, max_secrets;
  float total_time;
} Session;

// What the player keeps when moving on to the next floor.
typedef struct Persist {
  int health, ammo;
  WeaponKind weapon;
  bool has_arbalest;
} Persist;

typedef struct Input {
  bool forward, back, strafe_left, strafe_right, turn_left, turn_right;
  bool run, strafe_mod, fire, use;
  int weapon; // WeaponKind to switch to, or -1
  float mouse_dx;
} Input;

typedef struct Player {
  float x, y, dir_x, dir_y, plane_x, plane_y;
  int health, ammo;
  WeaponKind weapon;
  bool has_arbalest;
  int keys;
  int weapon_frame;
  float bob_phase, bob_amount;
  int speed_now;
  float radius;
} Player;

typedef enum { DOOR_CLOSED, DOOR_OPENING, DOOR_OPEN, DOOR_CLOSING } DoorState;

typedef struct Door {
  int x, y;
  char kind; // 'D', 'R' or 'Z'
  char tex;
  float open; // 0 closed .. 1 fully open
  DoorState state;
  float timer;
} Door;

typedef enum { TRAP_ARMED, TRAP_TRIGGERED, TRAP_FIRING, TRAP_COOLDOWN } TrapState;

typedef struct Trap {
  const TrapSpawn *spawn;
  TrapState state;
  float timer;
  int shots;
  bool seen; // set off at least once: shows on the map
} Trap;

typedef struct Projectile {
  float x, y, dir_x, dir_y, dist;
} Projectile;

typedef struct Game {
  const Level *level;
  int level_index;
  Session *session;
  Player player;

  Item *items;
  int item_count;
  Enemy *enemies;
  int enemy_count;
  Door *doors;
  int door_count;
  Trap *traps;
  int trap_count;
  bool *lever_pulled; // parallel to level->levers
  // per-cell lookups: index into doors / levers / portals, or -1
  int *door_at, *lever_at, *portal_at;
  Projectile projectiles[MAX_PROJECTILES];
  int projectile_count;

  uint8_t *visited; // automap
  float time;
  int kills, treasure, secrets_found;
  int total_enemies, total_treasure, total_secrets;
  int bonus;

  float damage_flash, shake, pickup_flash, portal_flash;
  char message[96];
  float message_timer;
  float fire_timer;
  bool fire_held;
  float weapon_anim_timer;
  bool player_dead, completed, show_map;
} Game;

Game *game_create(const Level *level, int level_index, Session *session, const Persist *persist);
void game_destroy(Game *g);
void game_update(Game *g, float dt, Input *input);
void game_say(Game *g, const char *text, float secs);
void game_add_score(Game *g, int n);

// world queries
char game_cell(const Game *g, int x, int y);
Door *game_door_at(Game *g, int x, int y);
float game_door_open_amount(const Game *g, int x, int y);
const Lever *game_lever_at(const Game *g, int x, int y);
bool game_lever_pulled(const Game *g, int x, int y);
// self is the moving enemy, or NULL when the player moves
bool game_is_blocked(const Game *g, float x, float y, float r, const Enemy *self);
bool game_line_of_sight(const Game *g, float x0, float y0, float x1, float y1);
Listener game_listener(const Game *g);

// events
void game_open_door(Game *g, Door *door, bool by_player);
void game_hurt_player(Game *g, int dmg);
void game_on_enemy_killed(Game *g, const Enemy *e);

#endif
