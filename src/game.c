// In-level gameplay: player movement & combat, doors, pickups, keys/exit
// logic, line-of-sight service and the level statistics.

#include "game.h"
#include "preferences.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct WeaponSpec {
  float rate;
  int dmg_lo, dmg_hi;
  float range;
  int burst_size;
  float burst_interval;
  bool uses_ammo;
  SoundId sound;
} WeaponSpec;

static const WeaponSpec WEAPONS[WEAPON_COUNT] = {
    [WEAPON_DAGGER] = {0.45f, 8, 16, 1.7f, 1, 0, false, SND_DAGGER},
    [WEAPON_CROSSBOW] = {0.42f, 10, 20, 24, 1, 0, true, SND_CROSSBOW},
    [WEAPON_ARBALEST] = {0.5f, 8, 16, 24, 3, 0.4f, true, SND_ARBALEST},
};

static const DifficultySettings DIFFICULTIES[DIFFICULTY_COUNT] = {
    [DIFFICULTY_NORMAL] = {"NORMAL", 1, 1, 1},
    [DIFFICULTY_EASY] = {"EASY", 0.75f, 0.5f, 1.5f},
    [DIFFICULTY_HARD] = {"HARD", 1.25f, 1.5f, 0.75f},
};

const DifficultySettings *game_difficulty_settings(Difficulty difficulty) {
  if (difficulty < 0 || difficulty >= DIFFICULTY_COUNT) difficulty = DIFFICULTY_NORMAL;
  return &DIFFICULTIES[difficulty];
}

static int scale_amount(int amount, float factor) {
  return maxi(1, (int)round_half_up(amount * factor));
}

#define EXTRA_LIFE_EVERY 40000

#define SHOT_Z 0.44f // chest height: where arrows and bolts fly
#define TRAP_ARROW_SPEED 9.0f // tiles per second
#define BOLT_SPEED 20.0f
#define BOLT_RANGE 30.0f // strays end in masonry long before this

#define VENOM_TICK 0.8f // seconds between the pangs of a spider's venom
#define VENOM_MAX 20 // however many bites, no more poison than this in the blood

// How many sound-blocking cells sit on the straight line between the
// listener and a sound source. Bars let sound through; doors muffle by how
// closed they are. Capped — past a couple of walls it's all the same.
static float sound_occlusion(void *ctx, float x0, float y0, float x1, float y1) {
  const Game *g = ctx;
  int cx = (int)floorf(x0), cy = (int)floorf(y0);
  int tx = (int)floorf(x1), ty = (int)floorf(y1);
  float dx = x1 - x0, dy = y1 - y0;
  int step_x = dx > 0 ? 1 : -1, step_y = dy > 0 ? 1 : -1;
  float t_delta_x = dx != 0 ? fabsf(1 / dx) : INFINITY;
  float t_delta_y = dy != 0 ? fabsf(1 / dy) : INFINITY;
  float t_max_x = dx != 0 ? (dx > 0 ? cx + 1 - x0 : x0 - cx) * t_delta_x : INFINITY;
  float t_max_y = dy != 0 ? (dy > 0 ? cy + 1 - y0 : y0 - cy) * t_delta_y : INFINITY;

  float walls = 0;
  for (int i = 0; i < 128 && walls < 3; i++) {
    if (cx == tx && cy == ty) break;
    if (t_max_x < t_max_y) {
      t_max_x += t_delta_x;
      cx += step_x;
    } else {
      t_max_y += t_delta_y;
      cy += step_y;
    }
    if (cx == tx && cy == ty) break;
    char cell = game_cell(g, cx, cy);
    if (level_is_open(cell) || cell == 'B' || cell == 'R') continue;
    if (cell == 'D' || cell == 'Z') {
      walls += 1 - game_door_open_amount(g, cx, cy);
      continue;
    }
    walls++;
  }
  return minf(3, walls);
}

Game *game_create(const Level *level, int level_index, Session *session, const Persist *persist) {
  Game *g = calloc(1, sizeof(Game));
  g->level = level;
  g->level_index = level_index;
  g->session = session;
  campaign_init(g);
  const DifficultySettings *difficulty = game_difficulty_settings(session->difficulty);

  Player *p = &g->player;
  p->x = level->start_x;
  p->y = level->start_y;
  p->dir_x = level->start_dx;
  p->dir_y = level->start_dy;
  p->plane_x = -level->start_dy * 0.66f;
  p->plane_y = level->start_dx * 0.66f;
  p->health = persist ? persist->health : 100;
  p->ammo = persist ? persist->ammo : scale_amount(16, difficulty->ammo);
  p->weapon = persist ? persist->weapon : WEAPON_CROSSBOW;
  p->has_arbalest = persist ? persist->has_arbalest : false;
  p->radius = 0.22f;

  const int cells = level->w * level->h;
  g->wall_health = calloc((size_t)cells, 1);
  g->wall_dust = calloc((size_t)cells, sizeof(float));
  for (int i = 0; i < cells; i++)
    if (level->grid[i] == '8') g->wall_health[i] = BREAKABLE_WALL_HEALTH;
  // room for every enemy to drop a quiver on top of the placed items
  g->items = malloc(sizeof(Item) * (level->item_count + level->enemy_count + 1));
  memcpy(g->items, level->items, sizeof(Item) * level->item_count);
  g->item_count = level->item_count;

  g->enemy_count = level->enemy_count;
  g->enemies = malloc(sizeof(Enemy) * (level->enemy_count + 1));
  for (int i = 0; i < level->enemy_count; i++) {
    Enemy *e = &g->enemies[i];
    enemy_init(e, &level->enemies[i]);
    e->tactical=level->campaign.enabled;
    e->hp = scale_amount(e->hp, difficulty->enemy_health);
  }

  g->barrel_count = level->barrel_count;
  g->barrels = calloc((size_t)level->barrel_count + 1, sizeof(Barrel));
  for (int i = 0; i < level->barrel_count; i++) {
    const BarrelSpawn *b = &level->barrels[i];
    g->barrels[i] = (Barrel){b->x, b->y, b->powder, false, 0};
  }

  g->door_at = malloc(sizeof(int) * cells);
  g->lever_at = malloc(sizeof(int) * cells);
  g->portal_at = malloc(sizeof(int) * cells);
  for (int i = 0; i < cells; i++) g->door_at[i] = g->lever_at[i] = g->portal_at[i] = -1;

  g->door_count = level->door_count;
  g->doors = malloc(sizeof(Door) * (level->door_count + 1));
  for (int i = 0; i < level->door_count; i++) {
    const DoorSpawn *d = &level->doors[i];
    g->doors[i] = (Door){d->x, d->y, d->kind, d->tex, 0, DOOR_CLOSED, 0};
    g->door_at[d->y * level->w + d->x] = i;
  }
  g->lever_pulled = calloc((size_t)level->lever_count + 1, sizeof(bool));
  for (int i = 0; i < level->lever_count; i++) g->lever_at[level->levers[i].y * level->w + level->levers[i].x] = i;
  for (int i = 0; i < level->portal_count; i++)
    g->portal_at[level->portals[i].y * level->w + level->portals[i].x] = i;

  g->trap_count = level->trap_count;
  g->traps = calloc((size_t)level->trap_count + 1, sizeof(Trap));
  for (int i = 0; i < level->trap_count; i++) g->traps[i] = (Trap){&level->traps[i], TRAP_ARMED, 0, 0, false};
  g->pendulums = calloc((size_t)level->pendulum_count + 1, sizeof(Pendulum));

  g->visited = calloc((size_t)cells, 1);
  g->total_enemies = level->enemy_count;
  g->total_treasure = level->treasure_count;
  g->total_secrets = level->secret_count;
  horror_init(g);

  // let positional audio muffle sounds that come from behind walls
  game_activate(g);
  return g;
}

