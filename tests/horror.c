#include "render.h"
#include "savegame.h"

#include <SDL3/SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(expr) do { if (!(expr)) { \
  fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #expr); exit(1); \
} } while (0)

static const char *ARENA =
    "@horror 1\n"
    "@haven 2 2 1.2\n"
    "@note 2 3 White candles bar the Jailer.|A bolt buys time. Break his line of sight.\n"
    "@scene cell 3 1 7 1 0\n"
    "@scene shadow 5 5 8 5 1\n"
    "@scene armor 10 7 11 7 1 13 7\n"
    "@trace blood 7 5\n"
    "11111111111111111\n"
    "1P....B.........1\n"
    "1...............1\n"
    "1...............1\n"
    "1111111D111111111\n"
    "1...............1\n"
    "1....!..........1\n"
    "1...............1\n"
    "1...............1\n"
    "1..K....K....K..1\n"
    "11111111111111111\n";

typedef struct Fixture { Level level; Session session; Game *game; } Fixture;
static char err[192];

static void parse(Level *lv, const char *text) {
  if (!level_parse(lv, text, err, sizeof err)) { fprintf(stderr, "%s\n", err); exit(1); }
}

static void setup(Fixture *f) {
  memset(f, 0, sizeof *f);
  parse(&f->level, ARENA);
  f->level.decoration_count = f->level.wall_box_count = f->level.hanging_count = 0;
  f->level.fire_light_count = 0;
  f->session = (Session){.next_life = 40000, .lives = 3};
  f->game = game_create(&f->level, 0, &f->session, NULL);
}

static void teardown(Fixture *f) { game_destroy(f->game); level_free(&f->level); }
static void tick(Game *g, float dt) { Input in = {.weapon = -1}; game_update(g, dt, &in); }
static void run_horror(Game *g, float seconds) {
  for (float t = 0; t < seconds; t += .025f) horror_update(g, .025f);
}
static void aim(Game *g, float x, float y, float dx, float dy) {
  g->player.x = x; g->player.y = y;
  g->player.dir_x = dx; g->player.dir_y = dy;
  g->player.plane_x = -dy * .66f; g->player.plane_y = dx * .66f;
}
static Warden *hunt(Game *g, float x, float y) {
  Warden *w = &g->horror.warden;
  *w = (Warden){.state = WARDEN_HUNT, .x = x, .y = y, .next_x = x, .next_y = y,
                .remaining = 35, .has_target = true, .target_x = g->player.x, .target_y = g->player.y};
  return w;
}

