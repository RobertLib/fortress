#include "savegame.h"

#include <SDL3/SDL.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Explicit little-endian fields: no struct padding, native enums or pointers
// reach disk. Bump the version when the schema or gameplay interpretation changes.
#define SAVE_VERSION 2
#define SAVE_LIMIT (16 * 1024 * 1024)
#define TIME_LIMIT 100000000.0f
static const char MAGIC[8] = {'F', 'R', 'T', 'S', 'A', 'V', 'E', '\0'};

typedef struct Codec {
  uint8_t *data;
  size_t size, pos, capacity;
  bool reading, ok;
  uint32_t version;
} Codec;

static bool fail(char *err, size_t size, const char *message) {
  if (size) snprintf(err, size, "%s", message);
  return false;
}

static void bytes(Codec *c, void *data, size_t size) {
  if (!c->ok) return;
  if (size > SAVE_LIMIT || c->pos > SAVE_LIMIT - size) {
    c->ok = false;
    return;
  }
  if (c->reading) {
    if (c->pos + size > c->size) {
      c->ok = false;
      return;
    }
    memcpy(data, c->data + c->pos, size);
  } else {
    if (c->pos + size > c->capacity) {
      size_t capacity = c->capacity ? c->capacity : 1024;
      while (capacity < c->pos + size) capacity *= 2;
      uint8_t *next = realloc(c->data, capacity);
      if (!next) {
        c->ok = false;
        return;
      }
      c->data = next;
      c->capacity = capacity;
    }
    memcpy(c->data + c->pos, data, size);
    c->size = c->pos + size;
  }
  c->pos += size;
}

static uint32_t u32(Codec *c, uint32_t value) {
  uint8_t b[4];
  for (int i = 0; i < 4; i++) b[i] = (uint8_t)(value >> (i * 8));
  bytes(c, b, sizeof b);
  uint32_t result = 0;
  for (int i = 0; i < 4; i++) result |= (uint32_t)b[i] << (i * 8);
  return result;
}

static int integer(Codec *c, int value, int lo, int hi) {
  int32_t signed_value = value;
  uint32_t bits;
  memcpy(&bits, &signed_value, sizeof bits);
  bits = u32(c, bits);
  memcpy(&signed_value, &bits, sizeof bits);
  if (signed_value < lo || signed_value > hi) {
    c->ok = false;
    return lo;
  }
  return signed_value;
}

static float real(Codec *c, float value, float lo, float hi) {
  _Static_assert(sizeof(float) == sizeof(uint32_t), "Save files require 32-bit floats");
  uint32_t bits;
  memcpy(&bits, &value, sizeof bits);
  bits = u32(c, bits);
  memcpy(&value, &bits, sizeof bits);
  if (!isfinite(value) || value < lo || value > hi) {
    c->ok = false;
    return lo;
  }
  return value;
}

#define I(field, lo, hi) do { int v = integer(c, (field), (lo), (hi)); if (c->reading) (field) = v; } while (0)
#define F(field, lo, hi) do { float v = real(c, (field), (lo), (hi)); if (c->reading) (field) = v; } while (0)
#define B(field) do { int v = integer(c, (field), 0, 1); if (c->reading) (field) = v != 0; } while (0)
#define T(field) F(field, -TIME_LIMIT, TIME_LIMIT)

static uint32_t checksum(const uint8_t *data, size_t size) {
  uint32_t hash = 2166136261u;
  for (size_t i = 0; i < size; i++) hash = (hash ^ data[i]) * 16777619u;
  return hash;
}

