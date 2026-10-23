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
    "11111111111\n"
    "1P..8.....1\n"
    "11118..8..1\n"
    "1...8.....1\n"
    "1Q..88..g.1\n"
    "1.........1\n"
    "11111111111\n";

typedef struct Fixture {
  Level level;
  Session session;
  Game *game;
} Fixture;

static void setup(Fixture *f) {
  memset(f, 0, sizeof *f);
  char err[128];
  CHECK(level_parse(&f->level, ARENA, err, sizeof err));
  f->level.decoration_count = 0;
  f->session.next_life = 40000;
  f->session.lives = 3;
  f->game = game_create(&f->level, 0, &f->session, NULL);
  f->game->enemies[0].state = ES_PAIN;
  f->game->enemies[0].timer = 100;
}

static void teardown(Fixture *f) {
  game_destroy(f->game);
  level_free(&f->level);
}

static void tick(Game *g, int frames) {
  Input input = {.weapon = -1};
  for (int i = 0; i < frames; i++) game_update(g, 0.01f, &input);
}

static void shoot(Game *g) {
  Input input = {.weapon = -1, .fire = true};
  game_update(g, 0.01f, &input);
}

static void test_weapons_and_movement(void) {
  for (int weapon = 0; weapon < WEAPON_COUNT; weapon++) {
    Fixture f;
    setup(&f);
    Game *g = f.game;
    int cell = f.level.w + 4;
    g->player.weapon = weapon;
    g->player.has_arbalest = true;
    CHECK(game_is_blocked(g, 4.5f, 1.5f, g->player.radius, NULL));
    CHECK(!game_line_of_sight(g, 1.5f, 1.5f, 6.5f, 1.5f));
    if (weapon == WEAPON_DAGGER) {
      shoot(g); // 2.5 cells away: out of reach
      tick(g, 50);
      CHECK(g->wall_health[cell] == BREAKABLE_WALL_HEALTH);
      g->player.x = 3.5f;
    }
    int ammo = g->player.ammo, shots = 0;
    while (game_cell(g, 4, 1) == '8' && shots < 5) {
      shoot(g);
      if (weapon != WEAPON_DAGGER) CHECK(g->wall_health[cell] > 0); // damage on arrival
      tick(g, 50);
      shots++;
    }
    CHECK(game_cell(g, 4, 1) == '.' && shots >= 3 && shots <= 5);
    CHECK(g->player.ammo == ammo - (weapon == WEAPON_DAGGER ? 0 : shots));
    CHECK(!game_is_blocked(g, 4.5f, 1.5f, g->player.radius, NULL));
    CHECK(!game_is_blocked(g, 4.5f, 1.5f, g->enemies[0].radius, &g->enemies[0]));
    CHECK(game_line_of_sight(g, 1.5f, 1.5f, 6.5f, 1.5f));
    CHECK(f.level.grid[cell] == '8'); // the reusable level stays unchanged
    tick(g, 100);
    CHECK(g->wall_dust[cell] == 0);
    game_destroy(g);
    f.game = game_create(&f.level, 0, &f.session, NULL);
    CHECK(game_cell(f.game, 4, 1) == '8'); // retry resets damage
    teardown(&f);
  }
}

