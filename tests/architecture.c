#include "render.h"
#include "savegame.h"

#include <SDL3/SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(expr) do { if (!(expr)) { \
  fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #expr); exit(1); \
} } while (0)

static const char *HALL =
    "@ceiling 0 0 13 7 3\n"
    "1111111111111\n"
    "1...........1\n"
    "1...........1\n"
    "1P..........1\n"
    "1...........1\n"
    "1...........1\n"
    "1111111111111\n";

static void aim(Game *g, float x, float y, float dx, float dy) {
  g->player.x = x; g->player.y = y;
  g->player.dir_x = dx; g->player.dir_y = dy;
  g->player.plane_x = -dy * .66f; g->player.plane_y = dx * .66f;
}

static void test_ceilings(const Assets *assets) {
  Level lv;
  char err[160], text[1024], path[1024];
  const char *bad[] = {"0 0 13 7 nan", "0 0 14 7 2", "0 0 13 7 .875", "0 0 13 7 5.125",
                       "0 0 13 7 2.1", "0 0 13 7 2 extra", "0 0 13 7", "-1 0 1 1 2"};
  for (size_t i = 0; i < sizeof bad / sizeof bad[0]; i++) {
    snprintf(text, sizeof text, "%s@ceiling %s\n", HALL, bad[i]);
    CHECK(!level_parse(&lv, text, err, sizeof err));
    CHECK(strstr(err, "Invalid @ceiling"));
    level_free(&lv);
  }
  CHECK(level_parse(&lv, "11111\n1P..1\n11111\n", err, sizeof err));
  lv.heights[6] = .5f;
  CHECK(!lv.has_ceilings && level_ceiling_at(&lv, 1, 1) == 1.5f);
  level_free(&lv);
  CHECK(level_parse(&lv, HALL, err, sizeof err));
  CHECK(lv.has_ceilings && !lv.has_heights && level_ceiling(&lv, 2.5f, 3.5f) == 3);
  lv.decoration_count = lv.wall_box_count = lv.hanging_count = 0;
  lv.torch_count = lv.fire_light_count = 0;
  Session session = {.lives = 3, .next_life = 40000};
  Game *g = game_create(&lv, 0, &session, NULL);
  CHECK(game_line_of_sight_3d(g, 1.5f, 3.5f, 2, 10.5f, 3.5f, 2));
  // A low lintel occludes the upper storey, leaving the ground-level route open.
  for (int y = 0; y < lv.h; y++) lv.ceilings[y * lv.w + 6] = 1;
  CHECK(!game_line_of_sight_3d(g, 1.5f, 3.5f, 2, 10.5f, 3.5f, 2));
  CHECK(game_line_of_sight(g, 1.5f, 3.5f, 10.5f, 3.5f));
  Input in = {.weapon = -1};
  g->projectiles[g->projectile_count++] = (Projectile){2.5f, 3.5f, 1, 0, 0, 2, SHOT_TRAP, NULL, 0, 0};
  game_update(g, .15f, &in);
  CHECK(g->projectile_count == 1 && g->projectiles[0].x > 3);
  game_update(g, .4f, &in);
  CHECK(g->projectile_count == 0); // the actual lintel stops the bolt
  Renderer r; renderer_init(&r, assets);
  Image *high = image_new(SCREEN_W, SCREEN_H), *low = image_new(SCREEN_W, SCREEN_H);
  render_view(&r, low, g);
  int center = SCREEN_W / 2;
  CHECK(r.depth[70 * SCREEN_W + center] < 5);
  for (int y = 0; y < lv.h; y++) lv.ceilings[y * lv.w + 6] = 3;
  render_view(&r, high, g);
  CHECK(r.depth[70 * SCREEN_W + center] > 5);
  // Tall opaque walls fill the view up to the room's ceiling, with no fog gap.
  for (int y = 0; y < VIEW_H; y++) CHECK(isfinite(r.depth[y * SCREEN_W + center]));
  int changed = 0;
  for (int i = 0; i < SCREEN_W * VIEW_H; i++) changed += high->px[i] != low->px[i];
  CHECK(changed > 1000);
  // Ceiling-only edits invalidate a save without replacing the live game.
  snprintf(path, sizeof path, "%stest-architecture-save.dat", SDL_GetBasePath());
  CHECK(savegame_write(path, g, err, sizeof err));
  lv.ceilings[0] = 2.75f;
  Game *original = g;
  CHECK(!savegame_load(path, &lv, 1, &g, &session, err, sizeof err));
  CHECK(g == original && strstr(err, "DIFFERENT LEVEL"));
  lv.ceilings[0] = 3;
  CHECK(savegame_load(path, &lv, 1, &g, &session, err, sizeof err));
  CHECK(SDL_RemovePath(path));
  image_free(high); image_free(low); renderer_free(&r); game_destroy(g); level_free(&lv);
}

