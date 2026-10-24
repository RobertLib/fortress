// Enemy AI: patrol (or stand guard) -> alert -> chase -> stop & shoot,
// with pain staggers and a small death animation. Rendered as billboards.

#ifndef FORTRESS_ENEMY_H
#define FORTRESS_ENEMY_H

#include "assets.h"
#include "audio.h"
#include "common.h"
#include "level.h"

struct Game;
struct Player;

typedef enum {
  ES_STAND,
  ES_PATROL,
  ES_FOLLOW, // hounds at heel
  ES_RETURN, // hounds (rats, spiders) trotting home after a lost chase
  ES_CHASE,
  ES_AIM,
  ES_FIRE,
  ES_WINDUP, // footmen at arm's length: knife or fist drawn back
  ES_STRIKE,
  ES_BITE, // hounds, rats and spiders: jaws shown for a beat
  ES_SWOOP, // veering off after a bite
  ES_PAIN,
  ES_DYING,
  ES_DEAD
} EnemyState;

typedef enum { MELEE_NONE, MELEE_FIST, MELEE_KNIFE } MeleeWeapon;

typedef struct EnemyStats {
  int hp;
  float speed, range, aim_time;
  float cooldown_min, cooldown_max;
  int dmg, score;
  float drop; // chance of leaving a quiver behind
  int melee_dmg;
  MeleeWeapon melee_weapon;
  bool swarm; // bats: fly, pass through other actors
  bool melee; // hounds, rats and spiders: bite instead of shooting
  bool underfoot; // rats: slip between the other actors' legs, can't open doors
  bool climber; // spiders: lurk under the ceiling, drop on a silk thread, can't open doors
  int venom; // spiders: health a bite keeps eating away afterwards
  float radius, scale;
  SoundId alert_sound, pain_sound, death_sound, bite_sound;
  SoundId growl_sound; // melee runners only: the noise they make on the hunt
  SoundId idle_sound; // rats and spiders: the odd noise that gives them away...
  float chatter; // ...every so many seconds, give or take (0 = silent)
} EnemyStats;

typedef struct Enemy {
  bool tactical;
  float tactic_timer, shield_pressure;
  bool charge_hit;
  float x, y;
  EnemyType type;
  const EnemyStats *stats;
  int hp;
  float radius;
  EnemyState state;
  // hounds: index of the handler in the enemy list, and the spot to fall
  // back to when there is no handler to return to
  int master;
  float home_x, home_y;
  bool spawn_patrol;
  float lost_timer, growl_timer;
  bool has_last_seen;
  float last_seen_x, last_seen_y;
  float dir_x, dir_y;
  float face_x, face_y; // heading a hound, rat or spider was last moving in (drives its view)
  float timer;
  float anim_time;
  bool moving;
  float attack_cooldown;
  float reaction;
  float wobble; // flight-path phase (bats), weave (rats), gait (spiders)
  float flutter_timer;
  bool flying; // bats, and spiders on their thread, carry a height above the floor
  float fly_height;
} Enemy;

void enemy_init(Enemy *e, const EnemySpawn *spawn);
void enemy_update(Enemy *e, struct Game *game, float dt);
void enemy_hit(Enemy *e, struct Game *game, int dmg);
void enemy_hit_player(Enemy *e, struct Game *game, int dmg, WeaponKind weapon);
void enemy_hit_from(Enemy *e, struct Game *game, int dmg, WeaponKind weapon,float source_x,float source_y);
bool enemy_shield_raised(const Enemy *e);
void enemy_wake(Enemy *e, struct Game *game, bool silent);
bool enemy_dead(const Enemy *e);
const Image *enemy_sprite(const Enemy *e, const Assets *assets, const struct Player *viewer);

#endif