void game_activate(Game *g) { audio_set_occlusion(g ? sound_occlusion : NULL, g); }

void game_destroy(Game *g) {
  if (!g) return;
  horror_free(g);
  audio_set_occlusion(NULL, NULL);
  free(g->items);
  free(g->enemies);
  free(g->barrels);
  free(g->doors);
  free(g->traps);
  free(g->pendulums);
  free(g->lever_pulled);
  free(g->door_at);
  free(g->lever_at);
  free(g->portal_at);
  free(g->visited);
  free(g->wall_health);
  free(g->wall_dust);
  free(g);
}

void game_say(Game *g, const char *text, float secs) {
  snprintf(g->message, sizeof g->message, "%s", text);
  g->message_timer = secs;
}

void game_add_score(Game *g, int n) {
  Session *s = g->session;
  s->score += n;
  while (s->score >= s->next_life) {
    s->next_life += EXTRA_LIFE_EVERY;
    if (s->arcade || !g->level->campaign.enabled) {
      s->lives++;
      audio_play(SND_EXTRA_LIFE, 1, 1);
      game_say(g, "EXTRA LIFE!", 2.2f);
    }
  }
}

Listener game_listener(const Game *g) {
  Listener l = {g->player.x, g->player.y, g->player.dir_x, g->player.dir_y};
  return l;
}

// ------------------------------------------------------------------ world

char game_cell(const Game *g, int x, int y) {
  const Level *lv = g->level;
  if (x < 0 || y < 0 || x >= lv->w || y >= lv->h) return '1';
  int cell = y * lv->w + x;
  char kind = lv->grid[cell];
  if (level_is_water(kind) && campaign_drained(g,x,y)) return '.';
  return kind == '8' && !g->wall_health[cell] ? '.' : kind;
}

Door *game_door_at(Game *g, int x, int y) {
  const Level *lv = g->level;
  if (x < 0 || y < 0 || x >= lv->w || y >= lv->h) return NULL;
  int i = g->door_at[y * lv->w + x];
  return i >= 0 ? &g->doors[i] : NULL;
}

float game_door_open_amount(const Game *g, int x, int y) {
  Door *d = game_door_at((Game *)g, x, y);
  return d ? d->open : 0;
}

const Lever *game_lever_at(const Game *g, int x, int y) {
  const Level *lv = g->level;
  if (x < 0 || y < 0 || x >= lv->w || y >= lv->h) return NULL;
  int i = g->lever_at[y * lv->w + x];
  return i >= 0 ? &lv->levers[i] : NULL;
}

bool game_lever_pulled(const Game *g, int x, int y) {
  const Level *lv = g->level;
  if (x < 0 || y < 0 || x >= lv->w || y >= lv->h) return false;
  int i = g->lever_at[y * lv->w + x];
  return i >= 0 && g->lever_pulled[i];
}

static const Portal *portal_at(const Game *g, int x, int y) {
  const Level *lv = g->level;
  if (x < 0 || y < 0 || x >= lv->w || y >= lv->h) return NULL;
  int i = g->portal_at[y * lv->w + x];
  return i >= 0 ? &lv->portals[i] : NULL;
}

bool game_is_blocked(const Game *g, float x, float y, float r, const Enemy *self) {
  float from_x = self ? self->x : g->player.x, from_y = self ? self->y : g->player.y;
  float from_z = level_floor(g->level, from_x, from_y);
  bool airborne = self && (self->stats->swarm || self->flying);
  int min_x = (int)floorf(x - r), max_x = (int)floorf(x + r);
  int min_y = (int)floorf(y - r), max_y = (int)floorf(y + r);
  for (int cy = min_y; cy <= max_y; cy++) {
    for (int cx = min_x; cx <= max_x; cx++) {
      char cell = game_cell(g, cx, cy);
      float floor = level_floor_at(g->level, cx, cy);
      if (!airborne && fabsf(floor - from_z) > LEVEL_STEP + .001f) return true;
      if (cell == '.' || level_is_water(cell)) continue;
      // The player can step into a pit. Grounded enemies keep back from
      // its edge; bats and spiders suspended on silk can pass above it.
      if (cell == '!' && (!self || self->stats->swarm || self->flying)) continue;
      if ((cell == 'D' || cell == 'R' || cell == 'Z') && game_door_open_amount(g, cx, cy) > 0.85f) continue;
      // a paired mirror lets the player (self == NULL) walk into the glass
      if (cell == '~' && !self) {
        const Portal *pt = portal_at(g, cx, cy);
        if (pt && pt->pair >= 0) continue;
      }
      return true;
    }
  }
  // entities block each other — except bat swarms, which fly right through
  // (and clear over the furniture too)
  if (self && self->stats->swarm) return false;
  for (int i = 0; i < g->barrel_count; i++) {
    const Barrel *b = &g->barrels[i];
    if (!b->destroyed && sq(b->x - x) + sq(b->y - y) < sq(r + BARREL_RADIUS)) return true;
  }
  const Level *lv = g->level;
  for (int i = 0; i < lv->decoration_count; i++) {
    const Decoration *d = &lv->decorations[i];
    if (!d->radius) continue;
    if ((d->x - x) * (d->x - x) + (d->y - y) * (d->y - y) < (r + d->radius) * (r + d->radius)) return true;
  }
  for (int i=0;i<lv->campaign.count;i++) {
    const Mechanism *m=&lv->campaign.stations[i];
    if (m->kind==MECHANISM_SEAL) continue;
    if (sq(m->x+.5f-x)+sq(m->y+.5f-y)<sq(r+.23f)) return true;
  }
  // Rats scurry underfoot: they slip between everybody's legs and nobody
  // trips over them — only another rat jostles a rat. Whatever is up in
  // the air (a swarm, a spider on its thread) is out of everybody's way.
  // Two already caught overlapping (a rat the player has stepped onto, a
  // spider dropped right on top of someone) may always move apart.
  bool underfoot = self && self->stats->underfoot;
  for (int i = 0; i < g->enemy_count; i++) {
    const Enemy *e = &g->enemies[i];
    if (e == self || enemy_dead(e) || e->stats->swarm || e->flying || e->stats->underfoot != underfoot) continue;
    float d = hypotf(e->x - x, e->y - y);
    if (d < r + e->radius && d <= hypotf(e->x - from_x, e->y - from_y)) return true;
  }
  if (self) {
    float d = hypotf(g->player.x - x, g->player.y - y);
    if (d < r + g->player.radius + 0.15f && d <= hypotf(g->player.x - from_x, g->player.y - from_y)) return true;
  }
  if (!self && horror_warden_solid(g)) {
    const Warden *w = &g->horror.warden;
    float d = hypotf(w->x - x, w->y - y);
    if (d < r + WARDEN_RADIUS && d <= hypotf(w->x - from_x, w->y - from_y)) return true;
  }
  return false;
}

// Voxel traversal; solid walls and mostly-closed wooden doors block sight.
// Bars are physical obstacles, but intentionally do not block vision/fire.
bool game_line_of_sight_3d(const Game *g, float x0, float y0, float z0, float x1, float y1, float z1) {
  int cx = (int)floorf(x0), cy = (int)floorf(y0);
  int tx = (int)floorf(x1), ty = (int)floorf(y1);
  float dx = x1 - x0, dy = y1 - y0;
  int step_x = dx > 0 ? 1 : -1, step_y = dy > 0 ? 1 : -1;
  float delta_x = dx != 0 ? fabsf(1 / dx) : INFINITY;
  float delta_y = dy != 0 ? fabsf(1 / dy) : INFINITY;
  float side_x = dx != 0 ? (dx > 0 ? cx + 1 - x0 : x0 - cx) * delta_x : INFINITY;
  float side_y = dy != 0 ? (dy > 0 ? cy + 1 - y0 : y0 - cy) * delta_y : INFINITY;
  float enter = 0;
  for (int i = 0; i < 256; i++) {
    float exit = minf(1, minf(side_x, side_y));
    float za = z0 + (z1 - z0) * enter, zb = z0 + (z1 - z0) * exit;
    float floor = level_floor_at(g->level, cx, cy);
    if (minf(za, zb) < floor + .001f || maxf(za, zb) > level_ceiling_at(g->level, cx, cy) - .001f) return false;
    if (cx == tx && cy == ty) return true;
    if (enter > 0) {
      char cell = game_cell(g, cx, cy);
      if (!level_is_open(cell) && cell != 'B' && cell != 'R' &&
          !((cell == 'D' || cell == 'Z') && game_door_open_amount(g, cx, cy) > .6f)) return false;
    }
    enter = exit;
    if (side_x < side_y) { side_x += delta_x; cx += step_x; }
    else { side_y += delta_y; cy += step_y; }
  }
  return false;
}