static uint8_t *walk_routes(Game *g) {
  const Level *lv = g->level;
  int cells = lv->w * lv->h, *queue = malloc((size_t)cells * sizeof(int));
  uint8_t *seen = calloc((size_t)cells, 1);
  int count = 1, start = (int)lv->start_y * lv->w + (int)lv->start_x;
  seen[start] = 1; queue[0] = start;
  static const int dirs[4][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};
  for (int i = 0; i < count; i++) {
    int x = queue[i] % lv->w, y = queue[i] / lv->w;
    for (int d = 0; d < 4; d++) {
      int nx = x + dirs[d][0], ny = y + dirs[d][1];
      if (nx < 0 || ny < 0 || nx >= lv->w || ny >= lv->h || seen[ny * lv->w + nx]) continue;
      char c = game_cell(g, nx, ny);
      if (c != '.' && c != 'D' && c != 'R' && c != 'Z' && !level_is_water(c)) continue;
      bool clear = true;
      aim(g, x + .5f, y + .5f, 1, 0);
      // Check the full crossing with the real player radius, rather than
      // teleporting between centres and overlooking blocked stair approaches.
      for (int step = 1; step <= 4; step++) {
        float px = x + .5f + dirs[d][0] * step * .25f;
        float py = y + .5f + dirs[d][1] * step * .25f;
        if (game_is_blocked(g, px, py, g->player.radius, NULL)) { clear = false; break; }
        g->player.x = px; g->player.y = py;
      }
      if (clear) { seen[ny * lv->w + nx] = 1; queue[count++] = ny * lv->w + nx; }
    }
  }
  free(queue);
  return seen;
}

// A door is an aperture through a continuous wall, with usable floor on
// both sides. Reachability alone cannot distinguish it from a floating post.
static bool passage(char c) {
  return c == '.' || level_is_water(c) || c == 'D' || c == 'R' || c == 'Z';
}

static bool jamb(char c) { return c && strchr("12345678BNL~", c); }

static int door_axis(const Level *lv, int x, int y) {
  if (x <= 0 || y <= 0 || x >= lv->w - 1 || y >= lv->h - 1) return 0;
  int at = y * lv->w + x;
  if (jamb(lv->grid[at - lv->w]) && jamb(lv->grid[at + lv->w]) &&
      passage(lv->grid[at - 1]) && passage(lv->grid[at + 1])) return 1;
  if (jamb(lv->grid[at - 1]) && jamb(lv->grid[at + 1]) &&
      passage(lv->grid[at - lv->w]) && passage(lv->grid[at + lv->w])) return lv->w;
  return 0;
}

static void test_door_framing(void) {
  const char *cases[] = {
    "1111111\n1P....1\n1..D..1\n1.....1\n1111111\n",
    "1111111\n1P.1..1\n1..D..1\n1..1..1\n1111111\n",
    "1111111\n1P.1..1\n1..D1.1\n1..1..1\n1111111\n",
  };
  for (int i = 0; i < 3; i++) {
    Level lv; char err[160]; CHECK(level_parse(&lv, cases[i], err, sizeof err));
    CHECK((door_axis(&lv, 3, 2) != 0) == (i == 1));
    level_free(&lv);
  }
}

static void screenshot(Image *fb, const char *path) {
  SDL_Surface *surface = SDL_CreateSurfaceFrom(fb->w, fb->h, SDL_PIXELFORMAT_ARGB8888,
                                              fb->px, fb->w * (int)sizeof(uint32_t));
  CHECK(surface && SDL_SaveBMP(surface, path)); SDL_DestroySurface(surface);
}

