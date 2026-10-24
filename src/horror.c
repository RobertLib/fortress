#include "horror.h"
#include "game.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static bool dry_cell(const Level *lv, int x, int y) {
  return x > 0 && y > 0 && x < lv->w - 1 && y < lv->h - 1 && lv->grid[y * lv->w + x] == '.';
}

// Horror directives are opt-in: old/custom maps retain their original rules.
bool horror_configure(Level *lv, const char *text, char *err, size_t size) {
  HorrorLayout *h = &lv->horror;
  char context[96] = {0};
  for (const char *p = text; *p;) {
    const char *end = strchr(p, '\n');
    size_t n = end ? (size_t)(end - p) : strlen(p);
    char line[512], extra;
    if (n >= sizeof line && *p == '@') goto bad;
    if (n < sizeof line) {
      memcpy(line, p, n);
      line[n] = 0;
      if (*p == '@') snprintf(context, sizeof context, "%.95s", line);
      if (!strncmp(line, "@horror ", 8)) {
        if (sscanf(line + 8, "%d %c", &h->chapter, &extra) != 1 || h->chapter < 1 || h->chapter > 5) goto bad;
      } else if (!strncmp(line, "@haven ", 7)) {
        if (h->haven_count == HORROR_HAVENS) goto bad;
        Haven *v = &h->havens[h->haven_count++];
        if (sscanf(line + 7, "%d %d %f %c", &v->x, &v->y, &v->radius, &extra) != 3 ||
            !dry_cell(lv, v->x, v->y) || !isfinite(v->radius) || v->radius < .6f || v->radius > 3) goto bad;
      } else if (!strncmp(line, "@note ", 6)) {
        if (h->note_count == HORROR_NOTES) goto bad;
        HorrorNote *note = &h->notes[h->note_count++];
        int used = 0;
        if (sscanf(line + 6, "%d %d %n", &note->x, &note->y, &used) != 2 || !used ||
            !dry_cell(lv, note->x, note->y) || !line[6 + used] || strlen(line + 6 + used) >= sizeof note->text) goto bad;
        snprintf(note->text, sizeof note->text, "%s", line + 6 + used);
        for (char *c = note->text; *c; c++) {
          if (*c == '\r') *c = 0;
          else if (*c != '|' && ((unsigned char)*c < 32 || (unsigned char)*c > 126)) goto bad;
        }
      } else if (!strncmp(line, "@scene ", 7)) {
        if (h->scene_count == HORROR_SCENES) goto bad;
        Haunt *s = &h->scenes[h->scene_count++];
        char kind[16];
        int fields = sscanf(line + 7, "%15s %d %d %d %d %d %d %d %c", kind,
                            &s->tx, &s->ty, &s->sx, &s->sy, &s->keys, &s->dx, &s->dy, &extra);
        if (fields != 6 && fields != 8) goto bad;
        static const char *names[] = {"cell", "shadow", "knock", "armor", "mirror"};
        int k = 0;
        while (k < 5 && strcmp(kind, names[k])) k++;
        if (k == 5 || (k == HAUNT_ARMOR && fields != 8) || s->keys < 0 || s->keys > 3 ||
            !dry_cell(lv, s->tx, s->ty) || !dry_cell(lv, s->sx, s->sy)) goto bad;
        s->kind = (HauntKind)k;
        if (k == HAUNT_ARMOR && !dry_cell(lv, s->dx, s->dy)) goto bad;
        if (fields == 6) { s->dx = s->sx; s->dy = s->sy; }
      } else if (!strncmp(line, "@trace ", 7)) {
        if (h->trace_count == HORROR_TRACES) goto bad;
        HorrorTrace *t = &h->traces[h->trace_count++];
        char kind[16];
        if (sscanf(line + 7, "%15s %d %d %c", kind, &t->x, &t->y, &extra) != 3 || !dry_cell(lv, t->x, t->y)) goto bad;
        if (!strcmp(kind, "blood")) t->kind = TRACE_BLOOD;
        else if (!strcmp(kind, "scratch")) t->kind = TRACE_SCRATCH;
        else if (!strcmp(kind, "chains")) t->kind = TRACE_CHAINS;
        else goto bad;
      }
    }
    if (!end) break;
    p = end + 1;
  }
  if (!h->chapter && (h->scene_count || h->note_count || h->haven_count || h->trace_count)) goto bad;
  if (!h->chapter) return true;

  // Keep authored scenes and refuges free of automatically placed furniture.
  for (int i = lv->decoration_count - 1; i >= 0; i--) {
    Decoration *d = &lv->decorations[i];
    bool reserved = false;
    for (int j = 0; j < h->haven_count; j++)
      if (hypotf(d->x - h->havens[j].x - .5f, d->y - h->havens[j].y - .5f) < h->havens[j].radius + .5f) reserved = true;
    for (int j = 0; j < h->note_count; j++)
      if (hypotf(d->x - h->notes[j].x - .5f, d->y - h->notes[j].y - .5f) < 1) reserved = true;
    for (int j = 0; j < h->scene_count; j++) {
      const Haunt *s = &h->scenes[j];
      if (hypotf(d->x - s->sx - .5f, d->y - s->sy - .5f) < 1 ||
          hypotf(d->x - s->tx - .5f, d->y - s->ty - .5f) < 1 ||
          hypotf(d->x - s->dx - .5f, d->y - s->dy - .5f) < 1) reserved = true;
    }
    if (reserved) {
      memmove(d, d + 1, (size_t)(lv->decoration_count - i - 1) * sizeof *d);
      lv->decoration_count--;
    }
  }
  // Each wall box has a collision decoration centred on its wall face.
  for (int i = lv->wall_box_count - 1; i >= 0; i--) {
    WallBox b = lv->wall_boxes[i];
    bool found = false;
    for (int j = 0; j < lv->decoration_count; j++)
      if (fabs(lv->decorations[j].x - (b.x + b.dx * .5 + .5)) < .01 &&
          fabs(lv->decorations[j].y - (b.y + b.dy * .5 + .5)) < .01 &&
          lv->decorations[j].kind == (b.kind == BOX_WARDROBE ? DECO_WARDROBE : DECO_CHEST)) found = true;
    if (!found) {
      memmove(&lv->wall_boxes[i], &lv->wall_boxes[i + 1], (size_t)(lv->wall_box_count - i - 1) * sizeof b);
      lv->wall_box_count--;
    }
  }
  return true;
bad:
  snprintf(err, size, "Invalid horror directive: %.80s", context);
  return false;
}

