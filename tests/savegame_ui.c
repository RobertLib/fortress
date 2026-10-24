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
  CHECK(menu_hit_test(menu, span, layout.cx, y) == index);
  handle_state_hover(layout.cx, y);
  handle_state_click(layout.cx, y);
}

static void check_layout(Menu menu, MenuSpan span) {
  MenuLayout l = menu_layout(menu, span);
  CHECK(l.cy - l.size * 0.9f > span.top);
  CHECK(l.cy + (menu.count - 1) * l.line_h + l.size * 0.35f < span.bottom);
  for (int i = 0; i < menu.count; i++) {
    CHECK(font_measure(menu.items[i].label(), l.size, FONT_BOLD) < span.width - 60);
    float y = l.cy + i * l.line_h - l.size * .3f;
    CHECK(menu_hit_test(menu, span, span.x + 2, y) == i);
    CHECK(menu_hit_test(menu, span, span.x + span.width - 2, y) == i);
    CHECK(menu_hit_test(menu, span, span.x - 1, y) == -1);
    CHECK(menu_hit_test(menu, span, span.x + span.width + 1, y) == -1);
  }
}

static void confirm_slot(bool saving) {
  CHECK(state==ST_SLOTS && slot_save_mode==saving);
  on_key_down(SDL_SCANCODE_RETURN);
}