// Find a physical route while treating ordinary doors as operable. Secret
// walls, pits, mirrors and breakable masonry stay closed. Furniture and both
// sorts of barrels remain solid throughout the search and the actual walk.
static int find_path(Game *g, int target, int *path) {
  const Level *lv = g->level;
  Player original = g->player;
  float *opens = malloc((size_t)(g->door_count + 1) * sizeof(float));
  for (int i = 0; i < g->door_count; i++) {
    opens[i] = g->doors[i].open;
    if (g->doors[i].kind != 'Z') g->doors[i].open = 1;
  }
  int cells = lv->w * lv->h, *parent = malloc((size_t)cells * sizeof(int));
  int *queue = malloc((size_t)cells * sizeof(int));
  for (int i = 0; i < cells; i++) parent[i] = -1;
  int root = (int)g->player.y * lv->w + (int)g->player.x;
  int count = 1; queue[0] = root; parent[root] = root;
  const int dx[] = {1, -1, 0, 0}, dy[] = {0, 0, 1, -1};
  for (int i = 0; i < count && parent[target] < 0; i++) {
    int x = queue[i] % lv->w, y = queue[i] / lv->w;
    for (int d = 0; d < 4; d++) {
      int nx = x + dx[d], ny = y + dy[d], next = ny * lv->w + nx;
      if (nx < 0 || ny < 0 || nx >= lv->w || ny >= lv->h || parent[next] >= 0) continue;
      char c = game_cell(g, nx, ny);
      if (!passage(c)) continue;
      aim(g, x + .5f, y + .5f, 1, 0);
      bool clear = true;
      for (int step = 1; step <= 4; step++) {
        float px = x + .5f + dx[d] * step * .25f, py = y + .5f + dy[d] * step * .25f;
        if (game_is_blocked(g, px, py, g->player.radius, NULL)) { clear = false; break; }
        g->player.x = px; g->player.y = py;
      }
      if (clear) { parent[next] = queue[i]; queue[count++] = next; }
    }
  }
  int length = 0;
  if (parent[target] >= 0) {
    for (int at = target;; at = parent[at]) {
      path[length++] = at;
      if (at == root) break;
    }
    for (int i = 0; i < length / 2; i++) {
      int swap = path[i]; path[i] = path[length - i - 1]; path[length - i - 1] = swap;
    }
  }
  for (int i = 0; i < g->door_count; i++) g->doors[i].open = opens[i];
  g->player = original;
  free(opens); free(parent); free(queue); return length;
}

static void control_tick(Game *g, Input *in, float dt) {
  // This isolates traversal from combat. The regular horror and trap suites
  // test attacks; omens, notes, door timing, water drag and pickups still run.
  g->horror.calm_timer = 10000;
  for (int i = 0; i < g->trap_count; i++) { g->traps[i].state = TRAP_COOLDOWN; g->traps[i].timer = 10000; }
  for (int i = 0; i < g->level->pendulum_count; i++) g->pendulums[i].hit_cooldown = 10000;
  game_update(g, dt, in);
  CHECK(!g->player_dead);
}

