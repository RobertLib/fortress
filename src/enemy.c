// Enemy AI: patrol (or stand guard) -> alert -> chase -> stop & shoot,
// with pain staggers and a small death animation. Rendered as billboards.
// A footman caught at arm's length doesn't bother with the crossbow: he
// pulls a knife (or swings a gauntleted fist) instead.
// The bat swarm is the odd one out: it flies (drawn head-high, passes
// through other actors), swoops at the player, bites and veers away.
// War hounds are melee runners: one leashed to a handler heels to him,
// barks him awake when it spots the player (and springs up when HE joins
// a fight), charges in to bite, and trots back to the master's side when
// the trail goes cold. A masterless hound guards its ground the same way.
// Rats are melee runners too, only small, quick and frail: they dart about
// in fits and starts, one squeal sets the whole pack on the player, and
// they come on in a zigzag scurry. They slip between everybody's legs, but
// a closed door stops them cold.

#include "enemy.h"

#include <string.h>

#include "game.h"

static const EnemyStats TYPES[ENEMY_TYPE_COUNT] = {
    [ENEMY_GUARD] = {.hp = 25, .speed = 1.7f, .range = 8, .aim_time = 0.42f, .cooldown_min = 0.7f, .cooldown_max = 1.5f,
                     .dmg = 14, .score = 100, .drop = 0.6f, .melee_dmg = 10, .melee_weapon = MELEE_FIST,
                     .radius = 0.3f, .scale = 0.82f, .alert_sound = SND_ALERT, .pain_sound = SND_ENEMY_PAIN,
                     .death_sound = SND_ENEMY_DEATH, .bite_sound = SND_ALERT},
    [ENEMY_KNIGHT] = {.hp = 60, .speed = 1.9f, .range = 9, .aim_time = 0.36f, .cooldown_min = 0.5f,
                      .cooldown_max = 1.2f, .dmg = 21, .score = 500, .drop = 0.8f, .melee_dmg = 18,
                      .melee_weapon = MELEE_KNIFE, .radius = 0.3f, .scale = 0.82f, .alert_sound = SND_ALERT,
                      .pain_sound = SND_ENEMY_PAIN, .death_sound = SND_ENEMY_DEATH, .bite_sound = SND_ALERT},
    [ENEMY_CAPTAIN] = {.hp = 45, .speed = 2.7f, .range = 9, .aim_time = 0.26f, .cooldown_min = 0.4f,
                       .cooldown_max = 1.0f, .dmg = 17, .score = 400, .drop = 0.5f, .melee_dmg = 14,
                       .melee_weapon = MELEE_KNIFE, .radius = 0.3f, .scale = 0.82f, .alert_sound = SND_ALERT,
                       .pain_sound = SND_ENEMY_PAIN, .death_sound = SND_ENEMY_DEATH, .bite_sound = SND_ALERT},
    [ENEMY_BAT] = {.hp = 16, .speed = 3.1f, .range = 0.8f, .cooldown_min = 0.9f, .cooldown_max = 1.6f, .dmg = 7,
                   .score = 150, .drop = 0, .swarm = true, .radius = 0.25f, .scale = 0.6f,
                   .alert_sound = SND_BAT_ALERT, .pain_sound = SND_BAT_SQUEAK, .death_sound = SND_BAT_DEATH,
                   .bite_sound = SND_BAT_BITE},
    [ENEMY_DOG] = {.hp = 18, .speed = 4.1f, .range = 0.95f, .cooldown_min = 0.7f, .cooldown_max = 1.3f, .dmg = 10,
                   .score = 200, .drop = 0, .melee = true, .radius = 0.24f, .scale = 0.62f,
                   .alert_sound = SND_DOG_BARK, .pain_sound = SND_DOG_PAIN, .death_sound = SND_DOG_DEATH,
                   .bite_sound = SND_DOG_BITE, .growl_sound = SND_DOG_GROWL},
    [ENEMY_RAT] = {.hp = 8, .speed = 3.8f, .range = 0.8f, .cooldown_min = 0.6f, .cooldown_max = 1.1f, .dmg = 4,
                   .score = 50, .drop = 0, .melee = true, .underfoot = true, .radius = 0.18f, .scale = 0.5f,
                   .alert_sound = SND_RAT_ALERT, .pain_sound = SND_RAT_SQUEAK, .death_sound = SND_RAT_DEATH,
                   .bite_sound = SND_RAT_BITE, .growl_sound = SND_RAT_SKITTER},
};