static void test_projectile_cover(void) {
  for (int scenario = 0; scenario < 6; scenario++) {
    Fixture f;
    setup(&f);
    Game *g = f.game;
    int cell = f.level.w + 4;
    if (scenario == 0) {
      f.level.grid[f.level.w + 3] = '1';
      shoot(g); // an ordinary wall stops the bolt first
    } else if (scenario == 1) {
      g->barrels[0] = (Barrel){3, 1.5f, false, false, 0};
      shoot(g); // an ordinary barrel stops the bolt first
    } else if (scenario == 2) {
      g->projectiles[g->projectile_count++] = (Projectile){2, 1.5f, 1, 0, 0, 0.44f, SHOT_TRAP, NULL, 0, 0};
    } else if (scenario == 3) {
      game_loose_bolt(g, 7.5f, 1.5f, -1, 0, SHOT_AT_PLAYER, NULL, 12);
    } else if (scenario == 4) {
      g->projectile_count = MAX_PROJECTILES;
      for (int i = 0; i < MAX_PROJECTILES; i++)
        g->projectiles[i] = (Projectile){1.5f, 1.5f, -1, 0, 0, 0.44f, SHOT_MISS, NULL, 0, 0};
      shoot(g); // a full projectile pool must still resolve the wall hit
    } else {
      g->enemies[0].x = 8.5f;
      g->enemies[0].y = 1.5f;
      game_loose_bolt(g, 1.5f, 1.5f, 1, 0, SHOT_AT_ENEMY, &g->enemies[0], 12);
    }
    tick(g, 60);
    if (g->wall_health[cell] != BREAKABLE_WALL_HEALTH - (scenario >= 2 ? WALL_BOLT_DAMAGE : 0))
      fprintf(stderr, "Projectile scenario %d: wall health %d\n", scenario, g->wall_health[cell]);
    CHECK(g->wall_health[cell] == BREAKABLE_WALL_HEALTH - (scenario >= 2 ? WALL_BOLT_DAMAGE : 0));
    CHECK(g->player.health == 100 && g->projectile_count == 0);
    CHECK(g->enemies[0].hp == g->enemies[0].stats->hp);
    teardown(&f);
  }
  Fixture f;
  setup(&f);
  Game *g = f.game;
  g->barrel_count = 0;
  game_hit_wall(g, 4, 4, BREAKABLE_WALL_HEALTH - WALL_BOLT_DAMAGE);
  game_loose_bolt(g, 1.5f, 4.5f, 1, 0, SHOT_MISS, NULL, WALL_BOLT_DAMAGE);
  Input input = {.weapon = -1};
  game_update(g, 0.5f, &input); // fast frames cannot skip a wall or damage the next one
  CHECK(game_cell(g, 4, 4) == '.' && game_cell(g, 5, 4) == '8');
  CHECK(g->wall_health[4 * f.level.w + 5] == BREAKABLE_WALL_HEALTH && g->projectile_count == 0);
  teardown(&f);
}

static void test_blast_and_indestructible_cells(void) {
  for (int covered = 0; covered < 2; covered++) {
    Fixture f;
    setup(&f);
    Game *g = f.game;
    g->barrels[0].x = covered ? 1.5f : 3.5f;
    g->player.x = g->enemies[0].x = 6.5f;
    g->player.y = g->enemies[0].y = 4.5f;
    if (covered) f.level.grid[4 * f.level.w + 3] = '1';
    int hp = g->enemies[0].hp;
    game_hit_barrel(g, 0);
    CHECK((game_cell(g, 4, 4) == '.') == !covered);
    CHECK(game_cell(g, 5, 4) == '8'); // a second layer absorbs no damage
    CHECK(g->wall_health[4 * f.level.w + 5] == BREAKABLE_WALL_HEALTH);
    CHECK(g->player.health == 100 && g->enemies[0].hp == hp);
    teardown(&f);
  }
  Fixture f;
  setup(&f);
  for (const char *c = "1234567BDRZXL~N"; *c; c++) {
    f.level.grid[f.level.w + 3] = *c;
    game_hit_wall(f.game, 3, 1, 1000);
    CHECK(game_cell(f.game, 3, 1) == *c);
  }
  game_hit_wall(f.game, -1, -1, 1000);
  game_hit_wall(f.game, 4, 1, 0);
  CHECK(game_cell(f.game, 4, 1) == '8');
  teardown(&f);
  char err[128];
  Level edge;
  CHECK(level_parse(&edge, "88888\n8P.88\n88888\n", err, sizeof err));
  CHECK(edge.grid[0] == '1' && edge.grid[edge.w + 4] == '1' && edge.grid[edge.w + 3] == '8');
  level_free(&edge);
}

static void rewrite_save(const char *path, bool legacy) {
  FILE *file = fopen(path, "rb");
  CHECK(file && fseek(file, 0, SEEK_END) == 0);
  long size = ftell(file);
  CHECK(size > 20 && fseek(file, 0, SEEK_SET) == 0);
  uint8_t *data = malloc((size_t)size);
  CHECK(data && fread(data, 1, (size_t)size, file) == (size_t)size);
  fclose(file);
  if (legacy) {
    data[8] = 1;
    size -= 4; // remove the v4 camera easing field; this arena has no walls or pendulums
  } else data[size - 16] = BREAKABLE_WALL_HEALTH + 1; // invalid last wall health
  uint32_t checksum = 2166136261u;
  for (long i = 0; i < size - 4; i++) checksum = (checksum ^ data[i]) * 16777619u;
  for (int i = 0; i < 4; i++) data[size - 4 + i] = (uint8_t)(checksum >> (i * 8));
  file = fopen(path, "wb");
  CHECK(file && fwrite(data, 1, (size_t)size, file) == (size_t)size);
  CHECK(fclose(file) == 0);
  free(data);
}