static void walk_to(Game *g, int target, int *doors_used, int *turns) {
  const Level *lv = g->level;
  int *path = malloc((size_t)lv->w * lv->h * sizeof(int));
  int length = find_path(g, target, path);
  if (!length) fprintf(stderr, "Floor %d: no physical route from %.2f,%.2f to %d,%d\n",
                       g->level_index + 1, g->player.x, g->player.y, target % lv->w, target / lv->w);
  CHECK(length > 0);
  int last_dx = 0, last_dy = 0;
  for (int i = 1; i < length; i++) {
    int x = path[i] % lv->w, y = path[i] / lv->w;
    int dx = x - path[i-1] % lv->w, dy = y - path[i-1] / lv->w;
    if (last_dx || last_dy) *turns += dx != last_dx || dy != last_dy;
    last_dx = dx; last_dy = dy;
    aim(g, g->player.x, g->player.y, dx, dy);
    Door *door = game_door_at(g, x, y);
    if (door && door->kind != 'Z' && (door->open < .9f || door->state == DOOR_CLOSING)) {
      for (int t = 0; t < 150 && (door->open < .9f || door->state == DOOR_CLOSING); t++) {
        Input use = {.weapon = -1, .use = t % 15 == 0}; control_tick(g, &use, .01f);
      }
      if (door->open < .9f) fprintf(stderr, "Floor %d: door at %d,%d cannot be operated\n", g->level_index + 1, x, y);
      CHECK(door->open >= .9f); (*doors_used)++;
    }
    bool arrived = false;
    for (int frame = 0; frame < 250; frame++) {
      float rx = x + .5f - g->player.x, ry = y + .5f - g->player.y, dist = hypotf(rx, ry);
      if (dist < .003f) { arrived = true; break; }
      aim(g, g->player.x, g->player.y, rx / dist, ry / dist);
      bool wet = level_is_water(game_cell(g, (int)g->player.x, (int)g->player.y));
      float dt = minf(.01f, dist / (3.4f * (wet ? WATER_SPEED : 1)));
      Input move = {.weapon = -1, .forward = true}; control_tick(g, &move, dt);
    }
    if (!arrived) fprintf(stderr, "Floor %d: stuck walking to %d,%d at %.3f,%.3f\n", g->level_index+1, x, y, g->player.x, g->player.y);
    CHECK(arrived);
  }
  free(path);
}

static int play_floor(Level *lv, int n, const int order[3], bool report) {
  Session session = {.lives = 3, .next_life = 40000};
  Game *g = game_create(lv, n - 1, &session, NULL);
  g->enemy_count = 0;
  int keys[3], key_count = 0, gate = -1, gate_count = 0, doors_used = 0, turns = 0;
  for (int i = 0; i < lv->item_count; i++)
    if (lv->items[i].kind == ITEM_KEY) {
      CHECK(key_count < 3);
      keys[key_count++] = (int)lv->items[i].y * lv->w + (int)lv->items[i].x;
    }
  for (int i = 0; i < lv->w * lv->h; i++) if (lv->grid[i] == 'X') { gate = i; gate_count++; }
  CHECK(key_count == 3 && gate_count == 1);
  // The exit belongs on the far side of the level, away from the entry room.
  CHECK(abs(gate % lv->w - (int)lv->start_x) >= lv->w / 2 ||
        abs(gate / lv->w - (int)lv->start_y) >= lv->h / 2);
  int *path = malloc((size_t)lv->w * lv->h * sizeof(int));
  for (int i = 0; i < 3; i++) {
    int length = find_path(g, keys[i], path);
    if (length < 20) fprintf(stderr, "Floor %d: key %d,%d path length %d\n", n, keys[i]%lv->w, keys[i]/lv->w, length);
    CHECK(length >= 20); // A key must require exploring a branch of the maze.
  }
  // Walk one consistent route using real movement, E, timed door animations
  // and automatic pickups. No position warps or injected keys along the way.
  for (int i = 0; i < 3; i++) walk_to(g, keys[order[i]], &doors_used, &turns);
  CHECK(g->player.keys == 3);
  const int dx[] = {1, -1, 0, 0}, dy[] = {0, 0, 1, -1};
  bool exited = false;
  for (int d = 0; d < 4; d++) {
    int x = gate % lv->w + dx[d], y = gate / lv->w + dy[d];
    if (x < 0 || y < 0 || x >= lv->w || y >= lv->h || !passage(lv->grid[y * lv->w + x])) continue;
    if (!find_path(g, y * lv->w + x, path)) continue;
    walk_to(g, y * lv->w + x, &doors_used, &turns);
    aim(g, g->player.x, g->player.y, -dx[d], -dy[d]);
    Input use = {.weapon = -1, .use = true}; control_tick(g, &use, .01f);
    exited = g->completed;
    if (exited) break;
  }
  if (!exited || turns < 12)
    fprintf(stderr, "Floor %d: exited=%d turns=%d doors=%d keys=%d gate=%d,%d\n", n, exited, turns, doors_used, g->player.keys, gate % lv->w, gate / lv->w);
  CHECK(exited && turns >= 12);
  for (int i = 0; i < g->door_count; i++) if (g->doors[i].kind == 'Z') CHECK(g->doors[i].open == 0);
  for (int i = 0; i < lv->w * lv->h; i++) if (lv->grid[i] == '8') CHECK(game_cell(g, i % lv->w, i / lv->w) == '8');
  if (report) printf("Floor %d: control-driven key/gate route, %d turns, %d door interactions\n", n, turns, doors_used);
  free(path); game_destroy(g); return doors_used;
}