void horror_init(Game *g) {
  HorrorState *h = &g->horror;
  h->reading = -1;
  h->noise_radius=11;
  for (int i = 0; i < 3; i++) h->opened_door[i] = -1;
  h->ambient_timer = 12;
  h->calm_timer = 12;
  if (!g->level->horror.chapter) return;
  size_t cells = (size_t)g->level->w * g->level->h;
  h->distance = malloc(cells * sizeof(int));
  h->queue = malloc(cells * sizeof(int));
}

void horror_free(Game *g) {
  free(g->horror.distance);
  free(g->horror.queue);
}

bool horror_in_haven(const Game *g, float x, float y) {
  const HorrorLayout *h = &g->level->horror;
  for (int i = 0; i < h->haven_count; i++) {
    const Haven *v = &h->havens[i];
    if (hypotf(x - v->x - .5f, y - v->y - .5f) < v->radius &&
        game_line_of_sight(g, x, y, v->x + .5f, v->y + .5f)) return true;
  }
  return false;
}

static bool watched(const Game *g, float x, float y) {
  float dx = x - g->player.x, dy = y - g->player.y, dist = hypotf(dx, dy);
  return dx * g->player.dir_x + dy * g->player.dir_y > dist * .68f &&
         game_line_of_sight(g, g->player.x, g->player.y, x, y);
}

bool horror_holds_door(const Game *g, int index) {
  for (int i = 0; i < g->horror.keys_seen; i++)
    if (g->horror.opened_door[i] == index) return true;
  return false;
}

