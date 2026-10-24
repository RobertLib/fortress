// End-to-end artwork inventory, ownership and full presentation smoke check.
#define SDL_MAIN_HANDLED
#define main fortress_entry
#include "../src/main.c"
#undef main

#define CHECK(e)                                                                                                       \
  do {                                                                                                                 \
    if (!(e)) {                                                                                                        \
      fprintf(stderr, "%s:%d: %s (%s)\n", __FILE__, __LINE__, #e, SDL_GetError());                                     \
      exit(1);                                                                                                         \
    }                                                                                                                  \
  } while (0)
static bool previews;
static void save_image(const Image *im, const char *name) {
  if (!previews) return;
  char path[256];
  snprintf(path, sizeof path, "build/art-%s.bmp", name);
  SDL_Surface *s = SDL_CreateSurfaceFrom(im->w, im->h, SDL_PIXELFORMAT_ARGB8888, im->px, im->w * 4);
  CHECK(s && SDL_SaveBMP(s, path));
  SDL_DestroySurface(s);
}
static void shot(const char *name) {
  render();
  save_image(fb, name);
  CHECK(fb->clip_x0 == 0 && fb->clip_y0 == 0 && fb->clip_x1 == W && fb->clip_y1 == H);
}
static uint64_t signature(const Image *im) {
  uint64_t h = 1469598103934665603ull;
  for (int i = 0; i < im->w * im->h; i++)
    h = (h ^ im->px[i]) * 1099511628211ull;
  return h;
}
static void inventory(void) {
  size_t slots = 0;
  for (size_t o = 0; o + sizeof(Image *) <= sizeof assets; o += sizeof(Image *)) {
    Image *im;
    memcpy(&im, (const unsigned char *)&assets + o, sizeof im);
    CHECK(im && im->w > 0 && im->h > 0);
    int ink = 0;
    for (int i = 0; i < im->w * im->h; i++) {
      uint32_t p = im->px[i], a = p >> 24;
      CHECK(((p >> 16) & 255) <= a && ((p >> 8) & 255) <= a && (p & 255) <= a);
      if (a > 4) ink++;
    }
    if (ink < mini(5, im->w * im->h))
      fprintf(stderr, "Artwork slot %zu (%dx%d) is blank: %d covered pixels\n", slots, im->w, im->h, ink);
    CHECK(ink >= mini(5, im->w * im->h));
    slots++;
  }
  CHECK(slots > 400);
  for (int t = 0; t < ENEMY_TYPE_COUNT; t++)
    for (int p = 1; p < 3; p++)
      CHECK(signature(assets.enemies[t].pose[0]) != signature(assets.enemies[t].pose[p]));
  for (int t=ENEMY_GUARD;t<=ENEMY_CAPTAIN;t++) for (int view=0;view<VIEW_COUNT;view++) {
    for (int frame=1;frame<3;frame++)
      CHECK(signature(assets.enemies[t].view[0][view])!=signature(assets.enemies[t].view[frame][view]));
    if (view) CHECK(signature(assets.enemies[t].view[0][0])!=signature(assets.enemies[t].view[0][view]));
  }
  for (int w = 0; w < WEAPON_COUNT; w++)
    for (int p = 1; p < 3; p++)
      CHECK(signature(assets.weapons[w][0]) != signature(assets.weapons[w][p]));
  for (int t = 0; t < SURFACE_THEME_COUNT; t++)
    for (int p = 0; p < SURFACE_MIPS; p++)
      CHECK(assets.floors[t][p]->w == (TEX >> p));
  for (int f = 0; f < FIREPLACE_FRAMES; f++) {
    const Image *im = assets.fireplaces[f], *stone = assets.hangings[HANG_FIREPLACE];
    int moving = 0;
    for (int y = 0; y < TEX; y++)
      for (int x = 0; x < TEX; x++) {
        int i = y * TEX + x;
        if (im->px[i] == stone->px[i]) continue;
        // The stonework, andirons and log bed stay fixed; only the firebox moves.
        CHECK((x + .5f) / TEX > .34f && (x + .5f) / TEX < .68f);
        CHECK((y + .5f) / TEX > .45f && (y + .5f) / TEX < .86f);
        moving++;
      }
    CHECK(moving > 500);
    for (int other = 0; other < f; other++)
      CHECK(signature(im) != signature(assets.fireplaces[other]));
  }
  printf("Validated %zu artwork slots: no missing assets, valid premultiplied alpha and animation frames.\n", slots);
}
static void fireplace_rendering(void) {
  static const char *arena =
      "111111111\n"
      "1.......1\n"
      "1.......1\n"
      "1.......1\n"
      "1.P.....1\n"
      "1.......1\n"
      "1.......1\n"
      "1.......1\n"
      "111111111\n";
  Level lv;
  char err[128];
  CHECK(level_parse(&lv, arena, err, sizeof err));
  lv.decoration_count = lv.wall_box_count = lv.hanging_count = 0;
  lv.torch_count = lv.fire_light_count = 0;
  Session session = {.lives = 3, .next_life = 40000};
  Game *g = game_create(&lv, 0, &session, NULL);
  g->player.weapon = WEAPON_DAGGER;
  Image *base = image_new(W, H), *moving = image_new(W, H);
  Image *sheet = previews ? image_new(5 * 320, 3 * 200) : NULL;
  float *depth = malloc(sizeof(float) * W * VIEW_H);
  CHECK(depth);
  const Hanging walls[] = {{0, 4, 1, 0, HANG_FIREPLACE, 0}, {8, 4, -1, 0, HANG_FIREPLACE, 0},
                           {4, 0, 0, 1, HANG_FIREPLACE, 0}, {4, 8, 0, -1, HANG_FIREPLACE, 0}};
  const float views[] = {-80, -45, 0, 45, 80};
  for (int elevated = 0; elevated < 2; elevated++) {
    lv.has_heights = elevated;
    for (int i = 0; i < lv.w * lv.h; i++) lv.heights[i] = elevated * .5f;
    for (int wall = 0; wall < 4; wall++) {
      Hanging hg = walls[wall];
      lv.hangings[0] = hg;
      float cx = hg.x + .5f + hg.dx * .5f, cy = hg.y + .5f + hg.dy * .5f;
      lv.decorations[0] = (Decoration){cx + hg.dx * .06f, cy + hg.dy * .06f, .26, 0, DECO_FIREPLACE};
      lv.decoration_count = 1;
      for (int view = 0; view < 5; view++) {
        image_fill(base, 0); image_fill(moving, 0); image_fill(fb, 0);
        float angle = views[view] * (float)M_PI / 180;
        g->player.x = cx + 1.35f * (hg.dx * cosf(angle) - hg.dy * sinf(angle));
        g->player.y = cy + 1.35f * (hg.dy * cosf(angle) + hg.dx * sinf(angle));
        float dx = cx + hg.dx * .09f - g->player.x, dy = cy + hg.dy * .09f - g->player.y;
        float length = hypotf(dx, dy);
        g->player.dir_x = dx / length; g->player.dir_y = dy / length;
        g->player.plane_x = -g->player.dir_y * .66f; g->player.plane_y = g->player.dir_x * .66f;
        g->time = 0;
        lv.hanging_count = 1;
        render_view(&renderer, base, g);
        memcpy(depth, renderer.depth, sizeof(float) * W * VIEW_H);
        lv.hanging_count = 0;
        render_view(&renderer, moving, g);
        int solid = 0, recessed = 0;
        for (int i = 0; i < W * VIEW_H; i++) {
          if (fabsf(depth[i] - renderer.depth[i]) < .0001f) continue;
          CHECK(isfinite(depth[i]) && depth[i] >= .05f && depth[i] < 2);
          float camera_x = 2.0f * (i % W) / W - 1;
          float rx = g->player.dir_x + g->player.plane_x * camera_x;
          float ry = g->player.dir_y + g->player.plane_y * camera_x;
          float normal = (g->player.x + rx * depth[i] - cx) * hg.dx +
                         (g->player.y + ry * depth[i] - cy) * hg.dy;
          CHECK(normal >= -.141f && normal <= .046f); // shallow frame, fire behind the wall
          solid += depth[i] < renderer.depth[i];
          recessed += depth[i] > renderer.depth[i] + .002f;
        }
        CHECK(solid > 100); // grazing views retain the shallow stone frame
        if (view > 0 && view < 4) CHECK(recessed > 50);
        // Its collision marker must never draw a second camera-facing hearth.
        lv.decoration_count = 0;
        render_view(&renderer, fb, g);
        CHECK(signature(fb) == signature(moving));
        lv.decoration_count = 1;
        lv.hanging_count = 1;
        g->time = 1.0f / 18;
        render_view(&renderer, moving, g);
        CHECK(!memcmp(depth, renderer.depth, sizeof(float) * W * VIEW_H));
        if (view > 0 && view < 4) CHECK(signature(base) != signature(moving));
        // Same world time gives the same fire, including a complete loop.
        g->time = FIREPLACE_FRAMES / 18.0f;
        render_view(&renderer, moving, g);
        CHECK(signature(base) == signature(moving));
        if (sheet && !wall && !elevated) {
          for (int phase = 0; phase < 3; phase++) {
            g->time = phase / 6.0f;
            render_view(&renderer, moving, g);
            render_hud(&renderer, moving, g);
            if (!phase && view == 2) save_image(moving, "fireplace-front");
            if (!phase && view == 4) save_image(moving, "fireplace-side");
            gfx_blit(sheet, moving, 0, 0, W, H, view * 320, phase * 200, 320, 200, 1);
          }
        }
      }
    }
  }
  if (sheet) save_image(sheet, "fireplace-views");
  // A low foreground ledge still occludes the lower half of the cavity.
  // Carving its supporting wall must not overwrite closer sector geometry.
  lv.hangings[0] = walls[0];
  g->player.x = 3.5f; g->player.y = 4.5f;
  g->player.dir_x = -1; g->player.dir_y = 0;
  g->player.plane_x = 0; g->player.plane_y = -.66f;
  lv.heights[4 * lv.w + 2] = .75f;
  lv.hanging_count = 0;
  render_view(&renderer, moving, g);
  memcpy(depth, renderer.depth, sizeof(float) * W * VIEW_H);
  lv.hanging_count = 1;
  render_view(&renderer, base, g);
  int occluded = 0;
  for (int i = 0; i < W * VIEW_H; i++) {
    if (depth[i] >= 2.4f) continue; // the supporting fireplace wall is 2.5 tiles away
    CHECK(base->px[i] == moving->px[i] && renderer.depth[i] == depth[i]);
    occluded++;
  }
  CHECK(occluded > 1000);
  lv.heights[4 * lv.w + 2] = .5f;
  // An intervening wall hides both the solid surround and animated fire.
  for (int y = 1; y < lv.h - 1; y++) lv.grid[y * lv.w + 2] = '1';
  lv.hanging_count = 1;
  render_view(&renderer, base, g);
  lv.hanging_count = 0;
  render_view(&renderer, moving, g);
  CHECK(signature(base) == signature(moving));
  free(depth);
  image_free(base); image_free(moving); image_free(sheet);
  game_destroy(g);
  level_free(&lv);
}
static void tile(Image *sheet, const Image *im, int col, int row, const char *name) {
  float x = col * 128, y = row * 144;
  for (int yy = 0; yy < 128; yy++)
    for (int xx = 0; xx < 128; xx++)
      gfx_fill_rect(sheet, x + xx, y + yy, 1, 1, ((xx / 8 + yy / 8) % 2) ? rgb(29, 41, 49) : rgb(24, 34, 42));
  gfx_blit(sheet, im, 0, 0, im->w, im->h, x, y, 128, 128, 1);
  font_draw(sheet, name, x + 4, y + 139, 9, FONT_REGULAR, ALIGN_LEFT, rgb(204, 196, 173));
}
static void galleries(void) {
  if (!previews) return;
  Image *sheet = image_new(POSE_COUNT * 128, ENEMY_TYPE_COUNT * 144);
  image_fill(sheet, 0xff0a1118);
  const char *names[] = {"GUARD", "KNIGHT", "CAPTAIN", "BATS", "HOUND", "RAT", "SPIDER"};
  for (int t = 0; t < ENEMY_TYPE_COUNT; t++)
    for (int p = 0; p < POSE_COUNT; p++) {
      char label[64];
      snprintf(label, sizeof label, "%s / %02d", names[t], p);
      tile(sheet,t==ENEMY_KNIGHT?assets.knight_melee.pose[p]:assets.enemies[t].pose[p],p,t,label);
    }
  save_image(sheet, "enemy-poses");
  image_free(sheet);
  sheet = image_new(8 * 128, 8 * 144);
  image_fill(sheet, 0xff0a1118);
  int n = 0;
  for (int i = 0; i < WT_COUNT; i++) {
    char label[32];
    snprintf(label, sizeof label, "WALL %02d", i);
    tile(sheet, assets.walls[i], n % 8, n / 8, label);
    n++;
  }
  for (int i = 0; i < SURFACE_THEME_COUNT; i++)
    for (int c = 0; c < 2; c++) {
      char label[32];
      snprintf(label, sizeof label, "%s %d", c ? "CEILING" : "FLOOR", i);
      tile(sheet, c ? assets.ceilings[i][0] : assets.floors[i][0], n % 8, n / 8, label);
      n++;
    }
  for (int i = 0; i < DECO_KIND_COUNT; i++) {
    char label[32];
    snprintf(label, sizeof label, "DECOR %02d", i);
    tile(sheet, assets.decor[i], n % 8, n / 8, label);
    n++;
  }
  for (int i = 0; i < ITEM_KIND_COUNT; i++) {
    char label[32];
    snprintf(label, sizeof label, "PICKUP %02d", i);
    tile(sheet, assets.items[i], n % 8, n / 8, label);
    n++;
  }
  for (int i = 0; i < 3; i++) {
    tile(sheet, assets.barrels[i], n % 8, n / 8, "BARREL");
    n++;
  }
  for (int i = 0; i < 4; i++) {
    tile(sheet, assets.warden[i], n % 8, n / 8, "JAILER");
    n++;
  }
  save_image(sheet, "world-inventory");
  image_free(sheet);
  sheet = image_new(8 * 128, 8 * 144);
  image_fill(sheet, 0xff0a1118);
  n = 0;
  for (int w = 0; w < WEAPON_COUNT; w++)
    for (int f = 0; f < 3; f++) {
      tile(sheet, assets.weapons[w][f], n % 8, n / 8, "VIEWMODEL");
      n++;
    }
  for (int f = 0; f < 4; f++) {
    tile(sheet, assets.torches[f], n % 8, n / 8, "TORCH");
    n++;
  }
  for (int f = 0; f < EXPLOSION_FRAMES; f++) {
    tile(sheet, assets.explosion[f], n % 8, n / 8, "EXPLOSION");
    n++;
  }
  for (int f = 0; f < WALL_DUST_FRAMES; f++) {
    tile(sheet, assets.wall_dust[f], n % 8, n / 8, "STONE DUST");
    n++;
  }
  for (int f = 0; f < PENDULUM_FRAMES; f += 6) {
    tile(sheet, assets.pendulum[f], n % 8, n / 8, "BLADE SWING");
    n++;
  }
  for (int i = 0; i < HANG_KIND_COUNT; i++) {
    tile(sheet, assets.hangings[i], n % 8, n / 8, "WALL HANGING");
    n++;
  }
  for (int i = 0; i < 3; i++) {
    tile(sheet, assets.horror_traces[i], n % 8, n / 8, "FLOOR TRACE");
    n++;
  }
  for (int i = 0; i < TRAP_SPRITE_COUNT; i++) {
    tile(sheet, assets.trap[i], n % 8, n / 8, "TRAP / BOLT");
    n++;
  }
  save_image(sheet, "effects-and-weapons");
  image_free(sheet);
  sheet = image_new(8 * 128, 16 * 144);
  image_fill(sheet, 0xff0a1118);
  n = 0;
  for (int t = ENEMY_GUARD; t <= ENEMY_SPIDER; t++) if (t!=ENEMY_BAT)
    for (int v = 0; v < VIEW_COUNT; v++)
      for (int p = 0; p < 3; p++) {
        char label[64];
        snprintf(label, sizeof label, "%s / V%d F%d", names[t], v, p);
        tile(sheet, assets.enemies[t].view[p][v], n % 8, n / 8, label);
        n++;
      }
  for (int t = 0; t < PORTRAIT_SITTERS; t++)
    for (int f = 0; f < PORTRAIT_ROWS * PORTRAIT_COLS; f++) {
      char label[64];
      snprintf(label, sizeof label, "PORTRAIT %d / %d", t, f);
      tile(sheet, assets.portraits[t][f], n % 8, n / 8, label);
      n++;
    }
  for (int b = 0; b < BOX_KIND_COUNT; b++) {
    tile(sheet, assets.boxes[b].front, n % 8, n / 8, "BOX FRONT");
    n++;
    tile(sheet, assets.boxes[b].side, n % 8, n / 8, "BOX SIDE");
    n++;
    tile(sheet, assets.boxes[b].lid, n % 8, n / 8, "BOX LID");
    n++;
  }
  for (int k = 0; k < 2; k++)
    for (int f = 0; f < WATER_FRAMES; f += 4) {
      tile(sheet, assets.water[k][f], n % 8, n / 8, "WATER");
      n++;
    }
  tile(sheet, assets.prisoner, n % 8, n / 8, "PRISONER");
  n++;
  tile(sheet, assets.haunt_shadow, n % 8, n / 8, "SHADOW");
  n++;
  for (int w = 0; w < WEAPON_COUNT; w++) {
    tile(sheet, assets.weapon_icons[w], n % 8, n / 8, "WEAPON ICON");
    n++;
  }
  save_image(sheet, "directions-and-details");
  image_free(sheet);
}
static void presentation(void) {
  clock_time = 2;
  set_state(ST_LOADING);
  shot("loading");
  set_state(ST_MENU);
  shot("menu");
  set_state(ST_DIFFICULTY);
  shot("difficulty");
  set_state(ST_CONTROLS);
  shot("controls");
  set_state(ST_QUIT);
  shot("quit");
  for (int k = 0; k < COMIC_KIND_COUNT; k++)
    for (int p = 0; p < COMIC_PANELS; p++) {
      comic_start(&comic, (ComicKind)k);
      comic.panel = p;
      comic.age[p] = 2;
      set_state(k ? ST_STORY_OUTRO : ST_STORY_INTRO);
      char name[64];
      snprintf(name, sizeof name, "story-%d-%d", k, p);
      shot(name);
    }
  for (int i = 0; i < LEVEL_COUNT; i++) {
    new_game(i, DIFFICULTY_NORMAL);
    set_state(ST_LEVEL_START);
    char name[64];
    snprintf(name, sizeof name, "chapter-%d", i + 1);
    shot(name);
    begin_level();
    game->time = 2;
    snprintf(name, sizeof name, "floor-%d-start", i + 1);
    shot(name);
    const Haven *v = &game->level->horror.havens[0];
    game->player.x = v->x + .5f;
    game->player.y = v->y + .5f;
    game->player.dir_x = 1;
    game->player.dir_y = 0;
    game->player.plane_x = 0;
    game->player.plane_y = .66f;
    snprintf(name, sizeof name, "floor-%d-hall", i + 1);
    shot(name);
  }
  set_state(ST_PAUSED);
  shot("pause");
  set_state(ST_DIED);
  shot("death");
  set_state(ST_LEVEL_COMPLETE);
  shot("floor-complete");
  set_state(ST_GAME_OVER);
  shot("game-over");
  set_state(ST_VICTORY);
  shot("victory");
  set_state(ST_PLAYING);
  game->show_map = true;
  memset(game->visited, 1, (size_t)game->level->w * game->level->h);
  shot("map");
  game->show_map = false;
  game->horror.reading = 0;
  shot("prisoner-note");
  game->horror.reading = -1;
  game->player.health = 18;
  game->player.keys = 3;
  game->player.venom = 8;
  shot("venom-and-keys");
  strcpy(error_msg, "Missing artwork: assets/guard.bmp");
  set_state(ST_ERROR);
  shot("missing-art-error");
}
int main(int argc, char **argv) {
  CHECK(SDL_Init(0));
  previews = argc > 1 && !strcmp(argv[1], "--screenshots");
  CHECK(assets_build(&assets));
  CHECK(comic_art_build(&comic_art));
  CHECK(load_levels());
  inventory();
  galleries();
  fb = image_new(W, H);
  renderer_init(&renderer, &assets);
  fireplace_rendering();
  presentation();
  game_destroy(game);
  game = NULL;
  renderer_free(&renderer);
  image_free(fb);
  fb = NULL;
  comic_art_free(&comic_art);
  assets_free(&assets);
  for (size_t o = 0; o + sizeof(Image *) <= sizeof assets; o += sizeof(Image *)) {
    Image *im;
    memcpy(&im, (const unsigned char *)&assets + o, sizeof im);
    CHECK(!im);
  }
  for (int i = 0; i < levels_loaded; i++)
    level_free(&levels[i]);
  font_shutdown();
  SDL_Quit();
  puts("Complete artwork inventory, ownership and presentation checks passed.");
  return 0;
}