static void check_breaches(Level *lv, int n) {
  Session session = {.lives = 3, .next_life = 40000};
  Game *g = game_create(lv, n - 1, &session, NULL); g->enemy_count = 0;
  int *path = malloc((size_t)lv->w * lv->h * sizeof(int)), count = 0;
  for (int y = 1; y < lv->h - 1; y++) for (int x = 1; x < lv->w - 1; x++) {
    int at = y * lv->w + x;
    if (lv->grid[at] != '8') continue;
    int axis = door_axis(lv, x, y); CHECK(axis);
    int a = at - axis, b = at + axis;
    aim(g, a % lv->w + .5f, a / lv->w + .5f, 1, 0);
    int detour = find_path(g, b, path);
    if (detour > 0 && detour <= 5) fprintf(stderr, "Floor %d: breach at %d,%d merely widens a passage\n", n,x,y);
    CHECK(detour == 0 || detour > 5);
    game_hit_wall(g, x, y, 100);
    CHECK(find_path(g, b, path) == 3); // two actual crossings through the opening
    g->wall_health[at] = BREAKABLE_WALL_HEALTH;
    count++;
  }
  CHECK(count == 2);
  free(path); game_destroy(g);
}

static void campaign_previews(Level *lv, int n, const Assets *assets) {
  const float views[LEVEL_COUNT][4] = {
    {20.5f, 10.5f, .8f, -.6f}, {17.5f, 17.5f, 1, 0},
    {23.5f, 16.5f, 0, -1}, {17.5f, 22.5f, 0, -1}, {23.5f, 19.5f, 0, -1},
  };
  Session session = {.lives = 3, .next_life = 40000};
  Game *g = game_create(lv, n - 1, &session, NULL);
  Renderer r; renderer_init(&r, assets); Image *fb = image_new(SCREEN_W, SCREEN_H);
  char path[128];
  g->time = 1.5f;
  render_view(&r, fb, g); render_hud(&r, fb, g);
  snprintf(path, sizeof path, "build/architecture-floor-%d-1.bmp", n); screenshot(fb, path);
  const float *v = views[n-1]; aim(g, v[0], v[1], v[2], v[3]);
  CHECK(level_is_open(game_cell(g, (int)v[0], (int)v[1])));
  render_view(&r, fb, g); render_hud(&r, fb, g);
  snprintf(path, sizeof path, "build/architecture-floor-%d-2.bmp", n); screenshot(fb, path);
  memset(g->visited, 1, (size_t)lv->w * lv->h); render_map(&r, fb, g, NULL);
  snprintf(path, sizeof path, "build/architecture-map-%d.bmp", n); screenshot(fb, path);
  g->enemy_count = 0;
  for (int i = 0; i < g->door_count; i++) {
    Door *door = &g->doors[i]; int axis = door_axis(lv, door->x, door->y);
    CHECK(axis);
    int dx = axis == 1, dy = axis != 1;
    aim(g, door->x + .5f - dx * 1.45f, door->y + .5f - dy * 1.45f, dx, dy);
    // At tight corners inspect from the other approach if it offers more room.
    if (!level_is_open(game_cell(g, (int)g->player.x, (int)g->player.y)))
      aim(g, door->x + .5f + dx * 1.45f, door->y + .5f + dy * 1.45f, -dx, -dy);
    for (int open = 0; open <= 1; open++) {
      door->open = open;
      render_view(&r, fb, g);
      snprintf(path, sizeof path, "build/maze-door-%d-%02d-%d.bmp", n, i+1, open); screenshot(fb, path);
    }
    door->open = 0;
  }
  image_free(fb); renderer_free(&r); game_destroy(g);
}

