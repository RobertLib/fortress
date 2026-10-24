// In-level gameplay: player movement & combat, doors, pickups, keys/exit
// logic, line-of-sight service and the level statistics.

#ifndef FORTRESS_GAME_H
#define FORTRESS_GAME_H

#include "audio.h"
#include "common.h"
#include "enemy.h"
#include "level.h"

#define MAX_PROJECTILES 256
#define BARREL_RADIUS 0.18f
#define BARREL_HEIGHT 0.48f
#define BARREL_BLAST_RADIUS 3.2f
#define BARREL_EXPLOSION_TIME 0.7f
#define BREAKABLE_WALL_HEALTH 36
#define WALL_BOLT_DAMAGE 12
#define WALL_DUST_TIME 0.8f
#define PIT_FALL_TIME 0.75f
#define PENDULUM_DAMAGE 30
#define PENDULUM_HIT_COOLDOWN 0.8f

typedef struct Barrel {
  float x, y;
  bool powder, destroyed;
  float blast_timer;
} Barrel;

// Zero-initialised sessions keep the original game balance.
typedef enum { DIFFICULTY_NORMAL, DIFFICULTY_EASY, DIFFICULTY_HARD, DIFFICULTY_COUNT } Difficulty;

typedef struct DifficultySettings {
  const char *name;
  float enemy_health, incoming_damage, ammo;
} DifficultySettings;

const DifficultySettings *game_difficulty_settings(Difficulty difficulty);

// Totals carried across the floors of one run.
typedef struct Session {
  Difficulty difficulty;
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
  float analog_forward, analog_strafe, analog_turn; // -1 .. 1, added to keyboard input
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
  float step_offset; // visual camera easing after stepping up or down
  int venom; // health a spider's bite is still to eat away
  float venom_timer; // until the next pang
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

typedef struct Pendulum {
  float hit_cooldown; // prevents repeated damage during one blade contact
} Pendulum;

// What an arrow in flight does. Trap arrows are real missiles that strike
// whoever stands in their way. Crossbow bolts only show a shot the hitscan
// roll has already settled: one bound for its target homes in on it and
// delivers the damage on arrival, a miss flies on into the masonry.
// Any arrow can be intercepted by a barrel or damage cracked masonry.
typedef enum { SHOT_TRAP, SHOT_MISS, SHOT_AT_PLAYER, SHOT_AT_ENEMY, SHOT_AT_WARDEN } ShotKind;

typedef struct Projectile {
  float x, y, dir_x, dir_y, dist;
  float z; // absolute world height of the shaft
  ShotKind kind;
  Enemy *target; // SHOT_AT_ENEMY
  int dmg; // damage on arrival; zero uses WALL_BOLT_DAMAGE against masonry
  float slope; // vertical travel per horizontal unit for non-homing bolts
} Projectile;

typedef struct Game {
  HorrorState horror;
  const Level *level;
  int level_index;
  Session *session;
  Player player;

  Item *items;
  int item_count;
  Enemy *enemies;
  int enemy_count;
  Barrel *barrels;
  int barrel_count;
  Door *doors;
  int door_count;
  Trap *traps;
  int trap_count;
  Pendulum *pendulums; // parallel to level->pendulums; phase follows game time
  bool *lever_pulled; // parallel to level->levers
  // per-cell lookups: index into doors / levers / portals, or -1
  int *door_at, *lever_at, *portal_at;
  uint8_t *wall_health; // per cell: '8' masonry becomes floor at zero health
  float *wall_dust; // seconds of falling stone and dust after collapse
  Projectile projectiles[MAX_PROJECTILES];
  int projectile_count;

  uint8_t *visited; // automap
  float time;
  int kills, treasure, secrets_found;
  int total_enemies, total_treasure, total_secrets;
  int bonus;

  float damage_flash, shake, pickup_flash, portal_flash, venom_flash;
  float pit_fall; // death animation only; live saves never need to store it
  char message[96];
  float message_timer;
  float fire_timer;
  bool fire_held;
  float weapon_anim_timer;
  bool player_dead, completed, show_map;
} Game;

Game *game_create(const Level *level, int level_index, Session *session, const Persist *persist);
void game_destroy(Game *g);
// Rebind positional audio after replacing a game or discarding a staged load.
void game_activate(Game *g);
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
bool game_line_of_sight_3d(const Game *g, float x0, float y0, float z0, float x1, float y1, float z1);
Listener game_listener(const Game *g);
float game_pendulum_angle(const PendulumSpawn *spawn, float time);

// events
void game_open_door(Game *g, Door *door, bool by_player);
// Loose a crossbow bolt from (x, y) along (aim_x, aim_y). SHOT_AT_PLAYER and
// SHOT_AT_ENEMY (at target) carry dmg to deliver on arrival.
void game_loose_bolt(Game *g, float x, float y, float aim_x, float aim_y, ShotKind kind, Enemy *target, int dmg);
void game_hurt_player(Game *g, int dmg);
// A shot ignites powder; ordinary barrels just stop the projectile.
void game_hit_barrel(Game *g, int index);
void game_hit_wall(Game *g, int x, int y, int dmg);
// Venom that goes on eating amount of health over the next few seconds.
void game_poison_player(Game *g, int amount);
void game_on_enemy_killed(Game *g, const Enemy *e);

#endif