bool game_line_of_sight(const Game *g, float x0, float y0, float x1, float y1) {
  return game_line_of_sight_3d(g, x0, y0, level_floor(g->level, x0, y0) + SHOT_Z,
                             x1, y1, level_floor(g->level, x1, y1) + SHOT_Z);
}

// ------------------------------------------------------------------ doors

void game_open_door(Game *g, Door *door, bool by_player) {
  if (door->kind == 'Z') return; // secret walls only answer to their lever
  if (campaign_door_braced(g,(int)(door-g->doors))) {
    if (by_player) game_say(g,"THE DOOR IS BRACED - WAIT FOR IT TO GIVE WAY",2);
    return;
  }
  if (door->state == DOOR_CLOSED || door->state == DOOR_CLOSING) {
    door->state = DOOR_OPENING;
    Listener l = game_listener(g);
    audio_play_at(SND_DOOR, door->x + 0.5f, door->y + 0.5f, &l, by_player ? 1 : 0.8f, 1);
  }
}

static bool doorway_occupied(const Game *g, const Door *d) {
#define IN_CELL(ex, ey, r) ((ex) + (r) > d->x && (ex) - (r) < d->x + 1 && (ey) + (r) > d->y && (ey) - (r) < d->y + 1)
  if (IN_CELL(g->player.x, g->player.y, g->player.radius + 0.1f)) return true;
  if (horror_warden_targetable(g) && IN_CELL(g->horror.warden.x, g->horror.warden.y, WARDEN_RADIUS + .1f)) return true;
  for (int i = 0; i < g->enemy_count; i++) {
    const Enemy *e = &g->enemies[i];
    if (!enemy_dead(e) && IN_CELL(e->x, e->y, e->radius + 0.1f)) return true;
  }
  return false;
#undef IN_CELL
}

static void update_doors(Game *g, float dt) {
  for (int i = 0; i < g->door_count; i++) {
    Door *d = &g->doors[i];
    if (d->state == DOOR_OPENING) {
      d->open += dt * (d->kind == 'Z' ? 0.55f : 1.6f); // stone grinds slowly
      if (d->open >= 1) {
        d->open = 1;
        d->state = DOOR_OPEN;
        d->timer = 4;
      }
    } else if (d->state == DOOR_OPEN) {
      if (d->kind == 'Z' || horror_holds_door(g, i)) continue; // revealed passages and haunted doors stay open
      d->timer -= dt;
      if (d->timer <= 0) {
        if (doorway_occupied(g, d)) {
          d->timer = 1;
        } else {
          d->state = DOOR_CLOSING;
          Listener l = game_listener(g);
          audio_play_at(SND_DOOR, d->x + 0.5f, d->y + 0.5f, &l, 0.7f, 0.85f);
        }
      }
    } else if (d->state == DOOR_CLOSING) {
      d->open -= dt * 1.6f;
      if (d->open <= 0) {
        d->open = 0;
        d->state = DOOR_CLOSED;
      }
    }
  }
}

// ------------------------------------------------------------------ player

float game_pendulum_angle(const PendulumSpawn *spawn, float time) {
  float phase = (spawn->x * 3 + spawn->y * 5) % 8 * ((float)M_PI / 4);
  return PENDULUM_MAX_ANGLE * sinf(time * (2 * (float)M_PI / PENDULUM_PERIOD) + phase);
}

static void update_pendulums(Game *g, float dt, float time) {
  const Player *p = &g->player;
  for (int i = 0; i < g->level->pendulum_count; i++) {
    const PendulumSpawn *s = &g->level->pendulums[i];
    Pendulum *trap = &g->pendulums[i];
    trap->hit_cooldown = maxf(0, trap->hit_cooldown - dt);
    float angle = game_pendulum_angle(s, time);
    float sn = sinf(angle), cs = cosf(angle);
    float bx = s->x + 0.5f + s->dx * PENDULUM_LENGTH * sn;
    float by = s->y + 0.5f + s->dy * PENDULUM_LENGTH * sn;
    // A circular player footprint against the blade's thin horizontal box.
    float half_width = PENDULUM_BLADE_WIDTH * cs + PENDULUM_BLADE_HEIGHT * fabsf(sn);
    float along = fabsf((p->x - bx) * s->dx + (p->y - by) * s->dy);
    float across = fabsf((p->x - bx) * s->dy - (p->y - by) * s->dx);
    float dx = maxf(0, along - half_width), dy = maxf(0, across - 0.035f);
    if (!trap->hit_cooldown && dx * dx + dy * dy < sq(p->radius) &&
        game_line_of_sight(g, bx, by, p->x, p->y)) {
      trap->hit_cooldown = PENDULUM_HIT_COOLDOWN;
      game_hurt_player(g, PENDULUM_DAMAGE);
      game_say(g, "STRUCK BY A SWINGING BLADE!", 1.5f);
      if (g->player_dead) return;
    }
    // Each sweep through the bottom of the arc gives a positional warning.
    float before = game_pendulum_angle(s, time - dt);
    if ((before < 0 && angle >= 0) || (before > 0 && angle <= 0)) {
      Listener l = game_listener(g);
      audio_play_at(SND_PENDULUM, s->x + 0.5f, s->y + 0.5f, &l, 0.85f, 1);
    }
  }
}

static void rotate_player(Player *p, float angle) {
  float c = cosf(angle), s = sinf(angle);
  float dx = p->dir_x * c - p->dir_y * s;
  p->dir_y = p->dir_x * s + p->dir_y * c;
  p->dir_x = dx;
  float px = p->plane_x * c - p->plane_y * s;
  p->plane_y = p->plane_x * s + p->plane_y * c;
  p->plane_x = px;
}

static void pull_lever(Game *g, int x, int y) {
  const Level *lv = g->level;
  int idx = g->lever_at[y * lv->w + x];
  if (idx < 0) return;
  const Lever *lever = &lv->levers[idx];
  // the handle sits on one face only — from any other side this is just
  // blank masonry under the player's hand
  float facing = (g->player.x - (x + 0.5f)) * lever->dx + (g->player.y - (y + 0.5f)) * lever->dy;
  if (facing < 0.3f) return;
  if (g->lever_pulled[idx]) {
    audio_play(SND_BLIP, 1, 1);
    return;
  }
  g->lever_pulled[idx] = true;
  audio_play(SND_LEVER, 1, 1);
  int opened = 0;
  Listener l = game_listener(g);
  for (int i = 0; i < lever->door_count; i++) {
    int cell = lever->doors[i];
    Door *door = game_door_at(g, cell % lv->w, cell / lv->w);
    if (door && door->state == DOOR_CLOSED) {
      door->state = DOOR_OPENING;
      audio_play_at(SND_SECRET, door->x + 0.5f, door->y + 0.5f, &l, 1, 1);
      opened++;
    }
  }
  if (opened) {
    g->secrets_found++;
    g->session->total_secrets_found++;
    game_add_score(g, 500);
    game_say(g, "A SECRET PASSAGE GRINDS OPEN!", 3);
  }
}

