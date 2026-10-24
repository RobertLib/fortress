// Drive the real SDL gamepad path with virtual devices; no hardware or window.
#define SDL_MAIN_HANDLED
#define main fortress_entry
#include "../src/main.c"
#undef main

#define CHECK(expr) do { if (!(expr)) { \
  fprintf(stderr, "%s:%d: %s (%s)\n", __FILE__, __LINE__, #expr, SDL_GetError()); exit(1); \
} } while (0)

static void pump(void) {
  SDL_Event event;
  while (SDL_PollEvent(&event)) handle_event(&event);
}

static SDL_JoystickID attach_pad(void) {
  SDL_VirtualJoystickDesc desc;
  SDL_INIT_INTERFACE(&desc);
  desc.type = SDL_JOYSTICK_TYPE_GAMEPAD;
  desc.vendor_id = 0xf00d;
  desc.product_id = 0x0001;
  desc.naxes = SDL_GAMEPAD_AXIS_COUNT;
  desc.nbuttons = SDL_GAMEPAD_BUTTON_COUNT;
  desc.axis_mask = (1u << SDL_GAMEPAD_AXIS_COUNT) - 1;
  desc.button_mask = (1u << SDL_GAMEPAD_BUTTON_COUNT) - 1;
  desc.name = "Fortress test gamepad";
  SDL_JoystickID id = SDL_AttachVirtualJoystick(&desc);
  CHECK(id != 0);
  CHECK(SDL_IsGamepad(id));
  // Virtual joystick triggers use the raw -32768..32767 range, unlike the
  // gamepad API's 0..32767 range. Start both at their released endpoint.
  SDL_Joystick *joystick = SDL_OpenJoystick(id);
  CHECK(joystick);
  CHECK(SDL_SetJoystickVirtualAxis(joystick, SDL_GAMEPAD_AXIS_LEFT_TRIGGER, -32768));
  CHECK(SDL_SetJoystickVirtualAxis(joystick, SDL_GAMEPAD_AXIS_RIGHT_TRIGGER, -32768));
  SDL_UpdateJoysticks();
  SDL_CloseJoystick(joystick);
  return id;
}

static void axis(SDL_Joystick *joystick, SDL_GamepadAxis axis, Sint16 value) {
  if (axis == SDL_GAMEPAD_AXIS_LEFT_TRIGGER || axis == SDL_GAMEPAD_AXIS_RIGHT_TRIGGER)
    value = (Sint16)((int)value * 2 - 32768 + (value == 32767));
  CHECK(SDL_SetJoystickVirtualAxis(joystick, axis, value));
  pump();
}

static void button(SDL_Joystick *joystick, SDL_GamepadButton button, bool down) {
  CHECK(SDL_SetJoystickVirtualButton(joystick, button, down));
  pump();
}

static void tap(SDL_Joystick *joystick, SDL_GamepadButton value) {
  button(joystick, value, true);
  button(joystick, value, false);
}