static void test_chapters_and_validation(void) {
  for (int n = 1; n <= LEVEL_COUNT; n++) {
    char path[32]; snprintf(path, sizeof path, "levels/%d.txt", n);
    char *text = SDL_LoadFile(path, NULL);
    CHECK(text);
    Level lv; parse(&lv, text); SDL_free(text);
    CHECK(lv.horror.chapter == n && lv.horror.note_count == 2 && lv.horror.haven_count == 2);
    CHECK(lv.horror.scene_count >= 3 && lv.horror.trace_count >= 5 && lv.key_count == 3);
    Session s = {.lives = 3, .next_life = 40000};
    Game *g = game_create(&lv, n - 1, &s, NULL);
    for (int i = 0; i < lv.horror.haven_count; i++) {
      Haven *v = &lv.horror.havens[i];
      CHECK(horror_in_haven(g, v->x + .5f, v->y + .5f));
      aim(g, v->x + .5f, v->y + .5f, 1, 0);
      CHECK(!game_is_blocked(g, v->x + .5f, v->y + .5f, .22f, NULL));
    }
    // All authored story fragments and refuges can be reached on foot.
    int cells = lv.w * lv.h, *queue = malloc((size_t)cells * sizeof(int));
    bool *seen = calloc((size_t)cells, sizeof(bool));
    int head = 0, tail = 0, start = (int)lv.start_y * lv.w + (int)lv.start_x;
    queue[tail++] = start; seen[start] = true;
    static const int dx[] = {1, 0, -1, 0}, dy[] = {0, 1, 0, -1};
    while (head < tail) {
      int cell = queue[head++], x = cell % lv.w, y = cell / lv.w;
      for (int k = 0; k < 4; k++) {
        int nx = x + dx[k], ny = y + dy[k];
        if (nx < 0 || ny < 0 || nx >= lv.w || ny >= lv.h) continue;
        char c = game_cell(g, nx, ny);
        if (c != '.' && !level_is_water(c) && c != 'D' && c != 'R' && c != 'Z') continue;
        int next = ny * lv.w + nx;
        if (seen[next] || fabsf(level_floor_at(&lv, nx, ny) - level_floor_at(&lv, x, y)) > LEVEL_STEP + .001f) continue;
        seen[next] = true; queue[tail++] = next;
      }
    }
    for (int i = 0; i < lv.horror.note_count; i++) CHECK(seen[lv.horror.notes[i].y * lv.w + lv.horror.notes[i].x]);
    for (int i = 0; i < lv.horror.haven_count; i++) CHECK(seen[lv.horror.havens[i].y * lv.w + lv.horror.havens[i].x]);
    free(queue); free(seen); game_destroy(g); level_free(&lv);
  }
  const char *bad[] = {"@horror 6\n", "@horror 1\n@haven 1 1 nan\n", "@horror 1\n@scene unknown 1 1 2 1 0\n",
                       "@horror 1\n@note 0 0 Lost\n", "@horror 1\n@scene armor 1 1 2 1 0\n"};
  for (int i = 0; i < 5; i++) {
    char text[512]; snprintf(text, sizeof text, "%s11111\n1P..1\n11111\n", bad[i]);
    Level lv; CHECK(!level_parse(&lv, text, err, sizeof err));
  }
  Level old;
  parse(&old, "11111\n1P..1\n11111\n");
  CHECK(old.horror.chapter == 0);
  level_free(&old);
}

static void test_scenes_and_light(void) {
  Fixture f; setup(&f); Game *g = f.game;
  aim(g, 3.5f, 1.5f, 1, 0);
  horror_update(g, .01f);
  CHECK(g->horror.scenes_fired == 1 && g->horror.scenes_changed == 0);
  g->player.keys = 1;
  horror_update(g, .01f);
  CHECK(g->horror.keys_seen == 1 && g->horror.pending_hunts == 1 && g->horror.blackout == 8);
  CHECK(g->horror.scenes_changed == 0); // the guard does not vanish while watched
  float x, y, alpha;
  CHECK(horror_scene_visible(g, 0, &x, &y, &alpha));
  g->player.dir_x = -1;
  horror_update(g, .01f);
  CHECK(g->horror.scenes_changed == 1 && !horror_scene_visible(g, 0, &x, &y, &alpha));
  float light = horror_torch_strength(g, g->player.x, g->player.y);
  g->horror.blackout = 6;
  CHECK(horror_torch_strength(g, g->player.x, g->player.y) < light * .2f);
  aim(g, 5.5f, 5.5f, 1, 0);
  g->horror.calm_timer = 100;
  horror_update(g, .01f);
  CHECK(horror_scene_visible(g, 1, &x, &y, &alpha));
  run_horror(g, 4);
  CHECK(!horror_scene_visible(g, 1, &x, &y, &alpha));
  CHECK(g->horror.scene_timer[1] == 0); // remaining on the trigger never replays it
  CHECK(g->horror.pending_hunts == 1); // no extra hunt for an already-seen key
  teardown(&f);
}

static void test_reading_pauses_world(void) {
  Fixture f; setup(&f); Game *g = f.game;
  aim(g, 2.5f, 3.4f, 0, -1);
  CHECK(horror_near_note(g) == 0);
  Input in = {.weapon = -1, .use = true};
  game_update(g, .01f, &in);
  CHECK(g->horror.reading == 0 && g->horror.notes_read == 1);
  float time = g->time, x = g->player.x;
  int ammo = g->player.ammo;
  g->player.venom = 5; g->player.venom_timer = .01f;
  Warden *w = hunt(g, 4.5f, 3.5f);
  in = (Input){.weapon = WEAPON_DAGGER, .forward = true, .fire = true};
  game_update(g, 1, &in);
  CHECK(g->time == time && g->player.x == x && g->player.health == 100 && g->player.venom == 5);
  CHECK(g->player.ammo == ammo && g->player.weapon == WEAPON_CROSSBOW && w->x == 4.5f);
  in.use = true;
  game_update(g, .01f, &in);
  CHECK(g->horror.reading == -1 && !in.use && g->time == time);
  teardown(&f);
}