float horror_torch_strength(const Game *g, float x, float y) {
  const HorrorState *h = &g->horror;
  if (!g->level->horror.chapter) return 1;
  uint32_t hash = (uint32_t)(x * 17 + y * 31) * 2654435761u;
  float strength = .8f;
  for (int i = 0; i < h->keys_seen; i++) {
    float dist = hypotf(x - h->key_x[i], y - h->key_y[i]);
    if (dist < 6 && hash % 3 == 0) strength = .12f;
    if (i == h->keys_seen - 1 && dist < 8 && h->blackout > 0) {
      float fade = minf(1, minf((8 - h->blackout) / .8f, h->blackout / 2));
      strength *= 1 - .94f * fade * maxf(0, 1 - dist / 10);
    }
  }
  const Warden *w = &h->warden;
  if (w->state != WARDEN_DORMANT && hypotf(x - w->x, y - w->y) < 4)
    strength *= .55f + .35f * sq(sinf(g->time * 4.1f + x * 3));
  return clampf(strength, .025f, 1);
}

int horror_near_note(const Game *g) {
  if (!g->level->horror.chapter) return -1;
  const Player *p = &g->player;
  for (int i = 0; i < g->level->horror.note_count; i++) {
    const HorrorNote *n = &g->level->horror.notes[i];
    float dx = n->x + .5f - p->x, dy = n->y + .5f - p->y, d = hypotf(dx, dy);
    if (d < 1.35f && (d < .45f || dx * p->dir_x + dy * p->dir_y > d * .3f) &&
        game_line_of_sight(g, p->x, p->y, n->x + .5f, n->y + .5f)) return i;
  }
  return -1;
}

bool horror_use_note(Game *g) {
  int n = horror_near_note(g);
  if (n < 0) return false;
  g->horror.reading = n;
  g->horror.reading_page = 0;
  g->show_map = false;
  g->horror.notes_read |= 1u << n;
  campaign_converse(g,n);
  audio_play(SND_PARCHMENT, .5f, 1);
  return true;
}

void horror_noise(Game *g, float x, float y, float seconds) {
  if (!g->level->horror.chapter) return;
  g->horror.noise_x = x;
  g->horror.noise_y = y;
  g->horror.noise_timer = seconds;
  g->horror.noise_radius=11;
}

bool horror_warden_solid(const Game *g) {
  return g->horror.warden.state == WARDEN_HUNT;
}

bool horror_warden_targetable(const Game *g) {
  return g->horror.warden.state == WARDEN_HUNT || g->horror.warden.state == WARDEN_STAGGER;
}

float horror_warden_aim(const Game *g, float range) {
  if (!horror_warden_targetable(g)) return INFINITY;
  const Player *p = &g->player;
  const Warden *w = &g->horror.warden;
  float dx = w->x - p->x, dy = w->y - p->y, dist = hypotf(dx, dy);
  if (dist >= range || dx * p->dir_x + dy * p->dir_y <= 0 ||
      fabsf(dx * -p->dir_y + dy * p->dir_x) > WARDEN_RADIUS + dist * .06f ||
      (p->weapon == WEAPON_DAGGER && fabsf(level_floor(g->level, p->x, p->y) - level_floor(g->level, w->x, w->y)) > .25f) ||
      !game_line_of_sight(g, p->x, p->y, w->x, w->y)) return INFINITY;
  return dist;
}

bool horror_hit_warden(Game *g, float stagger) {
  if (!horror_warden_targetable(g)) return false;
  Warden *w = &g->horror.warden;
  w->state = WARDEN_STAGGER;
  w->state_timer = stagger;
  w->attack_timer = 0;
  w->stuck_timer = 0;
  Listener l = game_listener(g);
  audio_play_at(SND_WARDEN_CHAIN, w->x, w->y, &l, 1, .65f);
  game_say(g, "JAILER STAGGERED — SLIP PAST!", minf(stagger, 2));
  return true;
}