static void test_menus(SDL_Joystick *joystick) {
  set_state(ST_MENU);
  CHECK(state == ST_MENU && main_menu_index == 0);
  tap(joystick, SDL_GAMEPAD_BUTTON_EAST);
  CHECK(state == ST_QUIT && running);
  tap(joystick, SDL_GAMEPAD_BUTTON_EAST);
  CHECK(state == ST_MENU && running);
  update_gamepad(.01f); // neutral arms navigation after a screen change
  button(joystick, SDL_GAMEPAD_BUTTON_DPAD_UP, true);
  CHECK(main_menu_index == main_menu().count - 1); // press works before the next frame
  update_gamepad(.01f);
  CHECK(main_menu_index == main_menu().count - 1);
  update_gamepad(.2f);
  CHECK(main_menu_index == main_menu().count - 1);
  update_gamepad(.21f);
  CHECK(main_menu_index == main_menu().count - 2);
  button(joystick, SDL_GAMEPAD_BUTTON_DPAD_UP, false);
  update_gamepad(.01f);
  main_menu_index = 0;
  tap(joystick, SDL_GAMEPAD_BUTTON_SOUTH);
  CHECK(state == ST_DIFFICULTY && difficulty_menu_index == 1);
  update_gamepad(.01f);
  axis(joystick, SDL_GAMEPAD_AXIS_LEFTY, -24000);
  update_gamepad(.01f);
  CHECK(difficulty_menu_index == 0);
  tap(joystick, SDL_GAMEPAD_BUTTON_SOUTH);
  CHECK(state == ST_STORY_INTRO && comic.panel == 0);
  tap(joystick, SDL_GAMEPAD_BUTTON_SOUTH);
  CHECK(state == ST_STORY_INTRO && comic.panel == 1);
  tap(joystick, SDL_GAMEPAD_BUTTON_EAST);
  CHECK(state == ST_LEVEL_START && session.difficulty == DIFFICULTY_EASY);
  axis(joystick, SDL_GAMEPAD_AXIS_LEFTY, 0);
  tap(joystick, SDL_GAMEPAD_BUTTON_SOUTH);
  CHECK(state == ST_PLAYING && !input.use);

  tap(joystick, SDL_GAMEPAD_BUTTON_START);
  CHECK(state == ST_PAUSED);
  update_gamepad(.01f);
  // Save and load are reachable entirely through the controller.
  game->player.health = 67;
  button(joystick, SDL_GAMEPAD_BUTTON_DPAD_DOWN, true);
  update_gamepad(.01f);
  button(joystick, SDL_GAMEPAD_BUTTON_DPAD_DOWN, false);
  update_gamepad(.01f);
  CHECK(pause_menu_index == 1);
  tap(joystick, SDL_GAMEPAD_BUTTON_SOUTH);
  CHECK(!strcmp(notice, "GAME SAVED") && state == ST_PAUSED);
  game->player.health = 24;
  button(joystick, SDL_GAMEPAD_BUTTON_DPAD_DOWN, true);
  update_gamepad(.01f);
  button(joystick, SDL_GAMEPAD_BUTTON_DPAD_DOWN, false);
  update_gamepad(.01f);
  CHECK(pause_menu_index == 2);
  tap(joystick, SDL_GAMEPAD_BUTTON_SOUTH);
  CHECK(state == ST_PLAYING && game->player.health == 67);
  tap(joystick, SDL_GAMEPAD_BUTTON_START);
  tap(joystick, SDL_GAMEPAD_BUTTON_EAST);
  CHECK(state == ST_PLAYING);

  tap(joystick, SDL_GAMEPAD_BUTTON_START);
  act_quit_to_menu();
  act_controls();
  tap(joystick, SDL_GAMEPAD_BUTTON_EAST);
  CHECK(state == ST_MENU);
  act_quit();
  tap(joystick, SDL_GAMEPAD_BUTTON_EAST);
  CHECK(state == ST_MENU && running);
  set_state(ST_DIED);
  tap(joystick, SDL_GAMEPAD_BUTTON_SOUTH);
  CHECK(state == ST_PLAYING); // Story rules restore the safe checkpoint directly.
  set_state(ST_LEVEL_COMPLETE);
  tap(joystick, SDL_GAMEPAD_BUTTON_SOUTH);
  CHECK(state == ST_LEVEL_START && game->level_index == 1);
  set_state(ST_GAME_OVER);
  tap(joystick, SDL_GAMEPAD_BUTTON_SOUTH);
  CHECK(state == ST_DIFFICULTY);
  tap(joystick, SDL_GAMEPAD_BUTTON_EAST);
  CHECK(state == ST_MENU);
  set_state(ST_GAME_OVER);
  tap(joystick, SDL_GAMEPAD_BUTTON_EAST);
  CHECK(state == ST_MENU && main_menu_index == 0);
  set_state(ST_VICTORY);
  tap(joystick, SDL_GAMEPAD_BUTTON_SOUTH);
  CHECK(state == ST_MENU && main_menu_index == 0);
  begin_story(COMIC_OUTRO);
  tap(joystick, SDL_GAMEPAD_BUTTON_START);
  CHECK(state == ST_STORY_OUTRO && comic.panel == 1);
  tap(joystick, SDL_GAMEPAD_BUTTON_EAST);
  CHECK(state == ST_VICTORY);
}