static const int CARDINALS[4][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};

static float frandf(void) { return (float)frand(); }

static float cooldown_roll(const EnemyStats *s) { return s->cooldown_min + frandf() * (s->cooldown_max - s->cooldown_min); }

static void play_at(const Enemy *e, Game *g, SoundId id, float volume, float rate) {
  Listener l = game_listener(g);
  audio_play_at(id, e->x, e->y, &l, volume, rate);
}

bool enemy_dead(const Enemy *e) { return e->state == ES_DEAD || e->state == ES_DYING; }

static Enemy *master_of(const Enemy *e, Game *g) { return e->master >= 0 ? &g->enemies[e->master] : NULL; }

void enemy_init(Enemy *e, const EnemySpawn *spawn) {
  memset(e, 0, sizeof *e);
  e->x = spawn->x;
  e->y = spawn->y;
  e->type = spawn->type;
  e->stats = &TYPES[spawn->type];
  e->hp = e->stats->hp;
  e->radius = e->stats->radius;
  e->state = spawn->patrol ? ES_PATROL : ES_STAND;
  e->master = spawn->master;
  if (e->master >= 0) e->state = ES_FOLLOW;
  e->home_x = spawn->x;
  e->home_y = spawn->y;
  e->spawn_patrol = spawn->patrol;
  const int *d = CARDINALS[(int)floor(frand() * 4)];
  e->dir_x = e->face_x = (float)d[0];
  e->dir_y = e->face_y = (float)d[1];
  e->anim_time = frandf() * 10;
  e->wobble = frandf() * (float)M_PI * 2;
  if (e->stats->underfoot) e->growl_timer = 1 + frandf() * 5; // a nest doesn't all squeak at once
}

static bool try_move(Enemy *e, Game *g, float nx, float ny, float step) {
  float len = hypotf(nx, ny);
  if (len == 0) len = 1;
  float tx = e->x + (nx / len) * step;
  float ty = e->y + (ny / len) * step;
  bool moved = false;
  if (!game_is_blocked(g, tx, e->y, e->radius, e)) {
    e->x = tx;
    moved = true;
  }
  if (!game_is_blocked(g, e->x, ty, e->radius, e)) {
    e->y = ty;
    moved = true;
  }
  if (moved && e->stats->melee) {
    e->face_x = nx / len;
    e->face_y = ny / len;
  }
  return moved;
}

static bool move_toward(Enemy *e, Game *g, float tx, float ty, float step) {
  float nx = tx - e->x, ny = ty - e->y;
  if (try_move(e, g, nx, ny, step) || try_move(e, g, signf(nx), 0, step) || try_move(e, g, 0, signf(ny), step)) {
    e->moving = true;
    return true;
  }
  return false;
}

// chasing enemies push open the door just ahead of them (rats can't)
static void open_door_ahead(Enemy *e, Game *g, float nx, float ny) {
  if (e->stats->underfoot) return;
  Door *door = game_door_at(g, (int)floorf(e->x + nx * 0.8f), (int)floorf(e->y + ny * 0.8f));
  if (door) game_open_door(g, door, false);
}

static bool can_see_player(const Enemy *e, const Game *g) {
  const Player *p = &g->player;
  if (hypotf(p->x - e->x, p->y - e->y) > 11) return false;
  return game_line_of_sight(g, e->x, e->y, p->x, p->y);
}

void enemy_wake(Enemy *e, Game *g, bool silent) {
  switch (e->state) {
  case ES_DEAD:
  case ES_DYING:
  case ES_CHASE:
  case ES_AIM:
  case ES_FIRE:
  case ES_SWOOP:
  case ES_BITE:
  case ES_WINDUP:
  case ES_STRIKE: return;
  default: break;
  }
  e->state = ES_CHASE;
  e->attack_cooldown = 0.35f + frandf() * 0.4f;
  e->lost_timer = 0;
  e->has_last_seen = true;
  e->last_seen_x = g->player.x;
  e->last_seen_y = g->player.y;
  if (!silent) play_at(e, g, e->stats->alert_sound, 1, 0.9f + frandf() * 0.25f);
  // a barking hound brings its handler running
  Enemy *m = master_of(e, g);
  if (m && !enemy_dead(m)) enemy_wake(m, g, true);
  // one squealing rat sets the rest of its pack scurrying
  if (e->stats->underfoot) {
    for (int i = 0; i < g->enemy_count; i++) {
      Enemy *o = &g->enemies[i];
      if (o->stats->underfoot && hypotf(o->x - e->x, o->y - e->y) < 4 &&
          game_line_of_sight(g, e->x, e->y, o->x, o->y))
        enemy_wake(o, g, true);
    }
  }
}