static bool segment_hits(float ax, float ay, float bx, float by, float x, float y, float radius) {
  float dx = bx - ax, dy = by - ay, len2 = dx * dx + dy * dy;
  float t = len2 > 0 ? clampf(((x - ax) * dx + (y - ay) * dy) / len2, 0, 1) : 0;
  return hypotf(ax + t * dx - x, ay + t * dy - y) < radius + WARDEN_RADIUS;
}

// Test the whole tread, not just its endpoints: an off-centre barrel or
// soldier can obstruct the passage between two otherwise clear tile centres.
static bool clear_tread(const Game *g, float ax, float ay, float bx, float by) {
  for (int i=0;i<g->level->campaign.count;i++) {
    const Mechanism *m=&g->level->campaign.stations[i];
    if (m->kind!=MECHANISM_SEAL && segment_hits(ax,ay,bx,by,m->x+.5f,m->y+.5f,.23f)) return false;
  }
  for (int i = 0; i < g->barrel_count; i++) {
    const Barrel *b = &g->barrels[i];
    if (!b->destroyed && segment_hits(ax, ay, bx, by, b->x, b->y, BARREL_RADIUS)) return false;
  }
  for (int i = 0; i < g->level->decoration_count; i++) {
    const Decoration *d = &g->level->decorations[i];
    if (d->radius && segment_hits(ax, ay, bx, by, d->x, d->y, d->radius)) return false;
  }
  for (int i = 0; i < g->enemy_count; i++) {
    const Enemy *e = &g->enemies[i];
    if (enemy_dead(e) || e->stats->swarm || e->stats->underfoot || e->flying) continue;
    // An enemy may have walked into the Jailer; permit him to move apart.
    if (hypotf(e->x - g->horror.warden.x, e->y - g->horror.warden.y) < e->radius + WARDEN_RADIUS) continue;
    if (segment_hits(ax, ay, bx, by, e->x, e->y, e->radius)) return false;
  }
  return true;
}

static bool walkable(const Game *g, int x, int y) {
  if (x < 1 || y < 1 || x >= g->level->w - 1 || y >= g->level->h - 1) return false;
  char c = game_cell(g, x, y);
  if (c != '.' && !level_is_water(c) && c != 'D' && c != 'R') return false;
  float px = x + .5f, py = y + .5f;
  if (horror_in_haven(g, px, py)) return false;
  return clear_tread(g, px, py, px, py);
}

static bool distances(Game *g, float tx, float ty) {
  HorrorState *h = &g->horror;
  const Level *lv = g->level;
  int x = (int)tx, y = (int)ty;
  if (!h->distance || !h->queue) return false;
  if (!walkable(g, x, y)) {
    // The player can stand beside furniture within a tile whose centre
    // is obstructed. Route to a nearby approach instead of abandoning him.
    int best_x = -1, best_y = -1;
    float best_dist = 1.5f;
    for (int ny = y - 1; ny <= y + 1; ny++) {
      for (int nx = x - 1; nx <= x + 1; nx++) {
        float px = nx + .5f, py = ny + .5f, dist = hypotf(px - tx, py - ty);
        if (dist >= best_dist || !walkable(g, nx, ny) ||
            fabsf(level_floor_at(lv, nx, ny) - level_floor(lv, tx, ty)) > LEVEL_STEP + .001f ||
            !game_line_of_sight(g, px, py, tx, ty) || !clear_tread(g, px, py, tx, ty)) continue;
        best_x = nx; best_y = ny; best_dist = dist;
      }
    }
    if (best_x < 0) return false;
    x = best_x; y = best_y;
  }
  for (int i = 0; i < lv->w * lv->h; i++) h->distance[i] = -1;
  int head = 0, tail = 0, start = y * lv->w + x;
  h->distance[start] = 0;
  h->queue[tail++] = start;
  static const int dx[] = {1, 0, -1, 0}, dy[] = {0, 1, 0, -1};
  while (head < tail) {
    int cell = h->queue[head++], cx = cell % lv->w, cy = cell / lv->w;
    for (int k = 0; k < 4; k++) {
      int nx = cx + dx[k], ny = cy + dy[k];
      if (nx < 0 || ny < 0 || nx >= lv->w || ny >= lv->h) continue;
      int next = ny * lv->w + nx;
      if (h->distance[next] >= 0) continue;
      if (!walkable(g, nx, ny) || fabsf(level_floor_at(lv, nx, ny) - level_floor_at(lv, cx, cy)) > LEVEL_STEP + .001f ||
          !clear_tread(g, cx + .5f, cy + .5f, nx + .5f, ny + .5f)) continue;
      h->distance[next] = h->distance[cell] + 1;
      h->queue[tail++] = next;
    }
  }
  return true;
}