static void test_actions(SDL_Joystick *joystick) {
  begin_level();
  update_gamepad(.01f);
  Input frame = {.weapon = -1};
  axis(joystick, SDL_GAMEPAD_AXIS_LEFTX, 4000);
  axis(joystick, SDL_GAMEPAD_AXIS_LEFTY, -4000);
  axis(joystick, SDL_GAMEPAD_AXIS_RIGHTX, 5000);
  apply_gamepad(&frame);
  CHECK(frame.analog_forward == 0 && frame.analog_strafe == 0 && frame.analog_turn == 0);
  axis(joystick, SDL_GAMEPAD_AXIS_LEFTX, 32767);
  axis(joystick, SDL_GAMEPAD_AXIS_LEFTY, -32768);
  axis(joystick, SDL_GAMEPAD_AXIS_RIGHTX, -32768);
  apply_gamepad(&frame);
  CHECK(fabsf(hypotf(frame.analog_forward, frame.analog_strafe) - 1) < .0001f);
  CHECK(frame.analog_strafe > 0 && frame.analog_forward > 0 && frame.analog_turn == -1);
  axis(joystick, SDL_GAMEPAD_AXIS_LEFTX, 0);
  axis(joystick, SDL_GAMEPAD_AXIS_LEFTY, 0);
  axis(joystick, SDL_GAMEPAD_AXIS_RIGHTX, 0);

  axis(joystick, SDL_GAMEPAD_AXIS_RIGHT_TRIGGER, 32767);
  apply_gamepad(&frame);
  CHECK(frame.fire);
  tap(joystick, SDL_GAMEPAD_BUTTON_START);
  tap(joystick, SDL_GAMEPAD_BUTTON_START);
  frame = (Input){.weapon = -1};
  update_gamepad(.01f);
  apply_gamepad(&frame);
  CHECK(!frame.fire); // held fire cannot leak through resume
  axis(joystick, SDL_GAMEPAD_AXIS_RIGHT_TRIGGER, 0);
  update_gamepad(.01f);
  axis(joystick, SDL_GAMEPAD_AXIS_RIGHT_TRIGGER, 32767);
  apply_gamepad(&frame);
  CHECK(frame.fire);
  axis(joystick, SDL_GAMEPAD_AXIS_RIGHT_TRIGGER, 0);
  frame = (Input){.weapon = -1, .fire = true};
  apply_gamepad(&frame);
  CHECK(frame.fire); // releasing gamepad fire preserves keyboard/mouse fire
  frame = (Input){.weapon = -1};
  button(joystick, SDL_GAMEPAD_BUTTON_LEFT_STICK, true);
  apply_gamepad(&frame);
  CHECK(frame.run);
  button(joystick, SDL_GAMEPAD_BUTTON_LEFT_STICK, false);
  frame.run = false;
  axis(joystick, SDL_GAMEPAD_AXIS_LEFT_TRIGGER, 32767);
  apply_gamepad(&frame);
  CHECK(frame.run);
  axis(joystick, SDL_GAMEPAD_AXIS_LEFT_TRIGGER, 0);

  tap(joystick, SDL_GAMEPAD_BUTTON_SOUTH);
  CHECK(input.use);
  game->player.weapon = WEAPON_CROSSBOW;
  game->player.has_arbalest = false;
  tap(joystick, SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER);
  CHECK(input.weapon == WEAPON_DAGGER);
  tap(joystick, SDL_GAMEPAD_BUTTON_LEFT_SHOULDER);
  CHECK(input.weapon == WEAPON_CROSSBOW);
  game->player.has_arbalest = true;
  tap(joystick, SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER);
  CHECK(input.weapon == WEAPON_ARBALEST);
  tap(joystick, SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER);
  CHECK(input.weapon == WEAPON_DAGGER);
  game->show_map = false;
  tap(joystick, SDL_GAMEPAD_BUTTON_NORTH);
  CHECK(game->show_map);
  tap(joystick, SDL_GAMEPAD_BUTTON_BACK);
  CHECK(!game->show_map);
  game->horror.reading = 0;
  tap(joystick, SDL_GAMEPAD_BUTTON_NORTH);
  CHECK(!game->show_map);
  on_key_down(SDL_SCANCODE_TAB);
  CHECK(!game->show_map); // the automap must not cover a readable note
  tap(joystick, SDL_GAMEPAD_BUTTON_SOUTH);
  update(.01f);
  CHECK(game->horror.reading == -1);
  update(.01f);
  CHECK(!input.use && input.weapon == -1 && game->player.weapon == WEAPON_DAGGER);
  game->player.weapon = WEAPON_CROSSBOW;
  game->fire_timer = 0;
  game->fire_held = false;
  int ammo = game->player.ammo;
  axis(joystick, SDL_GAMEPAD_AXIS_RIGHT_TRIGGER, 32767);
  update(.01f);
  CHECK(game->player.ammo == ammo - 1 && !input.fire);
  update(.01f);
  CHECK(game->player.ammo == ammo - 1); // a held crossbow trigger fires once
  axis(joystick, SDL_GAMEPAD_AXIS_RIGHT_TRIGGER, 0);
  update(.01f);
  CHECK(!game->fire_held);

  SDL_Event event = {.type = SDL_EVENT_WINDOW_FOCUS_LOST};
  handle_event(&event);
  CHECK(state == ST_PAUSED && !input_focused && !input.use && input.weapon == -1);
  tap(joystick, SDL_GAMEPAD_BUTTON_START);
  CHECK(state == ST_PAUSED);
  axis(joystick, SDL_GAMEPAD_AXIS_LEFTY, -32768);
  frame = (Input){.weapon = -1};
  apply_gamepad(&frame);
  CHECK(frame.analog_forward == 0);
  axis(joystick, SDL_GAMEPAD_AXIS_LEFTY, 0);
  event.type = SDL_EVENT_WINDOW_FOCUS_GAINED;
  handle_event(&event);
  tap(joystick, SDL_GAMEPAD_BUTTON_START);
  CHECK(state == ST_PLAYING && input_focused);

  // Automatic pauses must bypass Escape's "close the journal" action.
  on_key_down(SDL_SCANCODE_I);
  CHECK(game->campaign.journal);
  float time = game->time;
  game->player.venom = 2;
  game->player.venom_timer = .01f;
  int health = game->player.health;
  event.type = SDL_EVENT_WINDOW_FOCUS_LOST;
  handle_event(&event);
  CHECK(state == ST_PAUSED && game->campaign.journal && !input_focused);
  update(.5f);
  CHECK(game->time == time && game->player.health == health && game->player.venom == 2);
  event.type = SDL_EVENT_WINDOW_FOCUS_GAINED;
  handle_event(&event);
  update(.5f);
  CHECK(state == ST_PAUSED && game->time == time);
  tap(joystick, SDL_GAMEPAD_BUTTON_START);
  CHECK(state == ST_PLAYING && game->campaign.journal);
  tap(joystick, SDL_GAMEPAD_BUTTON_EAST);
  CHECK(!game->campaign.journal);
  update(.01f);
  CHECK(game->time > time);

  set_state(ST_LEVEL_START);
  state_timer = 4;
  event.type = SDL_EVENT_WINDOW_FOCUS_LOST;
  handle_event(&event);
  update(.01f);
  CHECK(state == ST_LEVEL_START); // the timed introduction cannot start in the background
  event.type = SDL_EVENT_WINDOW_FOCUS_GAINED;
  handle_event(&event);
  update(.01f);
  CHECK(state == ST_PLAYING);
}

