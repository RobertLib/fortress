#include "savegame.h"

#include <SDL3/SDL.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Explicit little-endian fields: no struct padding, native enums or pointers
// reach disk. Bump the version when the schema or gameplay interpretation changes.
#define SAVE_VERSION 8
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
  // Preserve signatures of older maps that have no pendulums.
  if (lv->pendulum_count) {
    u32(c, lv->pendulum_count);
    for (int i = 0; i < lv->pendulum_count; i++) {
      const PendulumSpawn *p = &lv->pendulums[i];
      u32(c, p->x); u32(c, p->y); u32(c, p->dx); u32(c, p->dy);
    }
  }
  // Flat legacy maps keep their signatures; elevation edits invalidate saves.
  if (lv->has_heights) {
    for (int i = 0; i < lv->w * lv->h; i++) real(c, lv->heights[i], -LEVEL_MAX_HEIGHT, LEVEL_MAX_HEIGHT);
  }
  if (lv->has_ceilings) {
    u32(c, 0x4345494cu); // ceiling edits participate in map compatibility
    for (int y = 0; y < lv->h; y++)
      for (int x = 0; x < lv->w; x++) real(c, level_ceiling_at(lv, x, y), -LEVEL_MAX_HEIGHT - 1, LEVEL_MAX_HEIGHT + 1);
  }
  if (lv->world.cells) {
    u32(c, 0x574f524cu);
    for (int i = 0; i < lv->w * lv->h; i++) {
      u32(c, lv->world.cells[i].outside); u32(c, lv->world.cells[i].ground); u32(c, lv->world.cells[i].authored);
    }
    u32(c, lv->world.prop_count);
    for (int i = 0; i < lv->world.prop_count; i++) {
      const WorldProp *p = &lv->world.props[i];
      u32(c, p->x); u32(c, p->y); u32(c, p->kind);
    }
    if (lv->world.roof_count) {
      u32(c,0x524f4f46u); u32(c,lv->world.roof_count);
      for (int i=0;i<lv->world.roof_count;i++) {
        const WorldRoof *r=&lv->world.roofs[i];
        u32(c,r->x); u32(c,r->y); u32(c,r->w); u32(c,r->h); u32(c,r->axis); u32(c,r->slate);
        real(c,r->eave,0,5); real(c,r->ridge,0,5);
      }
    }
    if (lv->world.facade_count) {
      u32(c,0x46414345u); u32(c,lv->world.facade_count);
      for (int i=0;i<lv->world.facade_count;i++) {
        const WorldFacade *f=&lv->world.facades[i];
        u32(c,f->x); u32(c,f->y); u32(c,(uint32_t)f->dx); u32(c,(uint32_t)f->dy); u32(c,f->kind);
      }
    }
  }
  const HorrorLayout *h = &lv->horror;
  if (lv->campaign.enabled) { u32(c,0x43414d50u); u32(c,lv->campaign.signature); }
  if (h->chapter) {
    u32(c, h->chapter);
    u32(c, h->scene_count);
    for (int i = 0; i < h->scene_count; i++) {
      const Haunt *s = &h->scenes[i];
      u32(c, s->kind); u32(c, s->tx); u32(c, s->ty); u32(c, s->sx); u32(c, s->sy);
      u32(c, s->keys); u32(c, s->dx); u32(c, s->dy);
    }
    u32(c, h->note_count);
    for (int i = 0; i < h->note_count; i++) {
      u32(c, h->notes[i].x); u32(c, h->notes[i].y);
      bytes(c, (void *)h->notes[i].text, sizeof h->notes[i].text);
    }
    u32(c, h->haven_count);
    for (int i = 0; i < h->haven_count; i++) {
      u32(c, h->havens[i].x); u32(c, h->havens[i].y);
      real(c, h->havens[i].radius, .6f, 3);
    }
    u32(c, h->trace_count);
    for (int i = 0; i < h->trace_count; i++) {
      u32(c, h->traces[i].x); u32(c, h->traces[i].y); u32(c, h->traces[i].kind);
    }
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
    F(a->dist, 0, 32); F(a->z, c->version >= 4 ? -LEVEL_MAX_HEIGHT : 0, c->version >= 4 ? LEVEL_MAX_HEIGHT + 1 : 1);
    I(a->kind, SHOT_TRAP, c->version>=7?SHOT_HOSTILE:c->version >= 5 ? SHOT_AT_WARDEN : SHOT_AT_ENEMY);
    int target = -1;
    if (!c->reading && a->target) {
      for (int j = 0; j < g->enemy_count; j++) if (a->target == &g->enemies[j]) target = j;
    }
    I(target, -1, g->enemy_count - 1);
    if ((a->kind == SHOT_AT_ENEMY || a->kind==SHOT_HOSTILE) && target < 0) c->ok = false;
    if (c->reading) a->target = target >= 0 ? &g->enemies[target] : NULL;
    I(a->dmg, 0, INT_MAX);
    if (c->version >= 4) F(a->slope, -1024, 1024);
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
  if (c->version >= 4) F(p->step_offset, -.25f, .25f);
  if (c->version >= 3) {
    for (int i = 0; c->ok && i < lv->pendulum_count; i++)
      F(g->pendulums[i].hit_cooldown, 0, PENDULUM_HIT_COOLDOWN);
  }
  if (c->version >= 5 && lv->horror.chapter) {
    HorrorState *h = &g->horror;
    I(h->scenes_fired, 0, (1 << lv->horror.scene_count) - 1);
    I(h->scenes_changed, 0, (1 << lv->horror.scene_count) - 1);
    if (h->scenes_changed & ~h->scenes_fired) c->ok = false;
    I(h->notes_read, 0, (1 << lv->horror.note_count) - 1);
    for (int i = 0; i < lv->horror.scene_count; i++) F(h->scene_timer[i], 0, 3.2f);
    I(h->keys_seen, 0, mini(3, p->keys)); I(h->pending_hunts, 0, c->version>=7 && lv->campaign.enabled?8:h->keys_seen);
    I(h->ambient_cycle, 0, INT_MAX - 1); I(h->reading, -1, lv->horror.note_count - 1);
    for (int i = 0; i < 3; i++) { F(h->key_x[i], 0, lv->w); F(h->key_y[i], 0, lv->h); }
    for (int i = 0; i < 3; i++) I(h->opened_door[i], -1, g->door_count - 1);
    F(h->blackout, 0, 8); T(h->ambient_timer); F(h->calm_timer, 0, TIME_LIMIT); F(h->tension, 0, 1);
    F(h->noise_x, 0, lv->w); F(h->noise_y, 0, lv->h); F(h->noise_timer, 0, TIME_LIMIT); B(h->was_safe);
    Warden *w = &h->warden;
    I(w->state, WARDEN_DORMANT, WARDEN_FADING);
    F(w->x, 0, lv->w); F(w->y, 0, lv->h); F(w->target_x, 0, lv->w); F(w->target_y, 0, lv->h);
    F(w->state_timer, 0, TIME_LIMIT); F(w->remaining, 0, TIME_LIMIT); F(w->lost_timer, 0, TIME_LIMIT);
    F(w->attack_timer, 0, 1.5f); T(w->step_timer); T(w->path_timer);
    F(w->next_x, 0, lv->w); F(w->next_y, 0, lv->h); B(w->has_target);
    if (w->state != WARDEN_DORMANT && (w->x < 1 || w->y < 1 || w->x >= lv->w - 1 || w->y >= lv->h - 1)) c->ok = false;
  }
  if (c->version >= 6) {
    I(g->burst_remaining, 0, 2);
    if (g->burst_remaining && (p->weapon != WEAPON_ARBALEST || p->ammo <= 0)) c->ok = false;
  }
  if (c->version>=7) {
    I(g->session->relics,0,(1<<CAMPAIGN_RELICS)-1); B(g->session->arcade);
    CampaignState *s=&g->campaign;
    I(s->done,0,(1<<lv->campaign.count)-1);
    I(s->relics_taken,0,(1<<lv->campaign.relic_count)-1);
    I(s->lessons,0,31); I(s->active,-1,lv->campaign.count-1); F(s->work,0,12);
    B(s->errand_found); B(s->errand_done); B(s->finale);
    I(s->decoys,0,5); I(s->braces,0,3);
    for (int i=0;i<CAMPAIGN_BRACES;i++) {
      I(s->brace[i].door,-1,g->door_count-1); F(s->brace[i].remaining,0,10);
      if (s->brace[i].remaining>0 && (s->brace[i].door<0 || g->doors[s->brace[i].door].kind!='D')) c->ok=false;
    }
    F(s->decoy_x,0,lv->w); F(s->decoy_y,0,lv->h); F(s->decoy_timer,0,7); T(s->decoy_pulse);
    T(s->lesson_timer); F(s->objective_flash,0,4);
    if (s->errand_done && !s->errand_found) c->ok=false;
    if (s->active>=0 && ((s->done&(1u<<s->active)) || s->work>lv->campaign.stations[s->active].duration)) c->ok=false;
    for (int i=0;i<lv->campaign.count;i++) if ((s->done&(1u<<i)) &&
        (s->done&lv->campaign.stations[i].requires)!=(uint32_t)lv->campaign.stations[i].requires) c->ok=false;
    if (s->finale && (!lv->campaign.enabled || s->done!=(uint32_t)((1<<lv->campaign.count)-1))) c->ok=false;
    for (int i=0;i<g->enemy_count;i++) {
      F(g->enemies[i].tactic_timer,0,9); F(g->enemies[i].shield_pressure,0,10); B(g->enemies[i].charge_hit);
    }
    for (int i=0;i<g->projectile_count;i++) I(g->projectiles[i].weapon,0,WEAPON_COUNT-1);
    F(g->horror.noise_radius,0,11);
  }
  if (c->version>=8) I(g->session->run_id,0,INT_MAX);
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

bool savegame_inspect(const char *path,const Level *levels,int count,SaveInfo *info) {
  if (!path || !levels || count<=0 || !info) return false;
  SDL_IOStream *file=SDL_IOFromFile(path,"rb");
  if (!file) return false;
  Sint64 length=SDL_GetIOSize(file);
  if (length<120 || length>SAVE_LIMIT) { SDL_CloseIO(file); return false; }
  size_t size=(size_t)length; uint8_t *data=malloc(size);
  bool read=data && SDL_ReadIO(file,data,size)==size;
  if (!SDL_CloseIO(file)) read=false;
  if (!read) { free(data); return false; }
  Codec c={.data=data,.size=size,.reading=true,.ok=true};
  char magic[8]; bytes(&c,magic,8); c.version=u32(&c,0);
  int index=integer(&c,0,0,count-1); uint32_t signature=u32(&c,0);
  u32(&c,0); u32(&c,0);
  Session session={0}; transfer_session(&c,&session);
  for (int i=0;i<6;i++) real(&c,0,-10000,10000);
  int health=integer(&c,0,1,100),ammo=integer(&c,0,0,99);
  int run_id=0;
  if (c.version>=8) { c.pos=size-8; run_id=integer(&c,0,0,INT_MAX); }
  c.pos=size-4; uint32_t hash=u32(&c,0);
  bool ok=c.ok && !memcmp(magic,MAGIC,8) && c.version>=1 && c.version<=SAVE_VERSION &&
    hash==checksum(data,size-4) && signature==level_signature(&levels[index]);
  if (ok) *info=(SaveInfo){index,health,ammo,session.difficulty,run_id};
  free(data); return ok;
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