// Catch edits to maps and their spawns, even when object counts still match.
static uint32_t level_signature(const Level *lv) {
  Codec storage = {.ok = true}, *c = &storage;
  u32(c, lv->w); u32(c, lv->h);
  bytes(c, lv->grid, (size_t)lv->w * lv->h);
  real(c, lv->start_x, 0, lv->w); real(c, lv->start_y, 0, lv->h);
  real(c, lv->start_dx, -1, 1); real(c, lv->start_dy, -1, 1);
  u32(c, lv->item_count);
  for (int i = 0; i < lv->item_count; i++) {
    real(c, lv->items[i].x, 0, lv->w); real(c, lv->items[i].y, 0, lv->h);
    u32(c, lv->items[i].kind);
  }
  u32(c, lv->enemy_count);
  for (int i = 0; i < lv->enemy_count; i++) {
    const EnemySpawn *e = &lv->enemies[i];
    real(c, e->x, 0, lv->w); real(c, e->y, 0, lv->h);
    u32(c, e->type); u32(c, e->patrol); u32(c, (uint32_t)e->master);
  }
  u32(c, lv->barrel_count);
  for (int i = 0; i < lv->barrel_count; i++) {
    const BarrelSpawn *b = &lv->barrels[i];
    real(c, b->x, 0, lv->w); real(c, b->y, 0, lv->h); u32(c, b->powder);
  }
  u32(c, lv->door_count);
  for (int i = 0; i < lv->door_count; i++) {
    const DoorSpawn *d = &lv->doors[i];
    u32(c, d->x); u32(c, d->y); u32(c, d->kind); u32(c, d->tex);
  }
  u32(c, lv->trap_count);
  for (int i = 0; i < lv->trap_count; i++) {
    const TrapSpawn *t = &lv->traps[i];
    u32(c, t->x); u32(c, t->y);
    real(c, t->dir_x, -1, 1); real(c, t->dir_y, -1, 1);
    real(c, t->origin_x, 0, lv->w); real(c, t->origin_y, 0, lv->h);
  }
  u32(c, lv->lever_count);
  for (int i = 0; i < lv->lever_count; i++) {
    const Lever *l = &lv->levers[i];
    u32(c, l->x); u32(c, l->y); u32(c, l->door_count);
    for (int j = 0; j < l->door_count; j++) u32(c, l->doors[j]);
  }
  u32(c, lv->portal_count);
  for (int i = 0; i < lv->portal_count; i++) {
    const Portal *p = &lv->portals[i];
    u32(c, p->x); u32(c, p->y); u32(c, (uint32_t)p->dx);
    u32(c, (uint32_t)p->dy); u32(c, (uint32_t)p->pair);
  }
  uint32_t hash = c->ok ? checksum(c->data, c->size) : 0;
  free(c->data);
  return hash;
}

static void transfer_session(Codec *c, Session *s) {
  I(s->difficulty, 0, DIFFICULTY_COUNT - 1);
  I(s->score, 0, INT_MAX - 40000); I(s->lives, 1, INT_MAX);
  I(s->next_life, 1, INT_MAX - 40000);
  if (s->next_life <= s->score) c->ok = false;
  I(s->total_kills, 0, INT_MAX); I(s->total_treasure, 0, INT_MAX);
  I(s->total_enemies, 0, INT_MAX); I(s->max_treasure, 0, INT_MAX);
  I(s->total_secrets_found, 0, INT_MAX); I(s->max_secrets, 0, INT_MAX);
  F(s->total_time, 0, TIME_LIMIT);
}