static void test_analog_movement(SDL_Joystick *joystick) {
  const char *arena =
      "111111111111\n"
      "1..........1\n"
      "1.P........1\n"
      "1..........1\n"
      "1..........1\n"
      "111111111111\n";
  Level level;
  char err[128];
  CHECK(level_parse(&level, arena, err, sizeof err));
  level.decoration_count = level.wall_box_count = 0;
  Session run = {.lives = 3, .next_life = 40000};
  Game *g = game_create(&level, 0, &run, NULL);
  CHECK(g);
  float x = g->player.x, y = g->player.y;
  Input frame = {.weapon = -1};
  axis(joystick, SDL_GAMEPAD_AXIS_LEFTY, -20200);
  apply_gamepad(&frame);
  CHECK(frame.analog_forward > .45f && frame.analog_forward < .55f);
  game_update(g, .1f, &frame);
  float slow = g->player.x - x;
  CHECK(fabsf(slow - 3.4f * .1f * frame.analog_forward) < .0001f);
  g->player.x = x;
  axis(joystick, SDL_GAMEPAD_AXIS_LEFTY, -32768);
  apply_gamepad(&frame);
  game_update(g, .1f, &frame);
  CHECK(fabsf(g->player.x - x - .34f) < .0001f && g->player.x - x > slow * 1.9f);
  g->player.x = x;
  g->player.y = y;
  axis(joystick, SDL_GAMEPAD_AXIS_LEFTX, 32767);
  apply_gamepad(&frame);
  frame.forward = true; // combined keyboard + stick stays within the speed cap
  game_update(g, .1f, &frame);
  CHECK(fabsf(hypotf(g->player.x - x, g->player.y - y) - .34f) < .0001f);
  frame = (Input){.weapon = -1, .analog_turn = 1};
  game_update(g, .1f, &frame);
  CHECK(fabsf(atan2f(g->player.dir_y, g->player.dir_x) - .27f) < .0001f);
  axis(joystick, SDL_GAMEPAD_AXIS_LEFTX, 0);
  axis(joystick, SDL_GAMEPAD_AXIS_LEFTY, 0);
  game_destroy(g);
  level_free(&level);
  game_activate(game);
}