static void sound_at(Game *g, SoundId id, float x, float y, float volume, float rate) {
  Listener l = game_listener(g);
  audio_play_at(id, x, y, &l, volume, rate);
}

static bool begin_warning(Game *g) {
  const Player *p = &g->player;
  if (horror_in_haven(g, p->x, p->y) || !distances(g, p->x, p->y)) return false;
  HorrorState *h = &g->horror;
  int best = -1;
  float best_score = -1000;
  for (int i = 0; i < g->level->w * g->level->h; i++) {
    int d = h->distance[i];
    float x = i % g->level->w + .5f, y = i / g->level->w + .5f;
    if (d < 7 || d > 18 || game_cell(g, (int)x, (int)y) != '.' || hypotf(x - p->x, y - p->y) < 5 || watched(g, x, y)) continue;
    bool occupied = false;
    for (int j = 0; j < g->enemy_count; j++)
      if (!enemy_dead(&g->enemies[j]) && hypotf(x - g->enemies[j].x, y - g->enemies[j].y) < .7f) occupied = true;
    if (occupied) continue;
    float score = 12 - abs(d - 11) + ((i * 37u) % 13) * .05f;
    if (score > best_score) { best_score = score; best = i; }
  }
  if (best < 0) return false;
  Warden *w = &h->warden;
  *w = (Warden){.state = WARDEN_WARNING, .x = best % g->level->w + .5f, .y = best / g->level->w + .5f,
                .state_timer = WARDEN_WARNING_TIME, .remaining = 30 + g->level->horror.chapter * 2};
  w->next_x = w->x; w->next_y = w->y;
  w->target_x = p->x; w->target_y = p->y;
  w->has_target = true;
  sound_at(g, SND_WARDEN_CHAIN, w->x, w->y, 1.4f, .72f);
  sound_at(g, SND_WARDEN_STEP, w->x, w->y, 1.3f, .8f);
  game_say(g, "CHAINS IN THE DARK — THE JAILER IS COMING", 3);
  return true;
}

static void fade_warden(Game *g) {
  Warden *w = &g->horror.warden;
  w->state = WARDEN_FADING;
  w->state_timer = 2;
  w->attack_timer = 0;
  g->horror.calm_timer = 32;
}

static void next_waypoint(Game *g) {
  Warden *w = &g->horror.warden;
  w->next_x = w->x; w->next_y = w->y;
  if (!w->has_target || !distances(g, w->target_x, w->target_y)) return;
  int x = (int)w->x, y = (int)w->y, cell = y * g->level->w + x;
  if (x < 0 || y < 0 || x >= g->level->w || y >= g->level->h || g->horror.distance[cell] < 0) return;
  // Finish the current tread before turning, so corners cannot be cut.
  if (hypotf(w->x - x - .5f, w->y - y - .5f) > .01f) {
    w->next_x = x + .5f; w->next_y = y + .5f;
    return;
  }
  int best = cell;
  static const int dx[] = {1, 0, -1, 0}, dy[] = {0, 1, 0, -1};
  for (int k = 0; k < 4; k++) {
    int nx = x + dx[k], ny = y + dy[k], i = ny * g->level->w + nx;
    if (nx < 0 || ny < 0 || nx >= g->level->w || ny >= g->level->h ||
        fabsf(level_floor_at(g->level, nx, ny) - level_floor_at(g->level, x, y)) > LEVEL_STEP + .001f ||
        !clear_tread(g, x + .5f, y + .5f, nx + .5f, ny + .5f)) continue;
    if (g->horror.distance[i] >= 0 && g->horror.distance[i] < g->horror.distance[best]) best = i;
  }
  w->next_x = best % g->level->w + .5f;
  w->next_y = best / g->level->w + .5f;
  if (best == cell) { w->next_x = w->target_x; w->next_y = w->target_y; }
}