static void transfer_game(Codec *c, Game *g) {
  const Level *lv = g->level;
  Player *p = &g->player;
  F(p->x, 0, lv->w - 0.001f); F(p->y, 0, lv->h - 0.001f);
  F(p->dir_x, -1.01f, 1.01f); F(p->dir_y, -1.01f, 1.01f);
  F(p->plane_x, -0.67f, 0.67f); F(p->plane_y, -0.67f, 0.67f);
  float dir_length = hypotf(p->dir_x, p->dir_y);
  float plane_length = hypotf(p->plane_x, p->plane_y);
  if (dir_length < 0.99f || dir_length > 1.01f || plane_length < 0.65f || plane_length > 0.67f ||
      fabsf(p->dir_x * p->plane_x + p->dir_y * p->plane_y) > 0.01f) c->ok = false;
  I(p->health, 1, 100); I(p->ammo, 0, 99); I(p->weapon, 0, WEAPON_COUNT - 1);
  B(p->has_arbalest); I(p->keys, 0, lv->key_count);
  if (p->weapon == WEAPON_ARBALEST && !p->has_arbalest) c->ok = false;
  I(p->weapon_frame, 0, 2); T(p->bob_phase); F(p->bob_amount, 0, 1);
  I(p->speed_now, 0, 8); I(p->venom, 0, 20); T(p->venom_timer);
  I(g->item_count, 0, lv->item_count + lv->enemy_count);
  for (int i = 0; c->ok && i < g->item_count; i++) {
    Item *it = &g->items[i];
    F(it->x, 0, lv->w); F(it->y, 0, lv->h); I(it->kind, 0, ITEM_KIND_COUNT - 1);
  }
  for (int i = 0; c->ok && i < g->enemy_count; i++) {
    Enemy *e = &g->enemies[i];
    F(e->x, 0, lv->w); F(e->y, 0, lv->h);
    I(e->hp, INT_MIN, INT_MAX); I(e->state, ES_STAND, ES_DEAD);
    F(e->home_x, 0, lv->w); F(e->home_y, 0, lv->h);
    T(e->lost_timer); T(e->growl_timer); B(e->has_last_seen);
    F(e->last_seen_x, 0, lv->w); F(e->last_seen_y, 0, lv->h);
    // Biting enemies peel away with a deliberately unnormalised heading.
    F(e->dir_x, -1.2f, 1.2f); F(e->dir_y, -1.2f, 1.2f);
    F(e->face_x, -1.01f, 1.01f); F(e->face_y, -1.01f, 1.01f);
    T(e->timer); T(e->anim_time); B(e->moving);
    T(e->attack_cooldown); T(e->reaction); T(e->wobble); T(e->flutter_timer);
    B(e->flying); F(e->fly_height, 0, 1);
  }
  for (int i = 0; c->ok && i < g->barrel_count; i++) {
    B(g->barrels[i].destroyed); F(g->barrels[i].blast_timer, 0, BARREL_EXPLOSION_TIME);
  }
  for (int i = 0; c->ok && i < g->door_count; i++) {
    F(g->doors[i].open, 0, 1); I(g->doors[i].state, DOOR_CLOSED, DOOR_CLOSING); T(g->doors[i].timer);
  }
  for (int i = 0; c->ok && i < g->trap_count; i++) {
    I(g->traps[i].state, TRAP_ARMED, TRAP_COOLDOWN); T(g->traps[i].timer);
    I(g->traps[i].shots, 0, 3); B(g->traps[i].seen);
  }
  for (int i = 0; c->ok && i < lv->lever_count; i++) B(g->lever_pulled[i]);
  for (int i = 0; c->ok && i < lv->w * lv->h; i++) I(g->visited[i], 0, 1);
  I(g->projectile_count, 0, MAX_PROJECTILES);
  for (int i = 0; c->ok && i < g->projectile_count; i++) {
    Projectile *a = &g->projectiles[i];
    F(a->x, -1, lv->w + 1); F(a->y, -1, lv->h + 1);
    F(a->dir_x, -1.01f, 1.01f); F(a->dir_y, -1.01f, 1.01f);
    F(a->dist, 0, 32); F(a->z, 0, 1); I(a->kind, SHOT_TRAP, SHOT_AT_ENEMY);
    int target = -1;
    if (!c->reading && a->target) {
      for (int j = 0; j < g->enemy_count; j++) if (a->target == &g->enemies[j]) target = j;
    }
    I(target, -1, g->enemy_count - 1);
    if (a->kind == SHOT_AT_ENEMY && target < 0) c->ok = false;
    if (c->reading) a->target = target >= 0 ? &g->enemies[target] : NULL;
    I(a->dmg, 0, INT_MAX);
  }
  F(g->time, 0, TIME_LIMIT);
  I(g->kills, 0, g->total_enemies); I(g->treasure, 0, g->total_treasure);
  I(g->secrets_found, 0, g->total_secrets);
  T(g->damage_flash); T(g->shake); T(g->pickup_flash); T(g->portal_flash); T(g->venom_flash);
  bytes(c, g->message, sizeof g->message);
  if (!memchr(g->message, '\0', sizeof g->message)) c->ok = false;
  T(g->message_timer); T(g->fire_timer); B(g->fire_held); T(g->weapon_anim_timer); B(g->show_map);
  if (c->version >= 2) {
    for (int i = 0; c->ok && i < lv->w * lv->h; i++) {
      if (lv->grid[i] != '8') continue;
      I(g->wall_health[i], 0, BREAKABLE_WALL_HEALTH);
      F(g->wall_dust[i], 0, WALL_DUST_TIME);
      if (g->wall_health[i] && g->wall_dust[i] > 0) c->ok = false;
    }
  }
}