static void test_return_journey(void) {
  Fixture f; setup(&f); Game *g = f.game;
  aim(g, 7.5f, 8.5f, 0, 1);
  Door *d = game_door_at(g, 7, 4);
  g->visited[4 * f.level.w + 7] = 1;
  g->player.keys = 1;
  horror_update(g, .025f);
  CHECK(horror_holds_door(g, 0) && d->state == DOOR_OPENING);
  g->horror.calm_timer = 100;
  for (int i = 0; i < 400; i++) tick(g, .025f);
  CHECK(d->state == DOOR_OPEN && d->open == 1); // still open when the player returns
  char path[1024]; snprintf(path, sizeof path, "%stest-horror-door.dat", SDL_GetBasePath());
  CHECK(savegame_write(path, g, err, sizeof err));
  CHECK(savegame_load(path, &f.level, 1, &f.game, &f.session, err, sizeof err));
  g = f.game;
  tick(g, .1f);
  CHECK(horror_holds_door(g, 0) && g->doors[0].open == 1);
  CHECK(SDL_RemovePath(path));
  teardown(&f);
}

static void test_spawn_and_warning(void) {
  Fixture f; setup(&f); Game *g = f.game;
  aim(g, 8.5f, 7.5f, 1, 0);
  run_horror(g, 15);
  CHECK(g->horror.warden.state == WARDEN_DORMANT);
  g->player.keys = 1;
  horror_update(g, .025f);
  g->horror.calm_timer = 0;
  horror_update(g, .025f);
  Warden *w = &g->horror.warden;
  CHECK(w->state == WARDEN_WARNING && g->horror.pending_hunts == 0);
  CHECK(hypotf(w->x - g->player.x, w->y - g->player.y) >= 5);
  run_horror(g, WARDEN_WARNING_TIME - .15f);
  CHECK(w->state == WARDEN_WARNING && g->player.health == 100);
  run_horror(g, .2f);
  CHECK(w->state == WARDEN_HUNT);
  // One key permits one encounter, even after the hunt and its rest finish.
  w->remaining = .01f;
  run_horror(g, 60);
  CHECK(w->state == WARDEN_DORMANT && g->horror.pending_hunts == 0 && g->horror.keys_seen == 1);
  teardown(&f);
}

static void test_warden_combat_and_escape(void) {
  Fixture f; setup(&f); Game *g = f.game;
  aim(g, 5.5f, 7.5f, 1, 0);
  Warden *w = hunt(g, 8.5f, 7.5f);
  Input fire = {.weapon = -1, .fire = true};
  game_update(g, .01f, &fire);
  CHECK(g->player.ammo == 15 && g->projectile_count == 1 && g->projectiles[0].kind == SHOT_AT_WARDEN);
  g->player.weapon = WEAPON_DAGGER; // a bolt keeps its full stagger after switching
  tick(g, .2f);
  CHECK(w->state == WARDEN_STAGGER && g->kills == 0 && g->session->score == 0);
  float x = w->x;
  run_horror(g, 1);
  CHECK(w->state == WARDEN_STAGGER && w->x == x);
  run_horror(g, 1.5f);
  CHECK(w->state == WARDEN_HUNT);
  w = hunt(g, 6.3f, 7.5f);
  horror_update(g, .025f);
  CHECK(w->attack_timer > 1.4f && g->player.health == 100);
  // A clearly signalled swing can be dodged before it lands.
  g->player.x = 3.5f;
  run_horror(g, 1.1f);
  CHECK(g->player.health == 100);
  aim(g, w->x - .75f, w->y, 1, 0);
  w->attack_timer = 0;
  run_horror(g, 1.1f);
  CHECK(g->player.health == 72);

  // A refuge bars the Jailer and lets his pursuit expire without damage.
  aim(g, 2.5f, 2.5f, 1, 0);
  g->player.health = 100;
  w = hunt(g, 5.5f, 2.5f);
  run_horror(g, 9);
  CHECK(g->player.health == 100 && w->state == WARDEN_DORMANT);
  CHECK(!horror_in_haven(g, w->x, w->y));
  teardown(&f);
}