static void update_warden(Game *g, float dt) {
  HorrorState *h = &g->horror;
  Warden *w = &h->warden;
  const Player *p = &g->player;
  if (w->state == WARDEN_DORMANT) {
    if (h->pending_hunts && h->calm_timer <= 0) {
      if (begin_warning(g)) h->pending_hunts--;
      else h->calm_timer = 6;
    }
    return;
  }
  w->state_timer = maxf(0, w->state_timer - dt);
  if (w->state == WARDEN_FADING) {
    if (w->state_timer <= 0) w->state = WARDEN_DORMANT;
    return;
  }
  if (w->state == WARDEN_WARNING) {
    if (w->state_timer <= 0) w->state = WARDEN_HUNT;
    return;
  }
  w->remaining = g->campaign.finale ? maxf(w->remaining,12) : maxf(0, w->remaining - dt);
  if (w->remaining <= 0) { fade_warden(g); return; }
  if (w->state == WARDEN_STAGGER) {
    if (w->state_timer <= 0) w->state = WARDEN_HUNT;
    return;
  }
  bool safe = horror_in_haven(g, p->x, p->y);
  float dist = hypotf(w->x - p->x, w->y - p->y);
  bool sees = !safe && dist < 9 && game_line_of_sight(g, w->x, w->y, p->x, p->y);
  bool hears = !safe && h->noise_timer > 0 && hypotf(w->x - h->noise_x, w->y - h->noise_y) < h->noise_radius;
  if (sees || hears) {
    w->target_x = sees ? p->x : h->noise_x;
    w->target_y = sees ? p->y : h->noise_y;
    w->has_target = true;
    w->lost_timer = 0;
  } else w->lost_timer += dt;
  if (w->lost_timer > 6) { fade_warden(g); return; }

  float before = w->attack_timer;
  w->attack_timer = maxf(0, w->attack_timer - dt);
  if (before > .55f && w->attack_timer <= .55f && sees && dist < 1.1f &&
      fabsf(level_floor(g->level, w->x, w->y) - level_floor(g->level, p->x, p->y)) <= .25f)
    game_hurt_player(g, 28);
  if (g->player_dead) return;
  if (sees && dist < 1 && w->attack_timer <= 0 &&
      fabsf(level_floor(g->level, w->x, w->y) - level_floor(g->level, p->x, p->y)) <= .25f) {
    w->attack_timer = 1.5f;
    sound_at(g, SND_WARDEN_CHAIN, w->x, w->y, .95f, 1.1f);
  }
  if (w->attack_timer > .55f) { w->stuck_timer = 0; return; }

  w->path_timer -= dt;
  if (hypotf(w->next_x - w->x, w->next_y - w->y) < .001f && w->path_timer <= 0) {
    next_waypoint(g);
    w->path_timer = .2f;
  }
  Door *door = game_door_at(g, (int)w->next_x, (int)w->next_y);
  if (door && door->kind != 'Z' && door->open < .86f) {
    if (door->state != DOOR_OPENING && door->state != DOOR_OPEN) game_open_door(g, door, false);
    return;
  }
  float dx = w->next_x - w->x, dy = w->next_y - w->y, len = hypotf(dx, dy);
  float speed = 1.65f + g->level->horror.chapter * .06f;
  if (level_is_water(game_cell(g, (int)w->x, (int)w->y))) speed *= WATER_SPEED;
  float step = minf(len, dt * speed);
  bool moved = false;
  if (len > .0001f) {
    static const EnemyStats body = {.radius = WARDEN_RADIUS};
    Enemy proxy = {.x = w->x, .y = w->y, .radius = WARDEN_RADIUS, .stats = &body};
    int steps = maxi(1, (int)ceilf(step / .07f));
    for (int i = 0; i < steps; i++) {
      float nx = w->x + dx / len * step / steps, ny = w->y + dy / len * step / steps;
      proxy.x = w->x; proxy.y = w->y;
      if (horror_in_haven(g, nx, ny) || game_is_blocked(g, nx, ny, WARDEN_RADIUS, &proxy)) break;
      w->x = nx; w->y = ny; moved = true;
    }
  }
  if (!moved && w->path_timer < -.8f) { w->next_x = w->x; w->next_y = w->y; }
  w->stuck_timer = moved ? 0 : w->stuck_timer + dt;
  // A changed or unreachable route must never leave an immortal roadblock.
  // Replanning gets a chance first; then the encounter ends without collision.
  if (w->stuck_timer > 3) { fade_warden(g); return; }
  w->step_timer -= dt;
  if (moved && w->step_timer <= 0) {
    w->step_timer = .85f;
    sound_at(g, SND_WARDEN_STEP, w->x, w->y, 1.25f, .85f);
    sound_at(g, SND_WARDEN_CHAIN, w->x, w->y, .35f, .85f);
  }
}