bool savegame_write(const char *path, Game *g, char *err, size_t err_size) {
  if (!g || g->player_dead || g->completed || g->player.health <= 0)
    return fail(err, err_size, "SAVE DURING A LIVE GAME");
  Codec storage = {.ok = true, .version = SAVE_VERSION}, *c = &storage;
  char magic[sizeof MAGIC];
  memcpy(magic, MAGIC, sizeof magic);
  bytes(c, magic, sizeof magic);
  u32(c, SAVE_VERSION); u32(c, g->level_index); u32(c, level_signature(g->level));
  uint64_t rng = random_state();
  u32(c, (uint32_t)rng); u32(c, (uint32_t)(rng >> 32));
  transfer_session(c, g->session);
  transfer_game(c, g);
  u32(c, checksum(c->data, c->size));
  if (!c->ok) {
    free(c->data);
    return fail(err, err_size, "COULD NOT ENCODE SAVED GAME");
  }
  size_t temporary_size = strlen(path) + sizeof ".tmp";
  char *temporary = malloc(temporary_size);
  if (!temporary) {
    free(c->data);
    return fail(err, err_size, "NOT ENOUGH MEMORY TO SAVE");
  }
  snprintf(temporary, temporary_size, "%s.tmp", path);
  SDL_IOStream *file = SDL_IOFromFile(temporary, "wb");
  bool ok = false;
  if (file) {
    ok = SDL_WriteIO(file, c->data, c->size) == c->size;
    if (ok) ok = SDL_FlushIO(file);
    if (!SDL_CloseIO(file)) ok = false;
    if (ok) ok = SDL_RenamePath(temporary, path);
    if (!ok) SDL_RemovePath(temporary);
  }
  free(temporary);
  free(c->data);
  return ok || fail(err, err_size, "COULD NOT WRITE SAVED GAME");
}

bool savegame_load(const char *path, const Level *levels, int level_count,
                   Game **current, Session *session, char *err, size_t err_size) {
  SDL_IOStream *file = SDL_IOFromFile(path, "rb");
  if (!file) return fail(err, err_size, "NO SAVED GAME OR FILE UNREADABLE");
  Sint64 size = SDL_GetIOSize(file);
  if (size < 32 || size > SAVE_LIMIT) {
    SDL_CloseIO(file);
    return fail(err, err_size, "SAVED GAME IS DAMAGED");
  }
  uint8_t *data = malloc((size_t)size);
  bool ok = data && SDL_ReadIO(file, data, (size_t)size) == (size_t)size;
  if (!SDL_CloseIO(file)) ok = false;
  if (!ok) {
    free(data);
    return fail(err, err_size, "COULD NOT READ SAVED GAME");
  }
  Codec storage = {.data = data, .size = (size_t)size, .reading = true, .ok = true}, *c = &storage;
  c->pos = c->size - 4;
  uint32_t stored_checksum = u32(c, 0);
  c->size -= 4;
  c->pos = 0;
  char magic[sizeof MAGIC];
  bytes(c, magic, sizeof magic);
  c->version = u32(c, 0);
  const char *error = NULL;
  if (memcmp(magic, MAGIC, sizeof magic) || checksum(data, c->size) != stored_checksum)
    error = "SAVED GAME IS DAMAGED";
  else if (c->version < 1 || c->version > SAVE_VERSION)
    error = "SAVED GAME VERSION IS NOT SUPPORTED";
  int index = integer(c, 0, 0, level_count - 1);
  uint32_t signature = u32(c, 0);
  if (!error && (!c->ok || signature != level_signature(&levels[index])))
    error = "SAVED GAME USES A DIFFERENT LEVEL";
  uint64_t rng = u32(c, 0);
  rng |= (uint64_t)u32(c, 0) << 32;
  Session restored = {0};
  transfer_session(c, &restored);
  if (!rng || !c->ok) error = error ? error : "SAVED GAME IS DAMAGED";
  if (error) {
    free(data);
    return fail(err, err_size, error);
  }

  uint64_t previous_rng = random_state();
  Game *loaded = game_create(&levels[index], index, &restored, NULL);
  transfer_game(c, loaded);
  ok = c->ok && c->pos == c->size;
  free(data);
  if (!ok) {
    game_destroy(loaded);
    game_activate(*current);
    random_seed(previous_rng);
    return fail(err, err_size, "SAVED GAME IS DAMAGED");
  }
  game_destroy(*current);
  *session = restored;
  loaded->session = session;
  *current = loaded;
  game_activate(loaded);
  random_seed(rng);
  return true;
}