static void use_action(Game *g) {
  if (horror_use_note(g)) return;
  if (campaign_use(g)) return;
  const Player *p = &g->player;
  static const float reach[3] = {0.6f, 1.1f, 1.6f};
  for (int i = 0; i < 3; i++) {
    int cx = (int)floorf(p->x + p->dir_x * reach[i]);
    int cy = (int)floorf(p->y + p->dir_y * reach[i]);
    char cell = game_cell(g, cx, cy);
    if (fabsf(level_floor_at(g->level, cx, cy) - level_floor(g->level, p->x, p->y)) > .25f) return;
    if (level_is_open(cell)) continue;
    if (cell == 'D' || cell == 'R') {
      Door *door = game_door_at(g, cx, cy);
      if (door) game_open_door(g, door, true);
      return;
    }
    if (cell == 'X') {
      if (p->keys >= 3) {
        if (!campaign_gate_ready(g)) return;
        g->completed = true;
        audio_play(SND_LEVEL_DONE, 1, 1);
      } else {
        audio_play(SND_LOCKED, 1, 1);
        int missing = 3 - p->keys;
        char msg[64];
        snprintf(msg, sizeof msg, "THE GATE IS LOCKED — %d KEY%s MISSING", missing, missing > 1 ? "S" : "");
        game_say(g, msg, 2.2f);
      }
      return;
    }
    if (cell == 'L') {
      pull_lever(g, cx, cy);
      return;
    }
    return; // plain wall (secret walls included — only their lever moves them)
  }
}

static bool fall_into_pit(Game *g) {
  Player *p = &g->player;
  int x = (int)floorf(p->x), y = (int)floorf(p->y);
  if (game_cell(g, x, y) != '!' || p->x < x + PIT_RIM || p->x > x + 1 - PIT_RIM ||
      p->y < y + PIT_RIM || p->y > y + 1 - PIT_RIM) return false;
  p->health = 0;
  p->venom = 0;
  p->speed_now = 0;
  p->bob_amount = 0;
  g->player_dead = true;
  g->show_map = false;
  g->pit_fall = 0.001f;
  game_say(g, "YOU FELL INTO A PIT!", 2);
  audio_play(SND_PIT_FALL, 1, 1);
  audio_play(SND_PLAYER_DEATH, 0.8f, 0.8f);
  return true;
}

static void update_player(Game *g, float dt, Input *in) {
  Player *p = &g->player;
  if (fall_into_pit(g)) return;
  float run = in->run ? 1.55f : 1;
  float speed = 3.4f * run * dt;
  float rot_speed = 2.7f * dt;

  if (in->turn_left && !in->strafe_mod) rotate_player(p, -rot_speed);
  if (in->turn_right && !in->strafe_mod) rotate_player(p, rot_speed);
  if (in->analog_turn) rotate_player(p, clampf(in->analog_turn, -1, 1) * rot_speed);
  if (in->mouse_dx) {
    rotate_player(p, in->mouse_dx * 0.0026f * preferences_get()->sensitivity/100.0f);
    in->mouse_dx = 0;
  }

  float mx = p->dir_x * in->analog_forward - p->dir_y * in->analog_strafe;
  float my = p->dir_y * in->analog_forward + p->dir_x * in->analog_strafe;
  if (in->forward) {
    mx += p->dir_x;
    my += p->dir_y;
  }
  if (in->back) {
    mx -= p->dir_x;
    my -= p->dir_y;
  }
  if (in->strafe_left || (in->strafe_mod && in->turn_left)) {
    mx += p->dir_y;
    my += -p->dir_x;
  }
  if (in->strafe_right || (in->strafe_mod && in->turn_right)) {
    mx += -p->dir_y;
    my += p->dir_x;
  }
  float len = hypotf(mx, my);
  if (len > 1) {
    mx /= len;
    my /= len;
  }
  len = minf(len, 1);
  // Sample both movement and the moving blades: neither a sprint nor a long
  // frame may jump across a trap. Idle players still meet the swinging blade.
  int steps = maxi(1, (int)ceilf(speed / 0.1f));
  if (g->level->pendulum_count) steps = maxi(steps, (int)ceilf(dt / 0.01f));
  float start_x = p->x, start_y = p->y;
  for (int i = 0; i < steps; i++) {
    float before_floor = level_floor(g->level, p->x, p->y);
    if (len > 0.001f) {
      bool wet = level_is_water(game_cell(g, (int)floorf(p->x), (int)floorf(p->y)));
      float step = speed / steps * (wet ? WATER_SPEED : 1);
      float nx = p->x + mx * step;
      if (!game_is_blocked(g, nx, p->y, p->radius, NULL)) p->x = nx;
      if (fall_into_pit(g)) return;
      float ny = p->y + my * step;
      if (!game_is_blocked(g, p->x, ny, p->radius, NULL)) p->y = ny;
      if (fall_into_pit(g)) return;
      if (wet != level_is_water(game_cell(g, (int)floorf(p->x), (int)floorf(p->y))))
        audio_play(SND_SPLASH, .65f, 1);
    }
    p->step_offset = clampf(p->step_offset + before_floor - level_floor(g->level, p->x, p->y), -.25f, .25f);
    p->step_offset *= expf(-18 * dt / steps);
    update_pendulums(g, dt / steps, g->time - dt + dt * (i + 1) / steps);
    if (g->player_dead) return;
  }
  if (len > 0.001f) {
    bool wet = level_is_water(game_cell(g, (int)floorf(p->x), (int)floorf(p->y)));
    float drag = wet ? WATER_SPEED : 1;
    p->speed_now = (int)(3.4f * run * len * drag);
    float before = p->bob_phase;
    p->bob_phase += dt * 9 * run * drag * len;
    if (wet && hypotf(p->x - start_x, p->y - start_y) > .001f &&
        floorf(before / (float)M_PI) != floorf(p->bob_phase / (float)M_PI))
      audio_play(SND_SPLASH, .4f, 1 + .08f * sinf(p->bob_phase));
    if (hypotf(p->x - start_x, p->y - start_y) > .001f &&
        floorf(before / (float)M_PI) != floorf(p->bob_phase / (float)M_PI)) {
      if (!wet) {
        GroundKind ground=world_ground(g->level,(int)p->x,(int)p->y);
        SoundId step=ground==GROUND_GRASS || ground==GROUND_LEAVES || ground==GROUND_EARTH?SND_STEP_LEAVES:
          !world_outside(g->level,(int)p->x,(int)p->y) && g->level->surface_theme==SURFACE_BARRACKS?SND_STEP_WOOD:SND_STEP;
        audio_play(step,in->run?.65f:.3f,1);
      }
      if (in->run || wet) {
        horror_noise(g,p->x,p->y,.7f);
        if (campaign_has_relic(g,RELIC_QUIET)) { g->horror.noise_timer=.32f; g->horror.noise_radius=4.5f; }
      }
    }
    p->bob_amount = minf(1, p->bob_amount + dt * 6);
  } else {
    p->speed_now = 0;
    p->bob_amount = maxf(0, p->bob_amount - dt * 6);
  }

  // stepping into a mirror: once the player's centre crosses into the
  // glass, the twin draws him out on its own side, facing away from it —
  // the arrival spot lies outside the cell, so walking back in returns him
  const Portal *portal = portal_at(g, (int)floorf(p->x), (int)floorf(p->y));
  if (portal && portal->pair >= 0) {
    const Portal *dest = &g->level->portals[portal->pair];
    p->x = dest->x + 0.5f + dest->dx * 0.8f;
    p->y = dest->y + 0.5f + dest->dy * 0.8f;
    p->dir_x = (float)dest->dx;
    p->dir_y = (float)dest->dy;
    p->plane_x = -dest->dy * 0.66f;
    p->plane_y = dest->dx * 0.66f;
    p->step_offset = 0;
    g->portal_flash = 0.6f;
    audio_play(SND_PORTAL, 1, 1);
  }

  if (in->use) {
    in->use = false;
    use_action(g);
  }
  if (in->weapon >= 0 && in->weapon < WEAPON_COUNT) {
    if (in->weapon != WEAPON_ARBALEST || p->has_arbalest) {
      if ((int)p->weapon != in->weapon) {
        p->weapon = (WeaponKind)in->weapon;
        g->burst_remaining = 0;
        audio_play(SND_BLIP, 1, 1);
      }
    }
    in->weapon = -1;
  }
}

