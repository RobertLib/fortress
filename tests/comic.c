// Exercise real story transitions, input, focus and procedural frames.
#define SDL_MAIN_HANDLED
#define main fortress_entry
#include "../src/main.c"
#undef main

#define CHECK(expr) do { if (!(expr)) { \
  fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #expr); exit(1); \
} } while (0)

static void test_intro(void) {
  selected_difficulty = DIFFICULTY_NORMAL;
  choose_difficulty(0);
  handle_state_key(SDL_SCANCODE_DOWN); // HARD
  handle_state_key(SDL_SCANCODE_RETURN);
  CHECK(state == ST_STORY_INTRO && comic.panel == 0);
  CHECK(session.difficulty == DIFFICULTY_HARD && session.lives == 3);
  CHECK(game->time == 0 && game->level_index == 0);
  float x = game->player.x, y = game->player.y;
  input.forward = input.fire = true;
  update(2);
  CHECK(game->time == 0 && game->player.x == x && game->player.y == y);
  CHECK(comic.age[0] == 2 && state == ST_STORY_INTRO);

  input_focused = false;
  update(10);
  handle_state_key(SDL_SCANCODE_RETURN);
  handle_state_click(580, 380);
  CHECK(comic.age[0] == 2 && comic.panel == 0);
  input_focused = true;
  handle_state_key(SDL_SCANCODE_SPACE);
  CHECK(comic.panel == 1 && comic.age[1] == 0);
  handle_state_click(210, 150);
  CHECK(comic.panel == 2);
  handle_state_key(SDL_SCANCODE_RETURN);
  CHECK(state == ST_LEVEL_START && state_timer == 0);
  CHECK(!input.forward && !input.fire && !input.use && input.weapon == -1);
  CHECK(game->time == 0 && session.difficulty == DIFFICULTY_HARD);

  begin_story(COMIC_INTRO);
  handle_state_key(SDL_SCANCODE_ESCAPE);
  CHECK(state == ST_LEVEL_START);
  begin_story(COMIC_INTRO);
  handle_state_click(580, 380);
  CHECK(state == ST_LEVEL_START);

  // Timed playback leaves playing to the floor screen; returning to the
  // game never replays the prologue.
  begin_story(COMIC_INTRO);
  update(7);
  CHECK(comic.panel == 1 && fabsf(comic.age[1] - .5f) < .001f);
  update(20);
  CHECK(state == ST_LEVEL_START && game->time == 0);
  begin_level();
  act_quit_to_menu();
  act_continue();
  CHECK(state == ST_PLAYING);
  restart_level();
  CHECK(state == ST_PLAYING); // Checkpoint retry does not replay the prologue.
  choose_difficulty(2);
  start_difficulty(DIFFICULTY_EASY);
  CHECK(state == ST_LEVEL_START && game->level_index == 2);
}

static void test_outro(void) {
  new_game(LEVEL_COUNT - 1, DIFFICULTY_NORMAL);
  session.score = 12345;
  session.total_kills = 17;
  session.total_time = 101;
  game->time = 23;
  int enemies = game->total_enemies;
  game->completed = true;
  resumable = true;
  set_state(ST_LEVEL_COMPLETE);
  handle_state_key(SDL_SCANCODE_RETURN);
  CHECK(state == ST_STORY_OUTRO && comic.panel == 0 && !resumable);
  CHECK(session.total_time == 124 && session.total_enemies == enemies);
  handle_state_key(SDL_SCANCODE_RIGHT);
  CHECK(comic.panel == 1);
  handle_state_key(SDL_SCANCODE_RETURN);
  CHECK(comic.panel == 2);
  handle_state_key(SDL_SCANCODE_RETURN);
  CHECK(state == ST_VICTORY && session.score == 12345 && session.total_kills == 17);
  CHECK(session.total_time == 124 && session.total_enemies == enemies);
  handle_state_key(SDL_SCANCODE_RETURN);
  CHECK(state == ST_MENU && main_menu_index == 0);
  set_state(ST_VICTORY);
  handle_state_key(SDL_SCANCODE_ESCAPE);
  CHECK(state == ST_MENU && main_menu_index == 0);
  begin_story(COMIC_OUTRO);
  handle_state_key(SDL_SCANCODE_ESCAPE);
  CHECK(state == ST_VICTORY && session.total_time == 124);
  begin_story(COMIC_OUTRO);
  update(30);
  CHECK(state == ST_VICTORY && session.total_time == 124);

  new_game(0, DIFFICULTY_NORMAL);
  next_level();
  CHECK(state == ST_LEVEL_START && game->level_index == 1);
  set_state(ST_GAME_OVER);
  handle_state_key(SDL_SCANCODE_RETURN);
  CHECK(state == ST_DIFFICULTY);
  set_state(ST_GAME_OVER);
  handle_state_key(SDL_SCANCODE_ESCAPE);
  CHECK(state == ST_MENU && main_menu_index == 0);
}

static uint64_t frame_hash(void) {
  uint64_t hash = 1469598103934665603ull;
  // Hash the illustrations, excluding captions, counters and progress bars.
  for (int y = 45; y < 284; y++)
    for (int x = 18; x < 622; x++) hash = (hash ^ fb->px[y * W + x]) * 1099511628211ull;
  return hash;
}

static void save_preview(const char *name) {
  SDL_Surface *surface = SDL_CreateSurfaceFrom(W, H, SDL_PIXELFORMAT_ARGB8888, fb->px, W * 4);
  CHECK(surface && SDL_SaveBMP(surface, name));
  SDL_DestroySurface(surface);
}

static void test_art(bool previews) {
  comic_art_build(&comic_art);
  fb = image_new(W, H);
  for (int kind = 0; kind < COMIC_KIND_COUNT; kind++) {
    begin_story((ComicKind)kind);
    for (int panel = 0; panel < COMIC_PANELS; panel++) {
      update(1);
      draw_story();
      uint64_t before = frame_hash();
      update(1);
      draw_story();
      CHECK(before != frame_hash());
      CHECK(fb->clip_x0 == 0 && fb->clip_y0 == 0 && fb->clip_x1 == W && fb->clip_y1 == H);
      if (previews) {
        char path[64];
        snprintf(path, sizeof path, "comic-%s-%d.bmp", kind == COMIC_INTRO ? "intro" : "outro", panel + 1);
        save_preview(path);
      }
      handle_state_key(SDL_SCANCODE_RETURN);
    }
    CHECK(state == (kind == COMIC_INTRO ? ST_LEVEL_START : ST_VICTORY));
  }
  draw_main_menu();
  if (previews) save_preview("comic-menu.bmp");
  image_free(fb);
  fb = NULL;
  comic_art_free(&comic_art);
  font_shutdown();
}

int main(int argc, char **argv) {
  CHECK(SDL_Init(0));
  CHECK(load_levels());
  test_intro();
  test_outro();
  test_art(argc > 1 && !strcmp(argv[1], "--previews"));
  game_destroy(game);
  for (int i = 0; i < levels_loaded; i++) level_free(&levels[i]);
  SDL_Quit();
  puts("Comic playback, input, focus, run transitions and animated art checks passed.");
  return 0;
}