static void test_path_doors_and_cover(void) {
  for (int vertical = 0; vertical < 2; vertical++) {
  Fixture f; setup(&f); Game *g = f.game;
  if (vertical) {
    f.level.has_heights = true;
    for (int y = 0; y < f.level.h; y++)
      for (int x = 0; x < f.level.w; x++) f.level.heights[y * f.level.w + x] = clampf((7 - y) * LEVEL_STEP, 0, .5f);
  }
  aim(g, 8.5f, 2.5f, 0, 1);
  Warden *w = hunt(g, 8.5f, 7.5f);
  Door *d = game_door_at(g, 7, 4);
  CHECK(d && !game_line_of_sight(g, w->x, w->y, g->player.x, g->player.y));
  for (int i = 0; i < 640 && w->y > 3.5f; i++) {
    horror_noise(g, g->player.x, g->player.y, 1);
    tick(g, .025f);
  }
  CHECK(d->open > .85f && w->y < 3.5f && g->player.health == 100);
  CHECK(level_floor(&f.level, w->x, w->y) == (vertical ? .5f : 0));
  // A closed wooden door and solid masonry stop a shot aimed at the Jailer.
  w = hunt(g, 7.5f, 5.5f);
  d->open = 0; d->state = DOOR_CLOSED;
  aim(g, 7.5f, 3.5f, 0, 1);
  CHECK(!isfinite(horror_warden_aim(g, 24)));
  CHECK(g->player.health == 100);
  teardown(&f);
  }
  Fixture f; setup(&f); Game *g = f.game;
  aim(g, 5.5f, 5.5f, 0, 1);
  Warden *w = hunt(g, 5.5f, 7.5f);
  for (int i = 0; i < 200; i++) {
    horror_update(g, .025f);
    CHECK(game_cell(g, (int)w->x, (int)w->y) != '!');
  }
  CHECK(w->y < 6.5f); // reaches the other side by walking around the pit
  teardown(&f);
}

static void test_save_roundtrip(void) {
  Fixture f; setup(&f); Game *g = f.game;
  aim(g, 3.5f, 1.5f, -1, 0);
  g->player.keys = 1;
  horror_update(g, .01f);
  g->horror.notes_read = 1;
  g->horror.reading = 0;
  Warden *w = hunt(g, 9.5f, 7.5f);
  w->state = WARDEN_STAGGER; w->state_timer = 1.23f;
  game_loose_bolt(g, g->player.x, g->player.y, 1, 0, SHOT_AT_WARDEN, NULL, 0);
  HorrorState snapshot = g->horror;
  char path[1024]; snprintf(path, sizeof path, "%stest-horror.dat", SDL_GetBasePath());
  CHECK(savegame_write(path, g, err, sizeof err));
  g->horror = (HorrorState){.distance = g->horror.distance, .queue = g->horror.queue};
  CHECK(savegame_load(path, &f.level, 1, &f.game, &f.session, err, sizeof err));
  g = f.game;
  CHECK(g->horror.scenes_fired == snapshot.scenes_fired && g->horror.scenes_changed == snapshot.scenes_changed);
  CHECK(g->horror.keys_seen == 1 && g->horror.pending_hunts == 1 && g->horror.blackout == 8);
  CHECK(g->horror.notes_read == 1 && g->horror.reading == 0 && g->horror.warden.state_timer == 1.23f);
  CHECK(g->projectile_count == 1 && g->projectiles[0].kind == SHOT_AT_WARDEN && !g->projectiles[0].target);
  g->horror.reading = -1;
  horror_update(g, .025f);
  CHECK(g->horror.pending_hunts == 1);
  g->horror.warden.state = 99;
  CHECK(!savegame_write(path, g, err, sizeof err));
  CHECK(savegame_load(path, &f.level, 1, &f.game, &f.session, err, sizeof err));
  Game *old = f.game;
  f.level.horror.notes[0].text[0] = 'X';
  CHECK(!savegame_load(path, &f.level, 1, &f.game, &f.session, err, sizeof err) && old == f.game);
  CHECK(SDL_RemovePath(path));
  teardown(&f);
}