// ------------------------------------------------------------------ combat

static void alert_nearby(Game *g, float x, float y, float radius) {
  for (int i = 0; i < g->enemy_count; i++) {
    Enemy *e = &g->enemies[i];
    if (enemy_dead(e)) continue;
    float dist = hypotf(e->x - x, e->y - y);
    if (dist < radius && (dist < 4 || game_line_of_sight(g, e->x, e->y, x, y)))
      enemy_wake(e, g, true);
  }
}

void game_hit_wall(Game *g, int x, int y, int dmg) {
  if (dmg <= 0 || game_cell(g, x, y) != '8') return;
  int cell = y * g->level->w + x;
  g->wall_health[cell] = maxi(0, g->wall_health[cell] - dmg);
  bool broken = !g->wall_health[cell];
  Listener l = game_listener(g);
  audio_play_at(broken ? SND_WALL_BREAK : SND_WALL_HIT, x + 0.5f, y + 0.5f, &l, broken ? 1.2f : 0.8f, 1);
  if (broken) {
    g->wall_dust[cell] = WALL_DUST_TIME;
    float dist = hypotf(x + 0.5f - g->player.x, y + 0.5f - g->player.y);
    if (dist < 5) g->shake = maxf(g->shake, 0.35f * (1 - dist / 5));
    game_say(g, "THE WALL CRUMBLES!", 1.8f);
    alert_nearby(g, x + 0.5f, y + 0.5f, 9);
  }
}

// First opaque cell on the actual weapon ray. Bars let the strike through;
// a door, lever or ordinary wall stops it before any fragile masonry behind.
static float weapon_wall(const Game *g, float x, float y, float dx, float dy, float range, int *wx, int *wy) {
  int cx = (int)floorf(x), cy = (int)floorf(y);
  int sx = dx > 0 ? 1 : -1, sy = dy > 0 ? 1 : -1;
  float tx = dx != 0 ? fabsf(1 / dx) : INFINITY;
  float ty = dy != 0 ? fabsf(1 / dy) : INFINITY;
  float nx = dx != 0 ? (dx > 0 ? cx + 1 - x : x - cx) * tx : INFINITY;
  float ny = dy != 0 ? (dy > 0 ? cy + 1 - y : y - cy) * ty : INFINITY;
  for (int i = 0; i < 128; i++) {
    float dist;
    if (nx < ny) { dist = nx; nx += tx; cx += sx; }
    else { dist = ny; ny += ty; cy += sy; }
    if (dist > range) break;
    char cell = game_cell(g, cx, cy);
    if (level_is_open(cell) || cell == 'B' || cell == 'R') continue;
    if ((cell == 'D' || cell == 'Z') && game_door_open_amount(g, cx, cy) > 0.85f) continue;
    *wx = cx;
    *wy = cy;
    return dist;
  }
  return INFINITY;
}

static int blast_damage(const Game *g, const Barrel *b, float x, float y) {
  float dist = hypotf(x - b->x, y - b->y);
  if (dist >= BARREL_BLAST_RADIUS || !game_line_of_sight(g, b->x, b->y, x, y)) return 0;
  return maxi(1, (int)round_half_up(90 * (1 - dist / BARREL_BLAST_RADIUS)));
}

void game_hit_barrel(Game *g, int index) {
  if (index < 0 || index >= g->barrel_count) return;
  Barrel *b = &g->barrels[index];
  if (b->destroyed) return;
  Listener l = game_listener(g);
  if (!b->powder) {
    audio_play_at(SND_ARROW_HIT, b->x, b->y, &l, 0.7f, 0.8f);
    return;
  }
  // Mark it first: chain reactions can never detonate the same barrel twice.
  b->destroyed = true;
  b->blast_timer = BARREL_EXPLOSION_TIME;
  // Snapshot cover before any wall collapses: one blast cannot tunnel through
  // several layers of masonry or hurt someone sheltered behind the breach.
  int wall_cells[81], wall_damage[81], wall_count = 0;
  int bx = (int)floorf(b->x), by = (int)floorf(b->y);
  for (int y = maxi(0, by - 4); y <= mini(g->level->h - 1, by + 4); y++) {
    for (int x = maxi(0, bx - 4); x <= mini(g->level->w - 1, bx + 4); x++) {
      if (game_cell(g, x, y) != '8') continue;
      int dmg = blast_damage(g, b, x + 0.5f, y + 0.5f);
      if (!dmg) continue;
      wall_cells[wall_count] = y * g->level->w + x;
      wall_damage[wall_count++] = dmg;
    }
  }
  audio_play_at(SND_EXPLOSION, b->x, b->y, &l, 1.6f, 0.9f + (float)frand() * 0.2f);
  horror_noise(g, b->x, b->y, 2);
  if (horror_warden_targetable(g) && blast_damage(g, b, g->horror.warden.x, g->horror.warden.y)) horror_hit_warden(g, WARDEN_STAGGER_TIME);
  alert_nearby(g, b->x, b->y, 12);
  for (int i = 0; i < g->enemy_count; i++) {
    Enemy *e = &g->enemies[i];
    if (enemy_dead(e)) continue;
    int dmg = blast_damage(g, b, e->x, e->y);
    if (dmg) enemy_hit(e, g, dmg);
  }
  int dmg = blast_damage(g, b, g->player.x, g->player.y);
  if (dmg) game_hurt_player(g, dmg);
  float dist = hypotf(b->x - g->player.x, b->y - g->player.y);
  if (dist < 6 && game_line_of_sight(g, b->x, b->y, g->player.x, g->player.y))
    g->shake = maxf(g->shake, 0.8f * (1 - dist / 6));
  for (int i = 0; i < g->barrel_count; i++) {
    const Barrel *next = &g->barrels[i];
    if (next->powder && !next->destroyed && blast_damage(g, b, next->x, next->y)) game_hit_barrel(g, i);
  }
  for (int i = 0; i < wall_count; i++)
    game_hit_wall(g, wall_cells[i] % g->level->w, wall_cells[i] / g->level->w, wall_damage[i]);
}