bool horror_scene_visible(const Game *g, int index, float *x, float *y, float *alpha) {
  const Haunt *s = &g->level->horror.scenes[index];
  const HorrorState *h = &g->horror;
  bool changed = (h->scenes_changed & (1u << index)) != 0;
  *x = (changed && s->kind == HAUNT_ARMOR ? s->dx : s->sx) + .5f;
  *y = (changed && s->kind == HAUNT_ARMOR ? s->dy : s->sy) + .5f;
  // Stand between the bars rather than directly behind their central upright.
  if (s->kind == HAUNT_CELL) *y += .2f;
  *alpha = 1;
  if (s->kind == HAUNT_CELL) return !changed;
  if (s->kind == HAUNT_ARMOR) return true;
  if (s->kind == HAUNT_KNOCK || h->scene_timer[index] <= 0) return false;
  *alpha = minf(1, h->scene_timer[index] / .6f) * .8f;
  return true;
}

static void update_scenes(Game *g, float dt) {
  HorrorState *h = &g->horror;
  const HorrorLayout *layout = &g->level->horror;
  const Player *p = &g->player;
  for (int i = 0; i < layout->scene_count; i++) {
    const Haunt *s = &layout->scenes[i];
    uint32_t bit = 1u << i;
    h->scene_timer[i] = maxf(0, h->scene_timer[i] - dt);
    if (!(h->scenes_fired & bit) && p->keys >= s->keys && hypotf(p->x - s->tx - .5f, p->y - s->ty - .5f) < 1.65f &&
        game_line_of_sight(g, p->x, p->y, s->tx + .5f, s->ty + .5f)) {
      h->scenes_fired |= bit;
      h->scene_timer[i] = s->kind == HAUNT_MIRROR ? 1.4f : 3.2f;
      SoundId sound = s->kind == HAUNT_KNOCK ? SND_HORROR_KNOCK : s->kind == HAUNT_CELL ? SND_HORROR_SCRAPE : SND_HORROR_BREATH;
      sound_at(g, sound, s->sx + .5f, s->sy + .5f, .8f, .9f);
    }
    if ((h->scenes_fired & bit) && !(h->scenes_changed & bit) &&
        ((s->kind == HAUNT_CELL && p->keys > 0) || (s->kind == HAUNT_ARMOR && p->keys >= s->keys)) &&
        !watched(g, s->sx + .5f, s->sy + .5f) && !watched(g, s->dx + .5f, s->dy + .5f) &&
        (s->kind != HAUNT_ARMOR || hypotf(p->x - s->dx - .5f, p->y - s->dy - .5f) > 1)) {
      h->scenes_changed |= bit;
      sound_at(g, SND_HORROR_SCRAPE, p->x - p->dir_x * 2, p->y - p->dir_y * 2, .55f, .8f);
    }
  }
}