// Dog perception: quicker on the uptake than the soldiery, and a hound
// whose handler joins a fight springs up with him even without a sighting.
static void watch_for_player(Enemy *e, Game *g, float dt) {
  const Enemy *m = master_of(e, g);
  if (m && !enemy_dead(m) && (m->state == ES_CHASE || m->state == ES_AIM || m->state == ES_FIRE)) {
    enemy_wake(e, g, false);
    return;
  }
  if (can_see_player(e, g)) {
    e->reaction += dt;
    if (e->reaction > 0.1f + frandf() * 0.15f) enemy_wake(e, g, false);
  } else {
    e->reaction = maxf(0, e->reaction - dt);
  }
}

static void flutter_wings(Enemy *e, Game *g, float dt) {
  e->flutter_timer -= dt;
  if (e->flutter_timer > 0) return;
  e->flutter_timer = 0.28f + frandf() * 0.2f;
  play_at(e, g, SND_FLUTTER, 0.55f, 0.9f + frandf() * 0.25f);
}

static void bite(Enemy *e, Game *g) {
  const Player *p = &g->player;
  play_at(e, g, e->stats->bite_sound, 1, 1);
  game_hurt_player(g, maxi(2, (int)round_half_up(2 + frand() * e->stats->dmg)));
  e->attack_cooldown = cooldown_roll(e->stats);
  // peel away past the player's shoulder
  float dx = e->x - p->x, dy = e->y - p->y;
  float d = hypotf(dx, dy);
  if (d == 0) d = 1;
  float side = frand() < 0.5 ? 1.0f : -1.0f;
  e->dir_x = (dx / d) * 0.6f + (-dy / d) * side;
  e->dir_y = (dy / d) * 0.6f + (dx / d) * side;
  if (e->stats->melee) {
    e->state = ES_BITE;
    e->timer = 0.2f;
  } else {
    e->state = ES_SWOOP;
    e->timer = 0.4f + frandf() * 0.25f;
  }
}

// Point-blank knife stab or bare-knuckle swing (footmen only). The blow
// lands only if the player hasn't slipped out of reach during the windup.
static void strike(Enemy *e, Game *g) {
  const Player *p = &g->player;
  play_at(e, g, e->stats->melee_weapon == MELEE_KNIFE ? SND_KNIFE_SLASH : SND_PUNCH, 1, 0.9f + frandf() * 0.2f);
  float dist = hypotf(p->x - e->x, p->y - e->y);
  if (dist < 1.45f && game_line_of_sight(g, e->x, e->y, p->x, p->y))
    game_hurt_player(g, maxi(3, (int)round_half_up(3 + frand() * e->stats->melee_dmg)));
}

static void fire(Enemy *e, Game *g) {
  const Player *p = &g->player;
  if (!game_line_of_sight(g, e->x, e->y, p->x, p->y)) return;
  play_at(e, g, SND_ENEMY_SHOT, 1, 1);
  float dist = hypotf(p->x - e->x, p->y - e->y);
  float evade = p->speed_now > 1.5f ? 2.5f : 0;
  float hit_chance = maxf(0.1f, 1 - (dist + evade) / 14);
  float aim_x = p->x - e->x, aim_y = p->y - e->y;
  if (frand() < hit_chance) {
    float falloff = dist < 2 ? 1 : dist < 6 ? 0.7f : 0.45f;
    int dmg = maxi(3, (int)round_half_up((4 + frand() * e->stats->dmg) * falloff));
    game_loose_bolt(g, e->x, e->y, aim_x, aim_y, SHOT_AT_PLAYER, NULL, dmg);
  } else {
    // a miss hisses past the player's shoulder into the wall behind
    float side = frand() < 0.5 ? 1.0f : -1.0f;
    float wide = (0.45f + frandf() * 0.4f) * side / maxf(dist, 0.5f);
    game_loose_bolt(g, e->x, e->y, aim_x - aim_y * wide, aim_y + aim_x * wide, SHOT_MISS, NULL, 0);
  }
}