static void update_weapon(Game *g, float dt, const Input *in) {
  Player *p = &g->player;
  g->fire_timer = maxf(0, g->fire_timer - dt);
  float old_weapon_timer=g->weapon_anim_timer;
  g->weapon_anim_timer = maxf(0, g->weapon_anim_timer - dt);
  if (g->level->campaign.enabled && old_weapon_timer>.22f && g->weapon_anim_timer<=.22f && p->weapon!=WEAPON_DAGGER)
    audio_play(SND_RELOAD,.5f,p->weapon==WEAPON_ARBALEST?1.2f:1);
  float weapon_duration=g->level->campaign.enabled?.42f:.16f;
  p->weapon_frame = g->weapon_anim_timer > 0 ? (g->weapon_anim_timer > weapon_duration*.65f ? 1 : 2) : 0;

  const WeaponSpec *spec = &WEAPONS[p->weapon];
  bool want_fire = g->burst_remaining > 0 || (in->fire && !g->fire_held);
  g->fire_held = in->fire;
  if (!want_fire || g->fire_timer > 0) return;

  if (spec->uses_ammo && p->ammo <= 0) {
    g->burst_remaining = 0;
    audio_play(SND_EMPTY, 1, 1);
    g->fire_timer = 0.3f;
    if (p->has_arbalest || p->weapon == WEAPON_CROSSBOW) game_say(g, "OUT OF BOLTS — SWITCH TO DAGGER!", 2.2f);
    return;
  }

  // A trigger press commits the whole burst, even after the trigger is released.
  if (!g->burst_remaining) g->burst_remaining = spec->burst_size;
  g->burst_remaining--;
  g->weapon_anim_timer = weapon_duration;
  if (spec->uses_ammo) p->ammo--;
  if (spec->uses_ammo && p->ammo == 0) g->burst_remaining = 0;
  g->fire_timer = g->burst_remaining > 0 ? spec->burst_interval : spec->rate;
  audio_play(spec->sound, 0.9f, 1);
  if (spec->uses_ammo) horror_noise(g, p->x, p->y, 1.5f);
  if (!g->level->campaign.enabled || spec->uses_ammo) alert_nearby(g, p->x, p->y, 9);

  // hitscan: nearest live enemy or barrel close enough to the view axis
  Enemy *best = NULL;
  float best_dist = spec->range;
  for (int i = 0; i < g->enemy_count; i++) {
    Enemy *e = &g->enemies[i];
    if (enemy_dead(e)) continue;
    if (p->weapon == WEAPON_DAGGER && fabsf(level_floor(g->level, e->x, e->y) - level_floor(g->level, p->x, p->y)) > .25f) continue;
    float dx = e->x - p->x, dy = e->y - p->y;
    float dist = hypotf(dx, dy);
    if (dist > spec->range || dist >= best_dist) continue;
    float forward = dx * p->dir_x + dy * p->dir_y;
    if (forward <= 0) continue;
    float side = fabsf(dx * -p->dir_y + dy * p->dir_x);
    if (side > e->radius + dist * (campaign_has_relic(g,RELIC_STEADY)?.075f:.06f)) continue;
    if (!game_line_of_sight(g, p->x, p->y, e->x, e->y)) continue;
    best = e;
    best_dist = dist;
  }
  int barrel = -1;
  for (int i = 0; i < g->barrel_count; i++) {
    const Barrel *b = &g->barrels[i];
    if (b->destroyed) continue;
    float dx = b->x - p->x, dy = b->y - p->y;
    float dist = hypotf(dx, dy);
    if (dist >= best_dist || dx * p->dir_x + dy * p->dir_y <= 0) continue;
    float side = fabsf(dx * -p->dir_y + dy * p->dir_x);
    if (side > BARREL_RADIUS + dist * 0.06f || !game_line_of_sight(g, p->x, p->y, b->x, b->y)) continue;
    barrel = i;
    best = NULL;
    best_dist = dist;
  }
  bool at_warden = horror_warden_aim(g, best_dist) < best_dist;
  if (at_warden) { best = NULL; barrel = -1; }
  int dmg = 0;
  if (best) {
    float falloff = best_dist < 4 ? 1 : best_dist < 10 ? 0.75f : 0.55f;
    double roll = spec->dmg_lo + frand() * (spec->dmg_hi - spec->dmg_lo);
    if (g->level->campaign.enabled) {
      roll=p->weapon==WEAPON_CROSSBOW?18:p->weapon==WEAPON_ARBALEST?12:14;
      falloff=best_dist<8?1:best_dist<16?.85f:.7f;
    }
    dmg = (int)round_half_up(roll * falloff);
  }
  // the dagger strikes home at once; a crossbow looses a bolt that carries
  // the hit to its mark, or flies on along the view into the wall
  if (p->weapon == WEAPON_DAGGER) {
    if (at_warden) horror_hit_warden(g, WARDEN_DAGGER_STAGGER);
    else if (best) enemy_hit_player(best, g, dmg,WEAPON_DAGGER);
    else if (barrel < 0) {
      int x, y;
      if (weapon_wall(g, p->x, p->y, p->dir_x, p->dir_y, spec->range, &x, &y) <= spec->range)
        game_hit_wall(g, x, y, (int)round_half_up(spec->dmg_lo + frand() * (spec->dmg_hi - spec->dmg_lo)));
    }
  } else if (at_warden) {
    const Warden *w = &g->horror.warden;
    game_loose_bolt(g, p->x, p->y, w->x - p->x, w->y - p->y, SHOT_AT_WARDEN, NULL, 0);
  } else if (barrel >= 0) {
    const Barrel *b = &g->barrels[barrel];
    if (g->projectile_count == MAX_PROJECTILES)
      game_hit_barrel(g, barrel);
    else {
      int index = g->projectile_count;
      game_loose_bolt(g, p->x, p->y, b->x - p->x, b->y - p->y, SHOT_MISS, NULL, 0);
      Projectile *shot = &g->projectiles[index];
      shot->slope = (level_floor(g->level, b->x, b->y) + SHOT_Z - shot->z) / maxf(.01f, best_dist);
    }
  } else if (best) {
    game_loose_bolt(g, p->x, p->y, best->x - p->x, best->y - p->y, SHOT_AT_ENEMY, best, dmg);
  } else {
    game_loose_bolt(g, p->x, p->y, p->dir_x, p->dir_y, SHOT_MISS, NULL, WALL_BOLT_DAMAGE);
  }
}

void game_hurt_player(Game *g, int dmg) {
  Player *p = &g->player;
  if (g->player_dead || g->completed || dmg <= 0) return;
  dmg = scale_amount(dmg, game_difficulty_settings(g->session->difficulty)->incoming_damage);
  p->health -= dmg;
  g->damage_flash = minf(0.75f, 0.25f + dmg / 40.0f);
  g->shake = minf(1, 0.45f + dmg / 35.0f);
  if (p->health <= 0) {
    p->health = 0;
    g->player_dead = true;
    audio_play(SND_PLAYER_DEATH, 1, 1);
  } else {
    audio_play(SND_PLAYER_PAIN, 0.8f, 1);
  }
}

// A spider's venom eats at the player a couple of points per pang; fresh
// bites top it up, a healing potion purges it. It can bring him to the
// brink, but it never finishes him off.
void game_poison_player(Game *g, int amount) {
  Player *p = &g->player;
  if (g->player_dead || g->completed || amount <= 0) return;
  amount = scale_amount(amount, game_difficulty_settings(g->session->difficulty)->incoming_damage);
  if (campaign_has_relic(g,RELIC_WARD)) amount=maxi(1,amount/2);
  if (p->venom <= 0) p->venom_timer = VENOM_TICK;
  p->venom = mini(VENOM_MAX, p->venom + amount);
}

static void update_venom(Game *g, float dt) {
  Player *p = &g->player;
  if (p->venom <= 0) return;
  p->venom_timer -= dt;
  if (p->venom_timer > 0) return;
  p->venom_timer = VENOM_TICK;
  int dmg = mini(mini(2, p->venom), p->health - 1);
  if (dmg <= 0) {
    p->venom = 0;
    return;
  }
  p->venom -= dmg;
  p->health -= dmg;
  g->venom_flash = 0.35f;
  audio_play(SND_VENOM, 0.9f, 1);
}

void game_on_enemy_killed(Game *g, const Enemy *e) {
  g->kills++;
  g->session->total_kills++;
  game_add_score(g, e->stats->score);
  if (frand() < e->stats->drop) g->items[g->item_count++] = (Item){e->x, e->y, ITEM_BOLTS_DROP};
}

// ------------------------------------------------------------------ traps