static void screenshot(const Image *im, const char *path) {
  SDL_Surface *s = SDL_CreateSurfaceFrom(im->w, im->h, SDL_PIXELFORMAT_ARGB8888, im->px, im->w * 4);
  CHECK(s && SDL_SaveBMP(s, path)); SDL_DestroySurface(s);
}

static int differences(const Image *a, const Image *b) {
  int n = 0; for (int i = 0; i < SCREEN_W * VIEW_H; i++) n += a->px[i] != b->px[i]; return n;
}

static void test_rendering(bool screenshots) {
  Assets assets; assets_build(&assets);
  Renderer r; renderer_init(&r, &assets);
  Image *fb = image_new(SCREEN_W, SCREEN_H), *other = image_new(SCREEN_W, SCREEN_H);
  Fixture f; setup(&f); Game *g = f.game;
  aim(g, 4.5f, 7.5f, 1, 0);
  hunt(g, 7.5f, 7.5f);
  render_view(&r, fb, g);
  g->horror.warden.state = WARDEN_DORMANT;
  render_view(&r, other, g);
  CHECK(differences(fb, other) > 500);
  if (screenshots) { render_hud(&r, fb, g); screenshot(fb, "build/horror-jailer.bmp"); }
  aim(g, 7.5f, 3.5f, 0, 1); // closed door hides the whole figure
  hunt(g, 7.5f, 5.5f);
  render_view(&r, fb, g);
  g->horror.warden.state = WARDEN_DORMANT;
  render_view(&r, other, g);
  CHECK(differences(fb, other) == 0);
  aim(g, 7.5f, 7.5f, 0, -1);
  render_view(&r, fb, g);
  f.level.horror.trace_count = 0;
  render_view(&r, other, g);
  CHECK(differences(fb, other) > 20);
  teardown(&f);

  for (int n = 1; n <= 5; n++) {
    char path[64]; snprintf(path, sizeof path, "levels/%d.txt", n);
    char *text = SDL_LoadFile(path, NULL); CHECK(text);
    Level lv; parse(&lv, text); SDL_free(text);
    Session session = {.lives = 3, .next_life = 40000};
    g = game_create(&lv, n - 1, &session, NULL);
    g->time = 2;
    if (n == 1) {
      aim(g, 3.25f, 1.5f, 1, 0);
      render_view(&r, fb, g); render_hud(&r, fb, g);
      if (screenshots) screenshot(fb, "build/horror-cell.bmp");
    }
    for (int i = 0; i < lv.horror.note_count; i++) {
      g->horror.reading = i;
      render_view(&r, fb, g); render_hud(&r, fb, g);
      if (screenshots) { snprintf(path, sizeof path, "build/horror-note-%d-%d.bmp", n, i + 1); screenshot(fb, path); }
    }
    g->horror.reading = -1;
    const Haven *v = &lv.horror.havens[0];
    aim(g, v->x + .5f, v->y + .5f, 1, 0);
    render_view(&r, fb, g); render_hud(&r, fb, g);
    if (screenshots) { snprintf(path, sizeof path, "build/horror-floor-%d.bmp", n); screenshot(fb, path); }
    game_destroy(g); level_free(&lv);
  }
  image_free(fb); image_free(other); renderer_free(&r);
}

int main(int argc, char **argv) {
  CHECK(SDL_Init(0)); random_seed(12345);
  test_chapters_and_validation();
  test_scenes_and_light();
  test_reading_pauses_world();
  test_return_journey();
  test_spawn_and_warning();
  test_warden_combat_and_escape();
  test_path_doors_and_cover();
  test_save_roundtrip();
  test_rendering(argc > 1 && !strcmp(argv[1], "--screenshots"));
  SDL_Quit();
  puts("Horror chapters, one-shot scenes, reading, pursuit, cover, refuges, save/load and rendering checks passed.");
  return 0;
}