static void die(Enemy *e, Game *g) {
  e->state = ES_DYING;
  e->timer = 0.4f;
  play_at(e, g, e->stats->death_sound, 1, 0.9f + frandf() * 0.2f);
  game_on_enemy_killed(g, e);
}

void enemy_hit(Enemy *e, Game *g, int dmg) {
  if (enemy_dead(e)) return;
  e->hp -= dmg;
  enemy_wake(e, g, true);
  if (e->hp <= 0) {
    die(e, g);
    return;
  }
  play_at(e, g, e->stats->pain_sound, 1, 1);
  if (frand() < 0.7) {
    e->state = ES_PAIN;
    e->timer = 0.22f;
  }
}

void enemy_update(Enemy *e, Game *g, float dt) {
  const EnemyStats *st = e->stats;
  const Player *p = &g->player;
  e->anim_time += dt;
  e->moving = false;

  if (st->swarm && !enemy_dead(e)) {
    // roosting swarms hang under the ceiling; airborne ones bob a little
    e->flying = true;
    e->fly_height = e->state == ES_STAND ? 0.8f : 0.54f + sinf(e->anim_time * 3.2f + e->wobble) * 0.05f;
    if (e->state == ES_CHASE || e->state == ES_SWOOP) flutter_wings(e, g, dt);
  }

  switch (e->state) {
  case ES_DEAD: return;

  case ES_DYING:
    e->timer -= dt;
    if (st->swarm) {
      // the swarm tumbles out of the air
      e->fly_height = maxf(st->scale / 2, (e->flying ? e->fly_height : 0.5f) - dt * 1.6f);
      e->flying = true;
    }
    if (e->timer <= 0) e->state = ES_DEAD;
    return;

  case ES_PAIN:
    e->timer -= dt;
    if (e->timer <= 0) e->state = ES_CHASE;
    return;

  case ES_STAND:
  case ES_PATROL:
    if (e->state == ES_PATROL) {
      if (st->swarm) {
        // aimless fluttering: drift on a free angle, re-pick it often
        e->timer -= dt;
        bool ok = try_move(e, g, e->dir_x, e->dir_y, st->speed * 0.45f * dt);
        if (e->timer <= 0 || !ok) {
          float a = frandf() * (float)M_PI * 2;
          e->dir_x = cosf(a);
          e->dir_y = sinf(a);
          e->timer = 0.6f + frandf() * 0.9f;
        }
        e->moving = true;
      } else if (st->underfoot) {
        // rats go in fits and starts: a quick dash along a free angle, then
        // a pause to sniff the air (a zero heading marks the pause)
        e->timer -= dt;
        bool dashing = e->dir_x != 0 || e->dir_y != 0;
        if (dashing) {
          if (try_move(e, g, e->dir_x, e->dir_y, st->speed * 0.6f * dt))
            e->moving = true;
          else
            e->timer = 0; // nosed into a wall
        }
        if (e->timer <= 0) {
          if (dashing) {
            e->dir_x = e->dir_y = 0;
            e->timer = 0.5f + frandf() * 1.3f;
          } else {
            float a = frandf() * (float)M_PI * 2;
            e->dir_x = cosf(a);
            e->dir_y = sinf(a);
            e->timer = 0.3f + frandf() * 0.6f;
          }
        }
      } else if (!try_move(e, g, e->dir_x, e->dir_y, st->speed * 0.55f * dt)) {
        const int *d = CARDINALS[(int)floor(frand() * 4)];
        e->dir_x = (float)d[0];
        e->dir_y = (float)d[1];
      } else {
        e->moving = true;
      }
    }
    if (st->underfoot) {
      // the odd squeak gives a nest away before it is seen
      e->growl_timer -= dt;
      if (e->growl_timer <= 0) {
        e->growl_timer = 2.5f + frandf() * 4.5f;
        play_at(e, g, SND_RAT_SQUEAK, 0.45f, 0.9f + frandf() * 0.3f);
      }
    }
    if (st->melee) {
      watch_for_player(e, g, dt);
    } else if (can_see_player(e, g)) {
      e->reaction += dt;
      if (e->reaction > 0.25f + frandf() * 0.3f) enemy_wake(e, g, false);
    } else {
      e->reaction = maxf(0, e->reaction - dt);
    }
    return;

  case ES_FOLLOW: {
    // at heel: trail the handler, sit tight when close
    const Enemy *m = master_of(e, g);
    if (!m || enemy_dead(m)) {
      // the handler has fallen — the hound guards the spot instead
      if (m) {
        e->home_x = m->x;
        e->home_y = m->y;
      }
      e->state = ES_STAND;
      return;
    }
    float d = hypotf(m->x - e->x, m->y - e->y);
    if (d > 1.4f) move_toward(e, g, m->x, m->y, st->speed * (d > 3 ? 0.85f : 0.5f) * dt);
    watch_for_player(e, g, dt);
    return;
  }

  case ES_RETURN: {
    // the trail went cold: trot back to the handler (or the guard post),
    // ready to spring again the moment the player shows himself
    const Enemy *m = master_of(e, g);
    if (m && enemy_dead(m)) m = NULL;
    float tx = m ? m->x : e->home_x;
    float ty = m ? m->y : e->home_y;
    float d = hypotf(tx - e->x, ty - e->y);
    if (d < 1.2f) {
      e->state = m ? ES_FOLLOW : e->spawn_patrol ? ES_PATROL : ES_STAND;
      e->reaction = 0;
      return;
    }
    float nx = (tx - e->x) / d, ny = (ty - e->y) / d;
    if (!move_toward(e, g, tx, ty, st->speed * 0.8f * dt)) {
      e->state = m ? ES_FOLLOW : ES_STAND; // wedged: settle where it stands
    }
    // noses doors open on the way home
    open_door_ahead(e, g, nx, ny);
    watch_for_player(e, g, dt);
    return;
  }

  case ES_CHASE: {
    e->attack_cooldown -= dt;
    float dist = hypotf(p->x - e->x, p->y - e->y);
    if (st->swarm) {
      // swoop straight through the player on a weaving path
      float k = dist != 0 ? dist : 1;
      float nx = (p->x - e->x) / k, ny = (p->y - e->y) / k;
      float wob = sinf(e->anim_time * 6.5f + e->wobble) * 0.7f;
      try_move(e, g, nx - ny * wob, ny + nx * wob, st->speed * dt);
      e->moving = true;
      if (dist < st->range && e->attack_cooldown <= 0) bite(e, g);
      open_door_ahead(e, g, nx, ny);
      return;
    }
    if (st->melee) {
      // run down the player — or the spot he was last seen at; once the
      // trail stays cold too long, give up the hunt and head home
      bool seen = game_line_of_sight(g, e->x, e->y, p->x, p->y);
      if (seen) {
        e->has_last_seen = true;
        e->last_seen_x = p->x;
        e->last_seen_y = p->y;
        e->lost_timer = 0;
      } else {
        e->lost_timer += dt;
        if (e->lost_timer > 3.5f) {
          e->lost_timer = 0;
          e->state = ES_RETURN;
          return;
        }
      }
      float tx = e->has_last_seen ? e->last_seen_x : p->x;
      float ty = e->has_last_seen ? e->last_seen_y : p->y;
      float d = hypotf(tx - e->x, ty - e->y);
      float k = d != 0 ? d : 1;
      float nx = (tx - e->x) / k, ny = (ty - e->y) / k;
      if (d > 0.3f) {
        // rats weave from side to side, straightening out for the bite
        float wob = st->underfoot ? sinf(e->anim_time * 7 + e->wobble) * 0.55f * minf(1, d / 2.5f) : 0;
        move_toward(e, g, tx - ny * wob * d, ty + nx * wob * d, st->speed * dt);
      }
      open_door_ahead(e, g, nx, ny);
      if (seen && dist < st->range && e->attack_cooldown <= 0) {
        bite(e, g);
        return;
      }
      e->growl_timer -= dt;
      if (e->growl_timer <= 0) {
        e->growl_timer = 1.1f + frandf() * 1.2f;
        play_at(e, g, st->growl_sound, 0.7f, 0.9f + frandf() * 0.3f);
      }
      return;
    }
    bool los = game_line_of_sight(g, e->x, e->y, p->x, p->y);
    // at arm's length the crossbow is useless — go for the knife/fists
    if (los && dist < 1.2f && e->attack_cooldown <= 0 && st->melee_dmg) {
      e->state = ES_WINDUP;
      e->timer = 0.26f;
      return;
    }
    if (los && dist < st->range && e->attack_cooldown <= 0 && dist > 0.7f) {
      e->state = ES_AIM;
      e->timer = st->aim_time;
      return;
    }
    // advance toward the player; if too close, hold position
    if (dist > 1.1f) {
      float nx = (p->x - e->x) / dist, ny = (p->y - e->y) / dist;
      float step = st->speed * dt;
      if (try_move(e, g, nx, ny, step) || try_move(e, g, signf(nx), 0, step) || try_move(e, g, 0, signf(ny), step))
        e->moving = true;
      open_door_ahead(e, g, nx, ny);
    }
    return;
  }

  case ES_AIM:
    e->timer -= dt;
    if (e->timer <= 0) {
      fire(e, g);
      e->state = ES_FIRE;
      e->timer = 0.18f;
    }
    return;

  case ES_FIRE:
  case ES_STRIKE:
    e->timer -= dt;
    if (e->timer <= 0) {
      e->attack_cooldown = cooldown_roll(st);
      e->state = ES_CHASE;
    }
    return;

  case ES_WINDUP:
    // arm rearing back for the stab / swing
    e->timer -= dt;
    if (e->timer <= 0) {
      strike(e, g);
      e->state = ES_STRIKE;
      e->timer = 0.18f;
    }
    return;

  case ES_BITE:
    // hounds only: jaws shown for a beat before darting off
    e->timer -= dt;
    if (e->timer <= 0) {
      e->state = ES_SWOOP;
      e->timer = 0.3f + frandf() * 0.2f;
    }
    return;

  case ES_SWOOP:
    // after a bite the swarm (or hound) veers off before circling back
    e->timer -= dt;
    try_move(e, g, e->dir_x, e->dir_y, st->speed * 0.85f * dt);
    e->moving = true;
    if (e->timer <= 0) e->state = ES_CHASE;
    return;
  }
}