static void test_save_load(void) {
  Fixture f;
  setup(&f);
  char err[128];
  char path[1024];
  int length = snprintf(path, sizeof path, "%stest-wall-save.dat", SDL_GetBasePath());
  CHECK(length > 0 && (size_t)length < sizeof path);
  game_hit_wall(f.game, 4, 1, WALL_BOLT_DAMAGE);
  game_hit_wall(f.game, 4, 3, 100);
  f.game->player.x = 4.5f;
  f.game->player.y = 3.5f; // saving while standing inside a breach is valid
  CHECK(savegame_write(path, f.game, err, sizeof err));
  game_hit_wall(f.game, 4, 1, 100);
  CHECK(savegame_load(path, &f.level, 1, &f.game, &f.session, err, sizeof err));
  CHECK(f.game->wall_health[f.level.w + 4] == BREAKABLE_WALL_HEALTH - WALL_BOLT_DAMAGE);
  CHECK(game_cell(f.game, 4, 1) == '8' && game_cell(f.game, 4, 3) == '.');
  CHECK(f.game->wall_dust[3 * f.level.w + 4] == WALL_DUST_TIME);
  CHECK(f.game->wall_health[2 * f.level.w + 7] == BREAKABLE_WALL_HEALTH);
  CHECK(f.game->player.x == 4.5f && f.game->player.y == 3.5f);
  CHECK(!game_is_blocked(f.game, 4.5f, 3.5f, f.game->player.radius, NULL));
  rewrite_save(path, false);
  Game *previous = f.game;
  uint64_t rng = random_state();
  CHECK(!savegame_load(path, &f.level, 1, &f.game, &f.session, err, sizeof err));
  CHECK(f.game == previous && random_state() == rng);
  CHECK(f.game->wall_health[f.level.w + 4] == BREAKABLE_WALL_HEALTH - WALL_BOLT_DAMAGE);
  CHECK(SDL_RemovePath(path));
  teardown(&f);

  memset(&f, 0, sizeof f);
  CHECK(level_parse(&f.level, "11111\n1P..1\n11111\n", err, sizeof err));
  f.session = (Session){.lives = 3, .next_life = 40000};
  f.game = game_create(&f.level, 0, &f.session, NULL);
  f.game->player.health = 61;
  CHECK(savegame_write(path, f.game, err, sizeof err));
  rewrite_save(path, true);
  f.game->player.health = 17;
  CHECK(savegame_load(path, &f.level, 1, &f.game, &f.session, err, sizeof err));
  CHECK(f.game->player.health == 61);
  CHECK(SDL_RemovePath(path));
  teardown(&f);
}

static void screenshot(const Image *image, const char *name) {
  SDL_Surface *surface = SDL_CreateSurfaceFrom(image->w, image->h, SDL_PIXELFORMAT_ARGB8888,
                                              image->px, image->w * (int)sizeof(uint32_t));
  CHECK(surface && SDL_SaveBMP(surface, name));
  SDL_DestroySurface(surface);
}

static void test_rendering(bool screenshots) {
  Fixture f;
  setup(&f);
  Assets assets;
  assets_build(&assets);
  Renderer renderer;
  renderer_init(&renderer, &assets);
  Image *fb = image_new(SCREEN_W, SCREEN_H);
  for (int stage = 0; stage < 4; stage++) {
    render_view(&renderer, fb, f.game);
    render_hud(&renderer, fb, f.game);
    CHECK(fabsf(renderer.zbuffer[SCREEN_W / 2] - (stage < 3 ? 2.5f : 8.5f)) < 0.01f);
    if (screenshots) {
      char name[64];
      snprintf(name, sizeof name, "build/wall-stage-%d.bmp", stage);
      screenshot(fb, name);
    }
    f.game->visited[f.level.w + 4] = 1;
    render_map(&renderer, fb, f.game, NULL);
    int cs = mini(8, (VIEW_H - 40) / f.level.h);
    int mx = SCREEN_W / 2 - f.level.w * cs / 2 + 4 * cs + cs / 2;
    int my = (VIEW_H - f.level.h * cs) / 2 + cs + cs / 2;
    CHECK(fb->px[my * fb->w + mx] == (stage < 3 ? 0xffa58a66u : 0xff1d2a34u));
    if (stage < 3) game_hit_wall(f.game, 4, 1, WALL_BOLT_DAMAGE);
  }
  tick(f.game, 100);
  render_view(&renderer, fb, f.game);
  render_hud(&renderer, fb, f.game);
  if (screenshots) screenshot(fb, "build/wall-rubble.bmp");
  image_free(fb);
  renderer_free(&renderer);
  teardown(&f);
}