static void key_omen(Game *g) {
  HorrorState *h = &g->horror;
  while (h->keys_seen < mini(3, g->player.keys)) {
    h->key_x[h->keys_seen] = g->player.x;
    h->key_y[h->keys_seen] = g->player.y;
    h->keys_seen++;
    h->pending_hunts++;
    h->blackout = 8;
    h->calm_timer = maxf(h->calm_timer, 9);
    audio_play_ex(SND_HORROR_BELL, .65f, .82f, 0, 2200);
    // A familiar, explored wooden door is found open on the return journey.
    for (int i = 0; i < g->door_count; i++) {
      Door *d = &g->doors[i];
      float dist = hypotf(d->x + .5f - g->player.x, d->y + .5f - g->player.y);
      if (d->kind == 'D' && d->state == DOOR_CLOSED && dist > 3 && dist < 9 &&
          g->visited[d->y * g->level->w + d->x] && !watched(g, d->x + .5f, d->y + .5f)) {
        game_open_door(g, d, false);
        h->opened_door[h->keys_seen - 1] = i;
        break;
      }
    }
  }
}

static void update_ambience(Game *g, float dt, bool safe) {
  HorrorState *h = &g->horror;
  h->ambient_timer -= dt;
  if (h->ambient_timer > 0 || safe || h->warden.state != WARDEN_DORMANT) return;
  // Leave real combat cues room in the mix, and preserve substantial silence.
  for (int i = 0; i < g->enemy_count; i++) {
    const Enemy *e = &g->enemies[i];
    if (!enemy_dead(e) && e->state >= ES_CHASE && e->state <= ES_STRIKE &&
        hypotf(e->x - g->player.x, e->y - g->player.y) < 7) return;
  }
  h->ambient_cycle++;
  Lcg r = lcg((uint32_t)h->ambient_cycle * 71237u + g->level->horror.chapter * 2137u);
  h->ambient_timer = 21 + lcg_next(&r) * 25;
  static const SoundId sounds[] = {SND_HORROR_WIND, SND_HORROR_KNOCK, SND_HORROR_SCRAPE, SND_HORROR_STEPS, SND_HORROR_BREATH};
  int kind = (h->ambient_cycle + g->level->horror.chapter) % 5;
  bool outside = world_outside(g->level, (int)g->player.x, (int)g->player.y);
  for (int attempt = 0; attempt < 32; attempt++) {
    float angle = lcg_next(&r) * 2 * (float)M_PI, distance = 3 + lcg_next(&r) * 4;
    int x = (int)floorf(g->player.x + cosf(angle) * distance), y = (int)floorf(g->player.y + sinf(angle) * distance);
    if (!dry_cell(g->level, x, y)) continue;
    SoundId sound = outside ? (kind == 4 ? SND_HORROR_BELL : SND_HORROR_WIND) : sounds[kind];
    sound_at(g, sound, x + .5f, y + .5f, outside ? .28f : kind == 0 ? .3f : .65f, .85f + lcg_next(&r) * .2f);
    break;
  }
}

void horror_update(Game *g, float dt) {
  if (!g->level->horror.chapter || g->player_dead || g->completed) return;
  HorrorState *h = &g->horror;
  bool safe = horror_in_haven(g, g->player.x, g->player.y);
  if (safe && !h->was_safe && g->message_timer <= 0)
    game_say(g, "WHITE CANDLES — THE JAILER CANNOT CROSS THEIR LIGHT", 4);
  h->was_safe = safe;
  h->blackout = maxf(0, h->blackout - dt);
  h->calm_timer = maxf(0, h->calm_timer - dt);
  h->noise_timer = maxf(0, h->noise_timer - dt);
  key_omen(g);
  update_scenes(g, dt);
  update_warden(g, dt);
  update_ambience(g, dt, safe);
  float target = !safe && h->warden.state != WARDEN_DORMANT ?
      maxf(0, 1 - hypotf(h->warden.x - g->player.x, h->warden.y - g->player.y) / 9) : 0;
  h->tension += (target - h->tension) * minf(1, dt * 2);
}