// Which of the four hound views the player sees: its heading projected
// into camera space, bucketed the same way the arrow sprites are.
static ViewDir view_from(const Enemy *e, const Player *p) {
  float depth = e->face_x * p->dir_x + e->face_y * p->dir_y;
  float lat = (e->face_x * p->plane_x + e->face_y * p->plane_y) / 0.66f;
  if (depth < -fabsf(lat)) return VIEW_FRONT; // running at the viewer
  if (depth > fabsf(lat)) return VIEW_BACK; // running away
  return lat > 0 ? VIEW_RIGHT : VIEW_LEFT;
}

const Image *enemy_sprite(const Enemy *e, const Assets *assets, const Player *viewer) {
  const EnemySprites *set = &assets->enemies[e->type];
  switch (e->state) {
  case ES_DEAD: return set->pose[POSE_DEAD];
  case ES_DYING: return set->pose[e->timer > 0.2f ? POSE_DIE1 : POSE_DIE2];
  case ES_PAIN: return set->pose[POSE_PAIN];
  case ES_AIM: return set->pose[POSE_AIM];
  case ES_FIRE:
  case ES_BITE: return set->pose[POSE_FIRE];
  case ES_WINDUP: return set->pose[POSE_STRIKE1];
  case ES_STRIKE: return set->pose[POSE_STRIKE2];
  default: {
    const EnemyStats *st = e->stats;
    int f = (int)floorf(e->anim_time * (st->swarm ? 11 : st->underfoot ? 14 : st->melee ? 9 : 5)) % 2;
    int frame = !e->moving ? 0 : f ? 1 : 2; // stand / walk1 / walk2
    if (st->melee && viewer) return set->view[frame][view_from(e, viewer)];
    static const Pose poses[3] = {POSE_STAND, POSE_WALK1, POSE_WALK2};
    return set->pose[poses[frame]];
  }
  }
}