static void test_campaign(const Assets *assets, bool screenshots) {
  for (int n = 1; n <= LEVEL_COUNT; n++) {
    char path[96], err[160]; snprintf(path, sizeof path, "levels/%d.txt", n);
    char *text = SDL_LoadFile(path, NULL); CHECK(text);
    Level lv; bool parsed = level_parse(&lv, text, err, sizeof err);
    if (!parsed) fprintf(stderr, "Floor %d: %s\n", n, err);
    CHECK(parsed); SDL_free(text);
    CHECK(lv.has_heights && lv.has_ceilings && lv.key_count == 3);
    CHECK(lv.horror.chapter == n && lv.horror.note_count >= 3 && lv.horror.haven_count >= 3);
    CHECK(lv.world.cells && lv.world.monk_count >= 1 && lv.world.landmark_count >= 2);
    int open = 0, narrow = 0;
    for (int y = 1; y < lv.h-1; y++) for (int x = 1; x < lv.w-1; x++) {
      if (!passage(lv.grid[y*lv.w+x])) continue;
      if (world_outside(&lv, x, y)) continue;
      int exits = passage(lv.grid[y*lv.w+x-1]) + passage(lv.grid[y*lv.w+x+1]) +
                  passage(lv.grid[(y-1)*lv.w+x]) + passage(lv.grid[(y+1)*lv.w+x]);
      open++; narrow += exits <= 2;
    }
    CHECK(narrow >= 100 && narrow * 3 >= open); // Long passages contrast with the church and houses.
    for (int i = 0; i < lv.door_count; i++) {
      DoorSpawn *d = &lv.doors[i]; int axis = door_axis(&lv, d->x, d->y);
      if (!axis) fprintf(stderr, "Floor %d: unframed/dead-end door at %d,%d\n", n, d->x, d->y);
      CHECK(axis);
      CHECK(fabsf(level_floor_at(&lv, d->x, d->y) - lv.heights[d->y*lv.w+d->x-axis]) <= LEVEL_STEP);
      CHECK(fabsf(level_floor_at(&lv, d->x, d->y) - lv.heights[d->y*lv.w+d->x+axis]) <= LEVEL_STEP);
    }
    static const int orders[6][3] = {{0,1,2}, {0,2,1}, {1,0,2}, {1,2,0}, {2,0,1}, {2,1,0}};
    int door_uses = 0;
    for (int i = 0; i < 6; i++) door_uses += play_floor(&lv, n, orders[i], i == 0);
    CHECK(door_uses > 0);
    printf("Floor %d: all six key orders completed\n", n);
    check_breaches(&lv, n);
    Session session = {.lives = 3, .next_life = 40000};
    Game *g = game_create(&lv, n-1, &session, NULL); g->enemy_count = 0;
    for (int i = 0; i < g->door_count; i++) g->doors[i].open = 1;
    uint8_t *seen = walk_routes(g);
    for (int i = 0; i < lv.item_count; i++) CHECK(seen[(int)lv.items[i].y*lv.w+(int)lv.items[i].x]);
    for (int i = 0; i < lv.horror.note_count; i++) {
      int at = lv.horror.notes[i].y*lv.w+lv.horror.notes[i].x;
      if (world_monk_note(&lv, i)) CHECK(seen[at-1] || seen[at+1] || seen[at-lv.w] || seen[at+lv.w]);
      else CHECK(seen[at]);
    }
    for (int i = 0; i < lv.horror.haven_count; i++) CHECK(seen[lv.horror.havens[i].y*lv.w+lv.horror.havens[i].x]);
    free(seen); game_destroy(g);
    if (screenshots) campaign_previews(&lv, n, assets);
    level_free(&lv);
  }
}

int main(int argc, char **argv) {
  CHECK(SDL_Init(0)); random_seed(12345);
  Assets assets; assets_build(&assets);
  test_ceilings(&assets); test_door_framing();
  test_campaign(&assets, argc > 1 && !strcmp(argv[1], "--screenshots"));
  SDL_Quit(); puts("Maze door framing, real traversal and ceiling checks passed."); return 0;
}