static void update_traps(Game *g, float dt) {
  const Player *p = &g->player;
  Listener l = game_listener(g);
  for (int i = 0; i < g->trap_count; i++) {
    Trap *t = &g->traps[i];
    const TrapSpawn *s = t->spawn;
    bool on_plate = (int)floorf(p->x) == s->x && (int)floorf(p->y) == s->y;
    switch (t->state) {
    case TRAP_ARMED:
      if (on_plate) {
        t->state = TRAP_TRIGGERED;
        t->timer = 0.35f; // a heartbeat to jump off before the volley
        t->seen = true;
        audio_play_at(SND_TRAP_CLICK, s->x + 0.5f, s->y + 0.5f, &l, 1, 1);
        game_say(g, "* CLICK *", 1);
      }
      break;
    case TRAP_TRIGGERED:
      t->timer -= dt;
      if (t->timer <= 0) {
        t->state = TRAP_FIRING;
        t->shots = 3;
        t->timer = 0;
      }
      break;
    case TRAP_FIRING:
      t->timer -= dt;
      if (t->timer <= 0) {
        if (g->projectile_count < MAX_PROJECTILES)
          g->projectiles[g->projectile_count++] =
              (Projectile){s->origin_x, s->origin_y, s->dir_x, s->dir_y, 0,
                           level_floor_at(g->level, s->x, s->y) + SHOT_Z, SHOT_TRAP, NULL, 0, 0,WEAPON_CROSSBOW};
        audio_play_at(SND_ENEMY_SHOT, s->origin_x, s->origin_y, &l, 0.9f, 1.25f);
        t->timer = 0.16f;
        if (--t->shots <= 0) {
          t->state = TRAP_COOLDOWN;
          t->timer = 2.5f;
        }
      }
      break;
    case TRAP_COOLDOWN:
      t->timer -= dt;
      if (t->timer <= 0 && !on_plate) t->state = TRAP_ARMED;
      break;
    }
  }
}

// ------------------------------------------------------------------ projectiles

static void land_bolt(Game *g, const Projectile *a) {
  if (a->kind == SHOT_AT_PLAYER)
    game_hurt_player(g, a->dmg);
  else if (a->kind == SHOT_AT_ENEMY)
    enemy_hit_from(a->target,g,a->dmg,a->weapon,a->target->x-a->dir_x,a->target->y-a->dir_y);
  else if (a->kind == SHOT_AT_WARDEN)
    horror_hit_warden(g, WARDEN_STAGGER_TIME+(campaign_has_relic(g,RELIC_VIGIL)?.9f:0));
}

void game_loose_bolt(Game *g, float x, float y, float aim_x, float aim_y, ShotKind kind, Enemy *target, int dmg) {
  float len = hypotf(aim_x, aim_y);
  if (len == 0) len = 1;
  Projectile a = {x, y, aim_x / len, aim_y / len, 0, level_floor(g->level, x, y) + SHOT_Z, kind, target, dmg, 0,
                  kind==SHOT_AT_ENEMY?g->player.weapon:WEAPON_CROSSBOW};
  if (g->projectile_count < MAX_PROJECTILES)
    g->projectiles[g->projectile_count++] = a;
  else if (kind == SHOT_MISS) {
    int wx, wy;
    if (weapon_wall(g, x, y, a.dir_x, a.dir_y, BOLT_RANGE, &wx, &wy) <= BOLT_RANGE)
      game_hit_wall(g, wx, wy, dmg > 0 ? dmg : WALL_BOLT_DAMAGE);
  } else
    land_bolt(g, &a); // no room left in the air: the hit lands at once
}

// A bolt bound for its mark steers at it every substep, climbing or sinking
// toward the mark's height, and lands its hit once within reach.
static bool home_bolt(Game *g, Projectile *a, float step) {
  const Player *p = &g->player;
  bool at_player = a->kind == SHOT_AT_PLAYER;
  bool at_warden = a->kind == SHOT_AT_WARDEN;
  float tx = at_player ? p->x : at_warden ? g->horror.warden.x : a->target->x;
  float ty = at_player ? p->y : at_warden ? g->horror.warden.y : a->target->y;
  float tz = level_floor(g->level, tx, ty) + (!at_player && !at_warden && a->target->flying ? a->target->fly_height : SHOT_Z);
  float reach = at_player ? p->radius : at_warden ? WARDEN_RADIUS : a->target->radius;
  float d = hypotf(tx - a->x, ty - a->y);
  if (d <= step + reach) {
    if (game_line_of_sight_3d(g, a->x, a->y, a->z, tx, ty, tz)) land_bolt(g, a);
    return true;
  }
  a->dir_x = (tx - a->x) / d;
  a->dir_y = (ty - a->y) / d;
  a->z += (tz - a->z) * step / d;
  return false;
}

static void update_projectiles(Game *g, float dt) {
  Player *p = &g->player;
  int kept = 0;
  for (int i = 0; i < g->projectile_count; i++) {
    Projectile *a = &g->projectiles[i];
    float travel = (a->kind == SHOT_TRAP ? TRAP_ARROW_SPEED : a->kind==SHOT_HOSTILE?11:BOLT_SPEED) * dt;
    int steps = maxi(1, (int)ceilf(travel / 0.05f)); // substeps so fast arrows can't skip a target
    float step = travel / steps;
    bool dead = false;
    for (int s = 0; s < steps && !dead; s++) {
      // another shot felled the mark first: the bolt flies on past
      if (a->kind == SHOT_AT_ENEMY && enemy_dead(a->target)) a->kind = SHOT_MISS;
      if (a->kind == SHOT_AT_WARDEN && !horror_warden_targetable(g)) a->kind = SHOT_MISS;
      if ((a->kind == SHOT_AT_PLAYER || a->kind == SHOT_AT_ENEMY || a->kind == SHOT_AT_WARDEN) && home_bolt(g, a, step)) {
        dead = true;
        break;
      }
      if (a->kind == SHOT_MISS || a->kind == SHOT_TRAP || a->kind==SHOT_HOSTILE) a->z += a->slope * step;
      a->x += a->dir_x * step;
      a->y += a->dir_y * step;
      a->dist += step;
      int cx = (int)floorf(a->x), cy = (int)floorf(a->y);
      char cell = game_cell(g, cx, cy);
      float floor = level_floor_at(g->level, cx, cy);
      if (a->z <= floor || a->z >= level_ceiling_at(g->level, cx, cy)) { dead = true; break; }
      if (cell == 'B' || cell == 'R') continue;
      if (!level_is_open(cell) && !((cell == 'D' || cell == 'Z') && game_door_open_amount(g, cx, cy) > 0.85f)) {
        if (cell == '8') game_hit_wall(g, cx, cy, a->dmg > 0 ? a->dmg : WALL_BOLT_DAMAGE);
        else {
          Listener l = game_listener(g);
          audio_play_at(SND_ARROW_HIT, a->x, a->y, &l, 0.7f, 1);
        }
        dead = true;
        break;
      }
      // Any real arrow can strike a barrel, including enemy and trap shots.
      for (int k = 0; k < g->barrel_count; k++) {
        const Barrel *b = &g->barrels[k];
        if (!b->destroyed && a->z >= level_floor(g->level, b->x, b->y) &&
            a->z <= level_floor(g->level, b->x, b->y) + BARREL_HEIGHT &&
            sq(b->x - a->x) + sq(b->y - a->y) <= sq(BARREL_RADIUS)) {
          game_hit_barrel(g, k);
          dead = true;
          break;
        }
      }
      if (dead) break;
      if (a->kind != SHOT_TRAP && a->kind!=SHOT_HOSTILE) {
        if (a->dist > BOLT_RANGE) dead = true;
        continue; // bolts strike nothing but their mark
      }
      if (!g->player_dead && fabsf(a->z - level_floor(g->level, p->x, p->y) - SHOT_Z) < .35f &&
          hypotf(p->x - a->x, p->y - a->y) < p->radius + 0.12f) {
        game_hurt_player(g,a->kind==SHOT_HOSTILE?a->dmg:10+(int)floorf(frand()*9));
        dead = true;
        break;
      }
      for (int k = 0; k < g->enemy_count; k++) {
        Enemy *e = &g->enemies[k];
        if (a->kind==SHOT_HOSTILE && e==a->target) continue;
        if (enemy_dead(e)) continue;
        if (fabsf(a->z - level_floor(g->level, e->x, e->y) - (e->flying ? e->fly_height : SHOT_Z)) < .35f &&
            hypotf(e->x - a->x, e->y - a->y) < e->radius + 0.05f) {
          enemy_hit(e,g,a->kind==SHOT_HOSTILE?a->dmg:14+(int)floorf(frand()*10));
          dead = true;
          break;
        }
      }
      if (a->dist > 14) dead = true;
    }
    if (!dead) g->projectiles[kept++] = *a;
  }
  g->projectile_count = kept;
}

