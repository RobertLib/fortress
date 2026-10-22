// Exercise the actual menu actions and input handlers without opening a window.
#define SDL_MAIN_HANDLED
#define main fortress_entry
#include "../src/main.c"
#undef main

#define CHECK(expr) do { if (!(expr)) { \
  fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #expr); exit(1); \
} } while (0)

static int find_item(Menu menu, const char *label) {
  for (int i = 0; i < menu.count; i++) if (!strcmp(menu.items[i].label(), label)) return i;
  CHECK(false);
  return -1;
}

static void click_item(Menu menu, MenuSpan span, const char *label) {
  int index = find_item(menu, label);
  MenuLayout layout = menu_layout(menu, span);
  float y = layout.cy + index * layout.line_h - layout.size * 0.3f;
  CHECK(menu_hit_test(menu, span, W / 2, y) == index);
  handle_state_hover(W / 2, y);
  handle_state_click(W / 2, y);
}

static void check_layout(Menu menu, MenuSpan span) {
  MenuLayout l = menu_layout(menu, span);
  CHECK(l.cy - l.size * 0.9f > span.top);
  CHECK(l.cy + (menu.count - 1) * l.line_h + l.size * 0.35f < span.bottom);
  for (int i = 0; i < menu.count; i++) CHECK(font_measure(menu.items[i].label(), l.size, FONT_BOLD) < W - 60);
}

int main(void) {
  CHECK(SDL_Init(0));
  save_path = malloc(sizeof "test-savegame-ui.dat");
  CHECK(save_path);
  strcpy(save_path, "test-savegame-ui.dat");
  SDL_RemovePath(save_path);
  CHECK(load_levels());

  set_state(ST_INTRO);
  on_key_down(SDL_SCANCODE_3);
#if FORTRESS_DEBUG_TOOLS
  CHECK(state == ST_LEVEL_START && game && game->level_index == 2);
  char *warp_args[] = {"fortress", "--level", "3"};
  char *short_warp_args[] = {"fortress", "-l", "2"};
  CHECK(warp_arg(3, warp_args) == 3);
  CHECK(warp_arg(3, short_warp_args) == 2);
  game_destroy(game);
  game = NULL;
#else
  CHECK(state == ST_INTRO && !game);
#endif
  set_state(ST_INTRO);
  on_key_down(SDL_SCANCODE_RETURN);
  CHECK(state == ST_MENU);

  check_layout(main_menu(), MAIN_MENU_SPAN);
  click_item(main_menu(), MAIN_MENU_SPAN, "LOAD GAME");
  CHECK(state == ST_MENU && !game && notice_timer > 0);

  new_game(2, DIFFICULTY_HARD);
  begin_level();
  game->player.health = 61;
  session.score = 1234;
  on_key_down(SDL_SCANCODE_F5);
  CHECK(state == ST_PLAYING && !strcmp(notice, "GAME SAVED"));
  game->player.health = 17;
  session.score = 999;
  input.forward = input.fire = input.use = true;
  input.mouse_dx = 123;
  input.weapon = WEAPON_DAGGER;
  on_key_down(SDL_SCANCODE_F9);
  CHECK(state == ST_PLAYING && game->level_index == 2 && game->player.health == 61 && session.score == 1234);
  CHECK(!input.forward && !input.fire && !input.use && input.mouse_dx == 0 && input.weapon == -1);

  on_key_down(SDL_SCANCODE_ESCAPE);
  CHECK(state == ST_PAUSED);
  check_layout(pause_menu(), PAUSE_MENU_SPAN);
  game->player.health = 52;
  click_item(pause_menu(), PAUSE_MENU_SPAN, "SAVE GAME");
  CHECK(state == ST_PAUSED && !strcmp(notice, "GAME SAVED"));
  game->player.health = 12;
  click_item(pause_menu(), PAUSE_MENU_SPAN, "LOAD GAME");
  CHECK(state == ST_PLAYING && game->player.health == 52);

  act_quit_to_menu();
  check_layout(main_menu(), MAIN_MENU_SPAN);
  // CONTINUE resumes memory, LOAD restores disk. A new run leaves disk intact.
  game->player.health = 37;
  act_continue();
  CHECK(game->player.health == 37);
  new_game(0, DIFFICULTY_EASY);
  begin_level();
  act_quit_to_menu();
  main_menu_index = find_item(main_menu(), "LOAD GAME");
  on_key_down(SDL_SCANCODE_RETURN);
  CHECK(state == ST_PLAYING && game->player.health == 52 && game->level_index == 2);
  CHECK(session.difficulty == DIFFICULTY_HARD && selected_difficulty == DIFFICULTY_HARD);

  game->player_dead = true;
  game->player.health = 0;
  set_state(ST_DIED);
  death_delay = 1;
  on_key_down(SDL_SCANCODE_F9);
  CHECK(state == ST_PLAYING && !game->player_dead && game->player.health == 52 && death_delay == 0);
  set_state(ST_GAME_OVER);
  on_key_down(SDL_SCANCODE_F9);
  CHECK(state == ST_PLAYING && game->player.health == 52);

  // A fresh process can reach the slot through the main menu.
  game_destroy(game);
  game = NULL;
  memset(&session, 0, sizeof session);
  resumable = false;
  set_state(ST_MENU);
  click_item(main_menu(), MAIN_MENU_SPAN, "LOAD GAME");
  CHECK(state == ST_PLAYING && game->player.health == 52 && session.score == 1234);

  on_key_down(SDL_SCANCODE_ESCAPE);
  CHECK(SDL_SaveFile(save_path, "broken", 6));
  Game *previous = game;
  click_item(pause_menu(), PAUSE_MENU_SPAN, "LOAD GAME");
  CHECK(state == ST_PAUSED && game == previous && game->player.health == 52 && notice_timer > 0);

  // Render the changed screens as part of the UI smoke check.
  fb = image_new(W, H);
  draw_main_menu();
  draw_pause_overlay();
  draw_controls();
  image_free(fb);
  font_shutdown();
  game_destroy(game);
  for (int i = 0; i < levels_loaded; i++) level_free(&levels[i]);
  SDL_RemovePath(save_path);
  free(save_path);
  SDL_Quit();
  puts("Save/load keyboard, mouse, menus, restart and recovery UI checks passed.");
  return 0;
}