int main(void) {
  SDL_SetHint(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS, "1");
  SDL_SetHint(SDL_HINT_GAMECONTROLLER_IGNORE_DEVICES_EXCEPT, "0xf00d/0x0001");
  CHECK(SDL_Init(SDL_INIT_GAMEPAD));
  SDL_JoystickID first = attach_pad();
  find_gamepad(); // already connected at startup
  CHECK(gamepad && gamepad_id == first);
  pump(); // the queued add event must not open the same device twice
  SDL_Joystick *joystick = SDL_GetGamepadJoystick(gamepad);
  CHECK(load_levels());
  save_path = malloc(sizeof "test-gamepad-save.dat");
  CHECK(save_path);
  strcpy(save_path, "test-gamepad-save.dat");
  SDL_RemovePath(save_path);

  test_menus(joystick);
  test_actions(joystick);
  test_analog_movement(joystick);

  SDL_JoystickID second = attach_pad();
  pump();
  CHECK(gamepad_id == first); // extra controllers cannot steal an active run
  SDL_Joystick *other = SDL_OpenJoystick(second);
  CHECK(other);
  tap(other, SDL_GAMEPAD_BUTTON_START);
  CHECK(state == ST_PLAYING);
  game->campaign.journal = true;
  float time = game->time;
  CHECK(SDL_DetachVirtualJoystick(first));
  pump();
  CHECK(state == ST_PAUSED && game->campaign.journal && gamepad_id == second && !input.fire);
  update(.5f);
  CHECK(game->time == time);
  CHECK(!strcmp(notice, "GAMEPAD DISCONNECTED"));
  CHECK(SDL_DetachVirtualJoystick(second));
  pump();
  SDL_CloseJoystick(other);
  CHECK(!gamepad && gamepad_id == 0 && !gamepad_active);
  first = attach_pad();
  pump();
  CHECK(gamepad && gamepad_id == first);
  tap(SDL_GetGamepadJoystick(gamepad), SDL_GAMEPAD_BUTTON_START);
  CHECK(state == ST_PLAYING && game->campaign.journal);
  tap(SDL_GetGamepadJoystick(gamepad), SDL_GAMEPAD_BUTTON_EAST);
  CHECK(!game->campaign.journal);

  // Exercise gamepad hints as well as switching back to keyboard hints.
  fb = image_new(W, H);
  begin_story(COMIC_INTRO);
  draw_story();
  set_state(ST_PLAYING);
  draw_main_menu();
  draw_difficulty();
  draw_controls();
  draw_quit();
  draw_pause_overlay();
  draw_died_overlay();
  draw_level_start();
  draw_level_complete();
  draw_game_over();
  draw_victory();
  CHECK(gamepad_active);
  on_key_down(SDL_SCANCODE_TAB);
  CHECK(!gamepad_active);
  draw_controls();
  image_free(fb);
  font_shutdown();
  CHECK(SDL_DetachVirtualJoystick(first));
  pump();
  game_destroy(game);
  for (int i = 0; i < levels_loaded; i++) level_free(&levels[i]);
  SDL_RemovePath(save_path);
  free(save_path);
  SDL_Quit();
  puts("Gamepad menus, analog movement, actions, focus and hotplug checks passed.");
  return 0;
}