// ------------------------------------------------------------------ items

static bool apply_pickup(Game *g, const Item *it) {
  Player *p = &g->player;
  float ammo = game_difficulty_settings(g->session->difficulty)->ammo;
  switch (it->kind) {
  case ITEM_KEY:
    p->keys++;
    if (g->level->campaign.enabled) g->campaign.checkpoint_requested=true;
    game_add_score(g, 200);
    audio_play(SND_PICKUP_KEY, 1, 1);
    if (p->keys >= 3) {
      game_say(g,g->level->campaign.enabled && g->campaign.done!=(uint32_t)((1<<g->level->campaign.count)-1)?
        "ALL 3 KEYS FOUND - COMPLETE THE CHAPTER OBJECTIVE":"ALL 3 KEYS FOUND - REACH THE GATE!",3);
      audio_play(SND_EXIT_OPEN, 1, 1);
    } else {
      char msg[32];
      snprintf(msg, sizeof msg, "KEY FOUND (%d/3)", p->keys);
      game_say(g, msg, 2.2f);
    }
    return true;
  case ITEM_POTION:
    if (p->health >= 100) return false;
    p->health = mini(100, p->health + (campaign_has_relic(g,RELIC_MERCY)?35:25));
    p->venom = 0;
    audio_play(SND_PICKUP_HEALTH, 1, 1);
    return true;
  case ITEM_BREAD:
    if (p->health >= 100) return false;
    p->health = mini(100, p->health + 10);
    audio_play(SND_PICKUP, 1, 1);
    return true;
  case ITEM_BOLTS:
    if (p->ammo >= 99) return false;
    p->ammo = mini(99, p->ammo + scale_amount(8, ammo));
    audio_play(SND_PICKUP_AMMO, 1, 1);
    return true;
  case ITEM_BOLTS_DROP:
    if (p->ammo >= 99) return false;
    p->ammo = mini(99, p->ammo + scale_amount(4, ammo));
    audio_play(SND_PICKUP_AMMO, 1, 1);
    return true;
  case ITEM_TREASURE:
    g->treasure++;
    g->session->total_treasure++;
    game_add_score(g, 500);
    audio_play(SND_PICKUP, 1, 1.3f);
    return true;
  case ITEM_ARBALEST:
    p->has_arbalest = true;
    p->ammo = mini(99, p->ammo + scale_amount(20, ammo));
    p->weapon = WEAPON_ARBALEST;
    audio_play(SND_PICKUP_WEAPON, 1, 1);
    game_say(g, "REPEATING CROSSBOW!", 2.2f);
    return true;
  default: return false;
  }
}

static void update_pickups(Game *g) {
  const Player *p = &g->player;
  for (int i = g->item_count - 1; i >= 0; i--) {
    Item it = g->items[i];
    if (hypotf(it.x - p->x, it.y - p->y) > 0.55f ||
        fabsf(level_floor(g->level, it.x, it.y) - level_floor(g->level, p->x, p->y)) > LEVEL_STEP + .001f) continue;
    if (!apply_pickup(g, &it)) continue;
    memmove(&g->items[i], &g->items[i + 1], sizeof(Item) * (g->item_count - i - 1));
    g->item_count--;
    g->pickup_flash = 0.35f;
  }
}

// ------------------------------------------------------------------ update

static void decay_fx(Game *g, float dt) {
  for (int i = 0; i < g->barrel_count; i++)
    g->barrels[i].blast_timer = maxf(0, g->barrels[i].blast_timer - dt);
  for (int i = 0; i < g->level->w * g->level->h; i++)
    g->wall_dust[i] = maxf(0, g->wall_dust[i] - dt);
  g->damage_flash = maxf(0, g->damage_flash - dt * 1.6f);
  g->shake = maxf(0, g->shake - dt * 3.2f);
  g->pickup_flash = maxf(0, g->pickup_flash - dt * 2.5f);
  g->portal_flash = maxf(0, g->portal_flash - dt * 1.8f);
  g->venom_flash = maxf(0, g->venom_flash - dt * 1.4f);
  g->message_timer = maxf(0, g->message_timer - dt);
  g->hit_confirm=maxf(0,g->hit_confirm-dt);
  g->impact_timer=maxf(0,g->impact_timer-dt);
}

static void mark_visited(Game *g) {
  const Level *lv = g->level;
  int px = (int)floorf(g->player.x), py = (int)floorf(g->player.y);
  const int R = 4;
  for (int y = maxi(0, py - R); y <= mini(lv->h - 1, py + R); y++)
    for (int x = maxi(0, px - R); x <= mini(lv->w - 1, px + R); x++) g->visited[y * lv->w + x] = 1;
}

static void update_water_audio(Game *g, float dt) {
  // Overlapping, softly faded burbles, from the closest water within earshot.
  // Scheduling uses the saved world clock, so no extra save state is needed.
  if (!g->level->water_count || floorf(g->time / .7f) == floorf((g->time - dt) / .7f)) return;
  float best = 36, sx = 0, sy = 0;
  bool flowing = false, found = false;
  int px = (int)floorf(g->player.x), py = (int)floorf(g->player.y);
  for (int y = maxi(0, py - 6); y <= mini(g->level->h - 1, py + 6); y++) {
    for (int x = maxi(0, px - 6); x <= mini(g->level->w - 1, px + 6); x++) {
      char cell = game_cell(g, x, y);
      if (!level_is_water(cell)) continue;
      float dist = sq(x + .5f - g->player.x) + sq(y + .5f - g->player.y);
      if (dist >= best) continue;
      best = dist;
      sx = x + .5f;
      sy = y + .5f;
      flowing = cell != '=';
      found = true;
    }
  }
  if (found) {
    Listener listener = game_listener(g);
    audio_play_at(SND_WATER_FLOW, sx, sy, &listener, flowing ? .65f : .22f, 1);
  }
}

void game_update(Game *g, float dt, Input *input) {
  if (g->campaign.journal) return;
  if (g->horror.reading >= 0 && !g->player_dead && !g->completed) {
    if (input->use) { input->use = false; g->horror.reading = -1; }
    return; // Reading pauses the world, including venom and the pursuer.
  }
  if (g->player_dead || g->completed) {
    if (g->pit_fall > 0) g->pit_fall = minf(PIT_FALL_TIME, g->pit_fall + dt);
    decay_fx(g, dt);
    return;
  }
  g->time += dt;
  update_doors(g, dt);
  update_player(g, dt, input);
  if (g->horror.reading >= 0) return;
  if (g->player_dead || g->completed) {
    mark_visited(g);
    decay_fx(g, dt);
    return;
  }
  update_weapon(g, dt, input);
  update_water_audio(g, dt);
  update_pickups(g);
  update_venom(g, dt);
  update_traps(g, dt);
  for (int i = 0; i < g->enemy_count; i++) enemy_update(&g->enemies[i], g, dt);
  update_projectiles(g, dt);
  campaign_update(g,dt);
  horror_update(g, dt);
  mark_visited(g);
  decay_fx(g, dt);
}