int main(void) {
  CHECK(SDL_Init(0));
  save_path = malloc(sizeof "test-savegame-ui.dat");
  CHECK(save_path);
  strcpy(save_path, "test-savegame-ui.dat");
  SDL_RemovePath(save_path);
  CHECK(load_levels());

  set_state(ST_MENU);
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
  CHECK(state == ST_MENU && !game);
#endif
  set_state(ST_MENU);
  on_key_down(SDL_SCANCODE_RETURN);
  CHECK(state == ST_DIFFICULTY && !game);
  on_key_down(SDL_SCANCODE_ESCAPE);
  CHECK(state == ST_MENU && !game);
  on_key_down(SDL_SCANCODE_ESCAPE);
  CHECK(state == ST_QUIT && running);
  on_key_down(SDL_SCANCODE_ESCAPE);
  CHECK(state == ST_MENU && running);

  check_layout(main_menu(), MAIN_MENU_SPAN);
  check_layout(difficulty_menu(), DIFFICULTY_MENU_SPAN);
  CHECK(main_menu().count==5 && pause_menu().count==5);
  click_item(main_menu(), MAIN_MENU_SPAN, "LOAD GAME");
  CHECK(state==ST_SLOTS && !slot_save_mode && slot_count()==5 && !game);
  confirm_slot(false);
  CHECK(state==ST_SLOTS && !game && notice_timer>0);
  on_key_down(SDL_SCANCODE_ESCAPE); CHECK(state==ST_MENU);

  new_game(2, DIFFICULTY_HARD);
  begin_level();
  game->player.health = 61;
  session.score = 1234;
  on_key_down(SDL_SCANCODE_F5);
  CHECK(state==ST_SLOTS && slot_save_mode && slot_count()==3);
  CHECK(!slot_valid[0]); // Opening Save never writes until a slot is chosen.
  on_key_down(SDL_SCANCODE_UP); CHECK(slot_row==2);
  on_key_down(SDL_SCANCODE_DOWN); CHECK(slot_row==0);
  on_key_down(SDL_SCANCODE_RIGHT); CHECK(slot_save_mode);
  confirm_slot(true);
  CHECK(state == ST_PLAYING && !strcmp(notice, "GAME SAVED"));
  begin_story(COMIC_INTRO); // Loading also exits a story directly into the saved run.
  game->player.health = 17;
  session.score = 999;
  input.forward = input.fire = input.use = true;
  input.mouse_dx = 123;
  input.weapon = WEAPON_DAGGER;
  on_key_down(SDL_SCANCODE_F9);
  CHECK(state==ST_SLOTS && !slot_save_mode && slot_row==0);
  confirm_slot(false);
  CHECK(state == ST_PLAYING && game->level_index == 2 && game->player.health == 61 && session.score == 1234);
  CHECK(!input.forward && !input.fire && !input.use && input.mouse_dx == 0 && input.weapon == -1);

  on_key_down(SDL_SCANCODE_ESCAPE);
  CHECK(state == ST_PAUSED);
  check_layout(pause_menu(), PAUSE_MENU_SPAN);
  game->player.health = 52;
  click_item(pause_menu(), PAUSE_MENU_SPAN, "SAVE GAME");
  CHECK(state==ST_SLOTS && slot_save_mode);
  handle_state_click(100,132); // The slot itself is the save action.
  CHECK(state == ST_PAUSED && !strcmp(notice, "GAME SAVED"));
  game->player.health = 12;
  click_item(pause_menu(), PAUSE_MENU_SPAN, "LOAD GAME");
  CHECK(state==ST_SLOTS && !slot_save_mode);
  handle_state_click(100,132);
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
  confirm_slot(false);
  CHECK(state == ST_PLAYING && game->player.health == 52 && game->level_index == 2);
  CHECK(session.difficulty == DIFFICULTY_HARD && selected_difficulty == DIFFICULTY_HARD);

  game->player_dead = true;
  game->player.health = 0;
  set_state(ST_DIED);
  death_delay = 1;
  on_key_down(SDL_SCANCODE_F9);
  confirm_slot(false);
  CHECK(state == ST_PLAYING && !game->player_dead && game->player.health == 52 && death_delay == 0);
  set_state(ST_GAME_OVER);
  on_key_down(SDL_SCANCODE_F9);
  confirm_slot(false);
  CHECK(state == ST_PLAYING && game->player.health == 52);

  // A fresh process can reach the slot through the main menu.
  game_destroy(game);
  game = NULL;
  memset(&session, 0, sizeof session);
  resumable = false;
  set_state(ST_MENU);
  click_item(main_menu(), MAIN_MENU_SPAN, "LOAD GAME");
  confirm_slot(false);
  CHECK(state == ST_PLAYING && game->player.health == 52 && session.score == 1234);

  on_key_down(SDL_SCANCODE_ESCAPE);
  CHECK(SDL_SaveFile(save_path, "broken", 6));
  Game *previous = game;
  click_item(pause_menu(), PAUSE_MENU_SPAN, "LOAD GAME");
  confirm_slot(false);
  CHECK(state==ST_SLOTS && game==previous && game->player.health==52 && notice_timer>0);
  on_key_down(SDL_SCANCODE_ESCAPE); CHECK(state==ST_PAUSED && game==previous);

  // Cancelled saves leave both disk and the current run untouched. F9 inside
  // Save changes the operation while preserving the original return screen.
  click_item(pause_menu(),PAUSE_MENU_SPAN,"SAVE GAME");
  on_key_down(SDL_SCANCODE_F9); CHECK(state==ST_SLOTS && !slot_save_mode);
  on_key_down(SDL_SCANCODE_ESCAPE); CHECK(state==ST_PAUSED);
  click_item(pause_menu(),PAUSE_MENU_SPAN,"SAVE GAME");
  slot_row=3; activate_slot(); CHECK(state==ST_SLOTS && slot_save_mode);
  slot_row=0; on_key_down(SDL_SCANCODE_ESCAPE); CHECK(state==ST_PAUSED && game==previous);

  game->player.health=44;
  char err[128]; CHECK(savegame_write(slot_path(3),game,err,sizeof err));
  on_key_down(SDL_SCANCODE_F9);
  for (int i=0;i<3;i++) on_key_down(SDL_SCANCODE_DOWN);
  CHECK(slot_row==3 && !slot_save_mode);
  confirm_slot(false); CHECK(state==ST_PLAYING && game->player.health==44);

  // Slot selection pauses the world and a cancellation resumes the caller.
  on_key_down(SDL_SCANCODE_F5);
  float time=game->time; update(.5f); CHECK(game->time==time);
  on_key_down(SDL_SCANCODE_ESCAPE); CHECK(state==ST_PLAYING && game->time==time);

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