static void reachable_cells(Game *g, uint8_t *seen) {
  const Level *lv = g->level;
  float original_x = g->player.x, original_y = g->player.y;
  int *queue = malloc(sizeof(int) * lv->w * lv->h);
  int start = (int)g->player.y * lv->w + (int)g->player.x, count = 1;
  queue[0] = start;
  seen[start] = 1;
  static const int dirs[4][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};
  for (int next = 0; next < count; next++) {
    int at = queue[next], x = at % lv->w, y = at / lv->w;
    g->player.x = x + .5f; g->player.y = y + .5f;
    for (int d = 0; d < 4; d++) {
      int nx = x + dirs[d][0], ny = y + dirs[d][1];
      if (nx < 0 || ny < 0 || nx >= lv->w || ny >= lv->h) continue;
      int cell = ny * lv->w + nx;
      if (seen[cell] || game_is_blocked(g, nx + 0.5f, ny + 0.5f, g->player.radius, NULL)) continue;
      seen[cell] = 1;
      queue[count++] = cell;
    }
  }
  g->player.x = original_x; g->player.y = original_y;
  free(queue);
}

static void test_shipped_levels(void) {
  for (int n = 1; n <= LEVEL_COUNT; n++) {
    char path[32], text[8192], err[128];
    snprintf(path, sizeof path, "levels/%d.txt", n);
    FILE *file = fopen(path, "rb");
    CHECK(file);
    size_t len = fread(text, 1, sizeof text - 1, file);
    CHECK(!ferror(file) && feof(file));
    text[len] = 0;
    fclose(file);
    Level lv;
    CHECK(level_parse(&lv, text, err, sizeof err));
    CHECK(lv.key_count == 3);
    Session session = {.next_life = 40000};
    Game *g = game_create(&lv, n - 1, &session, NULL);
    g->enemy_count = 0;
    for (int i = 0; i < g->door_count; i++) g->doors[i].open = 1;
    for (int i = 0; i < g->barrel_count; i++) if (g->barrels[i].powder) g->barrels[i].destroyed = true;
    uint8_t *before = calloc((size_t)lv.w * lv.h, 1), *after = calloc((size_t)lv.w * lv.h, 1);
    reachable_cells(g, before);
    int walls = 0;
    for (int y = 0; y < lv.h; y++) {
      for (int x = 0; x < lv.w; x++) {
        if (lv.grid[y * lv.w + x] != '8') continue;
        CHECK(x > 0 && y > 0 && x < lv.w - 1 && y < lv.h - 1);
        CHECK(before[y * lv.w + x - 1] || before[y * lv.w + x + 1] ||
              before[(y - 1) * lv.w + x] || before[(y + 1) * lv.w + x]);
        game_hit_wall(g, x, y, 100);
        float original_x = g->player.x, original_y = g->player.y;
        g->player.x = x + .5f; g->player.y = y + .5f;
        CHECK(!game_is_blocked(g, x + 0.5f, y + 0.5f, g->player.radius, NULL));
        g->player.x = original_x; g->player.y = original_y;
        walls++;
      }
    }
    CHECK(walls >= 2);
    reachable_cells(g, after);
    for (int i = 0; i < lv.item_count; i++) {
      int cell = (int)lv.items[i].y * lv.w + (int)lv.items[i].x;
      CHECK(!before[cell] || after[cell]);
      if (lv.items[i].kind == ITEM_KEY) CHECK(before[cell] && after[cell]);
    }
    free(before);
    free(after);
    game_destroy(g);
    level_free(&lv);
  }
}

int main(int argc, char **argv) {
  CHECK(SDL_Init(0));
  random_seed(12345);
  test_weapons_and_movement();
  test_projectile_cover();
  test_blast_and_indestructible_cells();
  test_save_load();
  test_shipped_levels();
  test_rendering(argc > 1 && !strcmp(argv[1], "--screenshots"));
  SDL_Quit();
  puts("Destructible wall checks passed.");
  return 0;
}
