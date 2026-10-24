// FORTRESS — a medieval first-person raycasting adventure.
// Entry point: window, asset/level loading, input, game states, menus and
// the main loop.

#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "assets.h"
#include "audio.h"
#include "comic.h"
#include "font.h"
#include "game.h"
#include "gfx.h"
#include "level.h"
#include "render.h"
#include "savegame.h"
#include "ui.h"

#define W SCREEN_W
#define H SCREEN_H

typedef enum {
  ST_LOADING,
  ST_INTRO,
  ST_MENU,
  ST_DIFFICULTY,
  ST_STORY_INTRO,
  ST_STORY_OUTRO,
  ST_CONTROLS,
  ST_QUIT,
  ST_LEVEL_START,
  ST_PLAYING,
  ST_PAUSED,
  ST_DIED,
  ST_LEVEL_COMPLETE,
  ST_GAME_OVER,
  ST_VICTORY,
  ST_ERROR
} State;

static SDL_Window *window;
static SDL_Renderer *sdl_renderer;
static SDL_Texture *screen_tex;
static SDL_Cursor *cursor_default, *cursor_pointer;
static Image *fb;

static Assets assets;
static Renderer renderer;
static ComicArt comic_art;
static Comic comic;
static Level levels[LEVEL_COUNT];
static int levels_loaded;
static char error_msg[256];

static State state = ST_LOADING;
static Game *game;
static Session session;
static float state_timer, death_delay, clock_time;
static bool running = true;
static bool resumable; // a run was left via the menu and can be picked back up
static int main_menu_index, pause_menu_index;
static int difficulty_menu_index = 1, pending_start_index;
static Difficulty selected_difficulty = DIFFICULTY_NORMAL;
static const Difficulty DIFFICULTY_CHOICES[] = {DIFFICULTY_EASY, DIFFICULTY_NORMAL, DIFFICULTY_HARD};
static Input input = {.weapon = -1};
static char *save_path;
static char notice[96];
static float notice_timer;
static SDL_Gamepad *gamepad;
static SDL_JoystickID gamepad_id;
static bool input_focused = true, gamepad_active;
static bool gamepad_fire_ready, gamepad_menu_blocked;
static int gamepad_menu_direction;
static float gamepad_menu_timer;

// ------------------------------------------------------------------ helpers

static bool fullscreen(void) { return (SDL_GetWindowFlags(window) & SDL_WINDOW_FULLSCREEN) != 0; }

static void toggle_fullscreen(void) { SDL_SetWindowFullscreen(window, !fullscreen()); }

static bool mouse_captured(void) { return SDL_GetWindowRelativeMouseMode(window); }

static void capture_mouse(bool on) { SDL_SetWindowRelativeMouseMode(window, on); }

static void blip(void) { audio_play(SND_BLIP, 1, 1); }

static void clear_input(void) {
  input = (Input){.weapon = -1};
  gamepad_fire_ready = false;
  gamepad_menu_blocked = true;
  gamepad_menu_direction = 0;
  gamepad_menu_timer = 0;
}

// ------------------------------------------------------------------ flow

static void set_state(State s) {
  state = s;
  state_timer = 0;
  clear_input();
  notice_timer = 0;
  // once the run has ended there is nothing left to continue
  if (s == ST_GAME_OVER || s == ST_STORY_OUTRO || s == ST_VICTORY) resumable = false;
}

static bool story_state(void) { return state == ST_STORY_INTRO || state == ST_STORY_OUTRO; }

static void story_sound(void) {
  static const SoundId sounds[COMIC_KIND_COUNT][COMIC_PANELS] = {
      {SND_DOOR, SND_LEVER, SND_PICKUP_WEAPON}, {SND_EXIT_OPEN, SND_STEP, SND_VICTORY}};
  audio_play(sounds[comic.kind][comic.panel], .65f, 1);
}

static void begin_story(ComicKind kind) {
  comic_start(&comic, kind);
  set_state(kind == COMIC_INTRO ? ST_STORY_INTRO : ST_STORY_OUTRO);
  capture_mouse(false);
  story_sound();
}

static void finish_story(void) {
  set_state(comic.kind == COMIC_INTRO ? ST_LEVEL_START : ST_VICTORY);
}

static void advance_story(void) {
  if (comic_next(&comic)) finish_story();
  else story_sound();
}

static void start_level(int index, const Persist *persist) {
  game_destroy(game);
  game = game_create(&levels[index], index, &session, persist);
  input.use = false;
  input.weapon = -1;
  input.mouse_dx = 0;
  set_state(ST_LEVEL_START);
}

static void new_game(int start_index, Difficulty difficulty) {
  resumable = false;
  selected_difficulty = difficulty;
  memset(&session, 0, sizeof session);
  session.difficulty = difficulty;
  session.lives = 3;
  session.next_life = 40000;
  start_level(start_index, NULL);
}

static void choose_difficulty(int start_index) {
  pending_start_index = start_index;
  for (int i = 0; i < DIFFICULTY_COUNT; i++)
    if (DIFFICULTY_CHOICES[i] == selected_difficulty) difficulty_menu_index = i;
  set_state(ST_DIFFICULTY);
}

static void begin_level(void) {
  set_state(ST_PLAYING);
  blip();
}

static void restart_level(void) {
  start_level(game->level_index, NULL); // classic rules: fresh crossbow after death
}

static void next_level(void) {
  const Player *p = &game->player;
  session.total_enemies += game->total_enemies;
  session.max_treasure += game->total_treasure;
  session.max_secrets += game->total_secrets;
  session.total_time += game->time;
  if (game->level_index + 1 >= LEVEL_COUNT) {
    begin_story(COMIC_OUTRO);
  } else {
    Persist persist = {p->health, p->ammo, p->weapon, p->has_arbalest};
    start_level(game->level_index + 1, &persist);
  }
}

static int level_bonus(void) {
  double kill_pct = game->total_enemies ? (double)game->kills / game->total_enemies : 1;
  double treasure_pct = game->total_treasure ? (double)game->treasure / game->total_treasure : 1;
  double time_bonus = maxf(0, floorf(120 - game->time)) * 10;
  return (int)round_half_up(kill_pct * 1000 + treasure_pct * 1000 + time_bonus);
}

// ------------------------------------------------------------------ save / load

static const char *get_save_path(void) {
  if (save_path) return save_path;
  char *directory = SDL_GetPrefPath("Fortress", "Fortress");
  if (!directory) return NULL;
  size_t size = strlen(directory) + sizeof "savegame.dat";
  save_path = malloc(size);
  if (save_path) snprintf(save_path, size, "%ssavegame.dat", directory);
  SDL_free(directory);
  return save_path;
}

static void notify_save(const char *message) {
  snprintf(notice, sizeof notice, "%s", message);
  notice_timer = 4;
  if (game && state == ST_PLAYING) game_say(game, message, 3);
}

static void act_save_game(void) {
  const char *path = get_save_path();
  char err[96];
  if (!path)
    notify_save("SAVE FOLDER UNAVAILABLE");
  else if (savegame_write(path, game, err, sizeof err)) {
    blip();
    notify_save("GAME SAVED");
  } else
    notify_save(err);
}

static void act_load_game(void) {
  const char *path = get_save_path();
  char err[96];
  if (!path) {
    notify_save("SAVE FOLDER UNAVAILABLE");
  } else if (savegame_load(path, levels, levels_loaded, &game, &session, err, sizeof err)) {
    selected_difficulty = session.difficulty;
    resumable = false;
    death_delay = 0;
    set_state(ST_PLAYING);
    notify_save("GAME LOADED");
    blip();
  } else {
    notify_save(err);
  }
}

// ------------------------------------------------------------------ menus

typedef struct MenuItem {
  const char *(*label)(void);
  void (*action)(void);
} MenuItem;

static const char *label_continue(void) { return "CONTINUE"; }
static const char *label_new_game(void) { return "NEW GAME"; }
static const char *label_save_game(void) { return "SAVE GAME"; }
static const char *label_load_game(void) { return "LOAD GAME"; }
static const char *label_controls(void) { return "CONTROLS"; }
static const char *label_sound(void) { return audio_enabled() ? "SOUND: ON" : "SOUND: OFF"; }
static const char *label_fullscreen(void) { return fullscreen() ? "FULLSCREEN: ON" : "FULLSCREEN: OFF"; }
static const char *label_quit(void) { return "QUIT GAME"; }
static const char *label_resume(void) { return "RESUME"; }
static const char *label_quit_to_menu(void) { return "QUIT TO MENU"; }
static const char *label_easy(void) { return "EASY"; }
static const char *label_normal(void) { return "NORMAL"; }
static const char *label_hard(void) { return "HARD"; }
static const char *label_back(void) { return "BACK"; }

static void act_continue(void) {
  blip();
  set_state(ST_PLAYING);
}

static void act_new_game(void) {
  blip();
  choose_difficulty(0);
}

static void start_difficulty(Difficulty difficulty) {
  audio_play(SND_START, 1, 1);
  new_game(pending_start_index, difficulty);
  if (pending_start_index == 0) begin_story(COMIC_INTRO);
}

static void act_easy(void) { start_difficulty(DIFFICULTY_EASY); }
static void act_normal(void) { start_difficulty(DIFFICULTY_NORMAL); }
static void act_hard(void) { start_difficulty(DIFFICULTY_HARD); }

static void act_back(void) {
  blip();
  set_state(ST_MENU);
}

static void act_controls(void) {
  blip();
  set_state(ST_CONTROLS);
}

static void act_sound(void) {
  audio_set_enabled(!audio_enabled());
  blip();
}

static void act_fullscreen(void) {
  blip();
  toggle_fullscreen();
}

static void act_quit(void) {
  blip();
  set_state(ST_QUIT);
}

static void act_resume(void) {
  blip();
  set_state(ST_PLAYING);
}

static void act_quit_to_menu(void) {
  blip();
  resumable = true;
  main_menu_index = 0;
  set_state(ST_MENU);
}

static const MenuItem MAIN_MENU_FULL[] = {
    {label_continue, act_continue}, {label_new_game, act_new_game}, {label_load_game, act_load_game},
    {label_controls, act_controls}, {label_sound, act_sound},       {label_fullscreen, act_fullscreen},
    {label_quit, act_quit},
};

static const MenuItem PAUSE_MENU[] = {
    {label_resume, act_resume},
    {label_save_game, act_save_game},
    {label_load_game, act_load_game},
    {label_sound, act_sound},
    {label_fullscreen, act_fullscreen},
    {label_quit_to_menu, act_quit_to_menu},
};
#define PAUSE_MENU_COUNT ((int)(sizeof PAUSE_MENU / sizeof PAUSE_MENU[0]))

static const MenuItem DIFFICULTY_MENU[] = {
    {label_easy, act_easy},
    {label_normal, act_normal},
    {label_hard, act_hard},
    {label_back, act_back},
};
#define DIFFICULTY_MENU_COUNT 4

typedef struct {
  const MenuItem *items;
  int count;
} Menu;

// CONTINUE only shows while there is a run to go back to
static Menu main_menu(void) {
  int count = (int)(sizeof MAIN_MENU_FULL / sizeof MAIN_MENU_FULL[0]);
  Menu m = {resumable ? MAIN_MENU_FULL : MAIN_MENU_FULL + 1, resumable ? count : count - 1};
  return m;
}

static Menu pause_menu(void) {
  Menu m = {PAUSE_MENU, PAUSE_MENU_COUNT};
  return m;
}

static Menu difficulty_menu(void) {
  Menu m = {DIFFICULTY_MENU, DIFFICULTY_MENU_COUNT};
  return m;
}

// Shared drawing/hit-test bounds: vertical fit, horizontal origin and width.
typedef struct {
  float top, bottom, max_line_h, max_size;
  float x, width;
} MenuSpan;

static const MenuSpan MAIN_MENU_SPAN = {129, 341, 32, 18, 40, 252};
static const MenuSpan PAUSE_MENU_SPAN = {110, 278, 27, 16, 190, 260};
static const MenuSpan DIFFICULTY_MENU_SPAN = {134, 294, 36, 21, 40, 252};

typedef struct {
  float cy, line_h, size;
  float cx;
} MenuLayout;

// Fit a menu into its vertical span: full size while it fits,
// proportionally smaller line height and font once items no longer do.
static MenuLayout menu_layout(Menu m, MenuSpan span) {
  MenuLayout l;
  l.cx = span.x + span.width / 2;
  l.line_h = minf(span.max_line_h, floorf((span.bottom - span.top) / (m.count + 1)));
  l.size = (float)round_half_up((span.max_size * l.line_h) / span.max_line_h);
  // centre the block in the span; baselines sit under the glyphs, so
  // offset by the cap height
  float cap_h = l.size * 0.72f;
  l.cy = (float)round_half_up((span.top + span.bottom) / 2 + cap_h / 2 - ((m.count - 1) * l.line_h) / 2);
  return l;
}

// index of the menu item under (x, y), or -1
static int menu_hit_test(Menu m, MenuSpan span, float x, float y) {
  MenuLayout l = menu_layout(m, span);
  for (int i = 0; i < m.count; i++) {
    float by = l.cy + i * l.line_h;
    float top = by - l.size * .9f - 3;
    if (y < top || y >= top + l.line_h - 3) continue;
    if (x >= span.x && x <= span.x + span.width) return i;
  }
  return -1;
}

// screens where a click anywhere acts like ENTER
static bool click_state(State s) {
  switch (s) {
  case ST_INTRO:
  case ST_STORY_INTRO:
  case ST_STORY_OUTRO:
  case ST_CONTROLS:
  case ST_QUIT:
  case ST_LEVEL_START:
  case ST_DIED:
  case ST_LEVEL_COMPLETE:
  case ST_GAME_OVER:
  case ST_VICTORY: return true;
  default: return false;
  }
}

static int menu_nav(SDL_Scancode sc, int count, int index) {
  bool up = sc == SDL_SCANCODE_UP || sc == SDL_SCANCODE_W;
  bool down = sc == SDL_SCANCODE_DOWN || sc == SDL_SCANCODE_S;
  if (!up && !down) return index;
  blip();
  return (index + (up ? -1 : 1) + count) % count;
}

static void handle_state_key(SDL_Scancode sc) {
  bool enter = sc == SDL_SCANCODE_RETURN || sc == SDL_SCANCODE_KP_ENTER;
  bool esc = sc == SDL_SCANCODE_ESCAPE;

  if (story_state()) {
    if (!input_focused) return;
    if (esc) finish_story();
    else if (enter || sc == SDL_SCANCODE_SPACE || sc == SDL_SCANCODE_RIGHT) advance_story();
    return;
  }

  switch (state) {
  case ST_INTRO:
    if (enter) {
      blip();
      main_menu_index = 0;
      set_state(ST_MENU);
    }
#if FORTRESS_DEBUG_TOOLS
    else if (sc >= SDL_SCANCODE_1 && sc <= SDL_SCANCODE_9) {
      // debug: a number key on the title screen warps to that level
      int n = sc - SDL_SCANCODE_1 + 1;
      if (n <= LEVEL_COUNT) {
        audio_play(SND_START, 1, 1);
        new_game(n - 1, selected_difficulty);
      }
    }
#endif
    break;
  case ST_MENU: {
    Menu m = main_menu();
    main_menu_index = menu_nav(sc, m.count, main_menu_index);
    if (enter) {
      m.items[main_menu_index].action();
    } else if (esc) {
      blip();
      set_state(ST_INTRO);
    }
    break;
  }
  case ST_DIFFICULTY:
    difficulty_menu_index = menu_nav(sc, DIFFICULTY_MENU_COUNT, difficulty_menu_index);
    if (enter)
      DIFFICULTY_MENU[difficulty_menu_index].action();
    else if (esc)
      act_back();
    break;
  case ST_CONTROLS:
    if (enter || esc) {
      blip();
      set_state(ST_MENU);
    }
    break;
  case ST_QUIT:
    if (enter) {
      running = false;
    } else if (esc) {
      blip();
      set_state(ST_MENU);
    }
    break;
  case ST_LEVEL_START:
    if (enter) begin_level();
    break;
  case ST_PLAYING:
    if (esc) {
      set_state(ST_PAUSED);
      pause_menu_index = 0;
      capture_mouse(false);
      blip();
    }
    break;
  case ST_PAUSED:
    pause_menu_index = menu_nav(sc, PAUSE_MENU_COUNT, pause_menu_index);
    if (esc) {
      blip();
      set_state(ST_PLAYING);
    } else if (enter) {
      PAUSE_MENU[pause_menu_index].action();
    } else if (sc == SDL_SCANCODE_Q) {
      resumable = true;
      main_menu_index = 0;
      set_state(ST_MENU);
    }
    break;
  case ST_DIED:
    if (enter) restart_level();
    break;
  case ST_LEVEL_COMPLETE:
    if (enter) next_level();
    break;
  case ST_GAME_OVER:
    if (enter) {
      blip();
      choose_difficulty(0);
    } else if (esc) {
      set_state(ST_INTRO);
    }
    break;
  case ST_VICTORY:
    if (enter || esc) set_state(ST_INTRO);
    break;
  default: break;
  }
}

static void handle_state_click(float x, float y) {
  if (story_state()) {
    if (!input_focused) return;
    if (x >= 520 && x <= 627 && y >= 368 && y <= 394) finish_story();
    else advance_story();
  } else if (state == ST_MENU) {
    Menu m = main_menu();
    int i = menu_hit_test(m, MAIN_MENU_SPAN, x, y);
    if (i >= 0) {
      main_menu_index = i;
      m.items[i].action();
    }
  } else if (state == ST_DIFFICULTY) {
    int i = menu_hit_test(difficulty_menu(), DIFFICULTY_MENU_SPAN, x, y);
    if (i >= 0) {
      difficulty_menu_index = i;
      DIFFICULTY_MENU[i].action();
    }
  } else if (state == ST_PAUSED) {
    int i = menu_hit_test(pause_menu(), PAUSE_MENU_SPAN, x, y);
    if (i >= 0) {
      pause_menu_index = i;
      PAUSE_MENU[i].action();
      // resuming by click should grab the mouse again right away
      if (state == ST_PLAYING) capture_mouse(true);
    }
  } else if (click_state(state)) {
    handle_state_key(SDL_SCANCODE_RETURN);
  }
}

static void handle_state_hover(float x, float y) {
  int hit = -1;
  if (state == ST_MENU) {
    hit = menu_hit_test(main_menu(), MAIN_MENU_SPAN, x, y);
    if (hit >= 0 && hit != main_menu_index) {
      main_menu_index = hit;
      blip();
    }
  } else if (state == ST_DIFFICULTY) {
    hit = menu_hit_test(difficulty_menu(), DIFFICULTY_MENU_SPAN, x, y);
    if (hit >= 0 && hit != difficulty_menu_index) {
      difficulty_menu_index = hit;
      blip();
    }
  } else if (state == ST_PAUSED) {
    hit = menu_hit_test(pause_menu(), PAUSE_MENU_SPAN, x, y);
    if (hit >= 0 && hit != pause_menu_index) {
      pause_menu_index = hit;
      blip();
    }
  }
  SDL_SetCursor(hit >= 0 || click_state(state) ? cursor_pointer : cursor_default);
}

// ------------------------------------------------------------------ input

static void find_gamepad(void) {
  if (gamepad) return;
  int count = 0;
  SDL_JoystickID *ids = SDL_GetGamepads(&count);
  for (int i = 0; i < count; i++) {
    gamepad = SDL_OpenGamepad(ids[i]);
    if (gamepad) {
      gamepad_id = ids[i];
      gamepad_active = true;
      gamepad_fire_ready = false;
      gamepad_menu_blocked = true;
      break;
    }
  }
  SDL_free(ids);
}

static void remove_gamepad(SDL_JoystickID id) {
  if (!gamepad || id != gamepad_id) return;
  SDL_CloseGamepad(gamepad);
  gamepad = NULL;
  gamepad_id = 0;
  gamepad_active = false;
  clear_input();
  if (state == ST_PLAYING) {
    handle_state_key(SDL_SCANCODE_ESCAPE);
    snprintf(notice, sizeof notice, "GAMEPAD DISCONNECTED");
    notice_timer = 4;
  }
  find_gamepad();
}

static float gamepad_axis(SDL_GamepadAxis axis) {
  Sint16 value = SDL_GetGamepadAxis(gamepad, axis);
  return value / (value < 0 ? 32768.0f : 32767.0f);
}

// A radial dead zone preserves the stick's direction and scales its remaining
// travel smoothly from walking speed to full speed, including the diagonals.
static void gamepad_move(float *x, float *y) {
  *x = gamepad_axis(SDL_GAMEPAD_AXIS_LEFTX);
  *y = gamepad_axis(SDL_GAMEPAD_AXIS_LEFTY);
  float length = hypotf(*x, *y);
  float amount = clampf((length - 0.24f) / 0.76f, 0, 1);
  float scale = length > 0 ? amount / length : 0;
  *x *= scale;
  *y *= scale;
}

static bool gamepad_firing(void) {
  return SDL_GetGamepadAxis(gamepad, SDL_GAMEPAD_AXIS_RIGHT_TRIGGER) > 8000;
}

static void cycle_weapon(int direction) {
  int weapon = input.weapon >= 0 ? input.weapon : (int)game->player.weapon;
  do {
    weapon = (weapon + direction + WEAPON_COUNT) % WEAPON_COUNT;
  } while (weapon == WEAPON_ARBALEST && !game->player.has_arbalest);
  input.weapon = weapon;
}

static void on_gamepad_button(SDL_GamepadButton button) {
  gamepad_active = true;
  if (button == SDL_GAMEPAD_BUTTON_START) {
    handle_state_key(state == ST_PLAYING || state == ST_PAUSED ? SDL_SCANCODE_ESCAPE : SDL_SCANCODE_RETURN);
  } else if (state != ST_PLAYING) {
    if (button == SDL_GAMEPAD_BUTTON_SOUTH) handle_state_key(SDL_SCANCODE_RETURN);
    if (button == SDL_GAMEPAD_BUTTON_EAST) handle_state_key(SDL_SCANCODE_ESCAPE);
    if ((state == ST_MENU || state == ST_DIFFICULTY || state == ST_PAUSED) &&
        (button == SDL_GAMEPAD_BUTTON_DPAD_UP || button == SDL_GAMEPAD_BUTTON_DPAD_DOWN)) {
      // Handle the press immediately so a following confirm in the same frame
      // selects the new item. Polling below supplies only the held repeats.
      gamepad_menu_direction = button == SDL_GAMEPAD_BUTTON_DPAD_UP ? -1 : 1;
      gamepad_menu_timer = 0.4f;
      gamepad_menu_blocked = false;
      handle_state_key(gamepad_menu_direction < 0 ? SDL_SCANCODE_UP : SDL_SCANCODE_DOWN);
    }
  } else {
    switch (button) {
    case SDL_GAMEPAD_BUTTON_SOUTH: input.use = true; break;
    case SDL_GAMEPAD_BUTTON_NORTH:
    case SDL_GAMEPAD_BUTTON_BACK:
    case SDL_GAMEPAD_BUTTON_TOUCHPAD:
      if (game->horror.reading < 0) game->show_map = !game->show_map;
      break;
    case SDL_GAMEPAD_BUTTON_LEFT_SHOULDER: cycle_weapon(-1); break;
    case SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER: cycle_weapon(1); break;
    default: break;
    }
  }
}

static void update_gamepad(float dt) {
  if (!gamepad || !input_focused) return;
  // A held trigger must be released after a pause, load or state change.
  if (!gamepad_firing()) gamepad_fire_ready = true;
  if (state != ST_MENU && state != ST_DIFFICULTY && state != ST_PAUSED) return;
  float y = gamepad_axis(SDL_GAMEPAD_AXIS_LEFTY);
  bool up = y < -0.55f || SDL_GetGamepadButton(gamepad, SDL_GAMEPAD_BUTTON_DPAD_UP);
  bool down = y > 0.55f || SDL_GetGamepadButton(gamepad, SDL_GAMEPAD_BUTTON_DPAD_DOWN);
  int direction = (int)down - (int)up;
  if (!direction) {
    gamepad_menu_blocked = false;
    gamepad_menu_direction = 0;
    gamepad_menu_timer = 0;
    return;
  }
  if (gamepad_menu_blocked) return;
  gamepad_menu_timer -= dt;
  if (direction != gamepad_menu_direction || gamepad_menu_timer <= 0) {
    gamepad_active = true;
    handle_state_key(direction < 0 ? SDL_SCANCODE_UP : SDL_SCANCODE_DOWN);
    gamepad_menu_timer = direction != gamepad_menu_direction ? 0.4f : 0.12f;
    gamepad_menu_direction = direction;
  }
}

static void apply_gamepad(Input *frame) {
  if (!gamepad || !input_focused) return;
  float x, y;
  gamepad_move(&x, &y);
  frame->analog_strafe = x;
  frame->analog_forward = -y;
  float turn = gamepad_axis(SDL_GAMEPAD_AXIS_RIGHTX);
  frame->analog_turn = signf(turn) * maxf(0, (fabsf(turn) - 0.24f) / 0.76f);
  frame->run |= SDL_GetGamepadButton(gamepad, SDL_GAMEPAD_BUTTON_LEFT_STICK) ||
                SDL_GetGamepadAxis(gamepad, SDL_GAMEPAD_AXIS_LEFT_TRIGGER) > 8000;
  frame->fire |= gamepad_fire_ready && gamepad_firing();
}

static bool *key_binding(SDL_Scancode sc) {
  switch (sc) {
  case SDL_SCANCODE_W:
  case SDL_SCANCODE_UP: return &input.forward;
  case SDL_SCANCODE_S:
  case SDL_SCANCODE_DOWN: return &input.back;
  case SDL_SCANCODE_A: return &input.strafe_left;
  case SDL_SCANCODE_D: return &input.strafe_right;
  case SDL_SCANCODE_LEFT: return &input.turn_left;
  case SDL_SCANCODE_RIGHT: return &input.turn_right;
  case SDL_SCANCODE_LSHIFT:
  case SDL_SCANCODE_RSHIFT: return &input.run;
  case SDL_SCANCODE_LALT:
  case SDL_SCANCODE_RALT: return &input.strafe_mod;
  case SDL_SCANCODE_LCTRL:
  case SDL_SCANCODE_RCTRL:
  case SDL_SCANCODE_J: return &input.fire;
  default: return NULL;
  }
}

static void on_key_down(SDL_Scancode sc) {
  gamepad_active = false;
  if (sc == SDL_SCANCODE_F5 && (state == ST_PLAYING || state == ST_PAUSED)) {
    act_save_game();
    return;
  }
  if (sc == SDL_SCANCODE_F9 && state != ST_LOADING && state != ST_ERROR && state != ST_QUIT) {
    act_load_game();
    return;
  }
  bool *b = key_binding(sc);
  if (b) *b = true;
  if (sc == SDL_SCANCODE_E || sc == SDL_SCANCODE_SPACE) input.use = true;
  if (sc == SDL_SCANCODE_1) input.weapon = WEAPON_DAGGER;
  if (sc == SDL_SCANCODE_2) input.weapon = WEAPON_CROSSBOW;
  if (sc == SDL_SCANCODE_3) input.weapon = WEAPON_ARBALEST;
  if (sc == SDL_SCANCODE_M) {
    audio_set_enabled(!audio_enabled());
    if (game) game_say(game, audio_enabled() ? "SOUND ON" : "SOUND OFF", 1.2f);
  }
  if (sc == SDL_SCANCODE_F) toggle_fullscreen();
  if (sc == SDL_SCANCODE_TAB && game && state == ST_PLAYING && game->horror.reading < 0) game->show_map = !game->show_map;
  handle_state_key(sc);
}

// logical 640x400 coordinates of a mouse event
static void event_pos(const SDL_Event *e, float *x, float *y) {
  SDL_Event copy = *e;
  SDL_ConvertEventToRenderCoordinates(sdl_renderer, &copy);
  if (copy.type == SDL_EVENT_MOUSE_MOTION) {
    *x = copy.motion.x;
    *y = copy.motion.y;
  } else {
    *x = copy.button.x;
    *y = copy.button.y;
  }
}

static void handle_event(const SDL_Event *e) {
  switch (e->type) {
  case SDL_EVENT_QUIT: running = false; break;
  case SDL_EVENT_GAMEPAD_ADDED: find_gamepad(); break;
  case SDL_EVENT_GAMEPAD_REMOVED: remove_gamepad(e->gdevice.which); break;
  case SDL_EVENT_GAMEPAD_BUTTON_DOWN:
    if (e->gbutton.which == gamepad_id && gamepad && input_focused)
      on_gamepad_button((SDL_GamepadButton)e->gbutton.button);
    break;
  case SDL_EVENT_GAMEPAD_AXIS_MOTION:
    if (e->gaxis.which == gamepad_id && gamepad && input_focused && abs(e->gaxis.value) > 8000)
      gamepad_active = true;
    break;
  case SDL_EVENT_WINDOW_FOCUS_GAINED: input_focused = true; break;
  case SDL_EVENT_WINDOW_FOCUS_LOST:
    input_focused = false;
    clear_input();
    if (state == ST_PLAYING) handle_state_key(SDL_SCANCODE_ESCAPE);
    capture_mouse(false);
    break;
  case SDL_EVENT_KEY_DOWN:
    if (!e->key.repeat) on_key_down(e->key.scancode);
    break;
  case SDL_EVENT_KEY_UP: {
    bool *b = key_binding(e->key.scancode);
    if (b) *b = false;
    break;
  }
  case SDL_EVENT_MOUSE_BUTTON_DOWN:
    if (e->button.button != SDL_BUTTON_LEFT) break;
    gamepad_active = false;
    if (state == ST_PLAYING) {
      if (!mouse_captured()) capture_mouse(true);
      input.fire = true;
    } else {
      float x, y;
      event_pos(e, &x, &y);
      handle_state_click(x, y);
    }
    break;
  case SDL_EVENT_MOUSE_BUTTON_UP:
    if (e->button.button == SDL_BUTTON_LEFT) input.fire = false;
    break;
  case SDL_EVENT_MOUSE_MOTION:
    if (e->motion.xrel || e->motion.yrel) gamepad_active = false;
    if (mouse_captured()) {
      if (state == ST_PLAYING) input.mouse_dx += e->motion.xrel;
    } else {
      float x, y;
      event_pos(e, &x, &y);
      handle_state_hover(x, y);
    }
    break;
  default: break;
  }
}

// ------------------------------------------------------------------ update

static void update(float dt) {
  notice_timer = maxf(0, notice_timer - dt);
  if (story_state() && input_focused) {
    int previous = comic.panel;
    if (comic_update(&comic, dt)) finish_story();
    else if (comic.panel != previous) story_sound();
    return;
  }
  if (state == ST_LEVEL_START && state_timer > 3 && input_focused) begin_level();
  update_gamepad(dt);
  if (state != ST_PLAYING) return;
  Input frame = input;
  apply_gamepad(&frame);
  game_update(game, dt, &frame);
  input.use = frame.use;
  input.weapon = frame.weapon;
  input.mouse_dx = frame.mouse_dx;

  if (game->player_dead) {
    death_delay += dt;
    if (death_delay > 1.4f) {
      death_delay = 0;
      session.lives--;
      capture_mouse(false);
      if (session.lives <= 0) {
        audio_play(SND_GAME_OVER, 1, 1);
        set_state(ST_GAME_OVER);
      } else {
        set_state(ST_DIED);
      }
    }
  } else {
    death_delay = 0;
  }

  if (game->completed && state == ST_PLAYING) {
    game->bonus = level_bonus();
    game_add_score(game, game->bonus);
    capture_mouse(false);
    set_state(ST_LEVEL_COMPLETE);
  }
}

// ------------------------------------------------------------------ drawing

static void fill(const char *color) { gfx_fill_rect(fb, 0, 0, W, H, color_parse(color)); }

static void text_align(const char *str, float x, float y, float size, const char *color, TextAlign align) {
  font_draw(fb, str, x, y, size, FONT_BOLD, align, color_parse(color));
}

static void text(const char *str, float x, float y, float size, const char *color) {
  text_align(str, x, y, size, color, ALIGN_CENTER);
}

static const char *gamepad_button_name(SDL_GamepadButton button) {
  switch (SDL_GetGamepadButtonLabel(gamepad, button)) {
  case SDL_GAMEPAD_BUTTON_LABEL_A: return "A";
  case SDL_GAMEPAD_BUTTON_LABEL_B: return "B";
  case SDL_GAMEPAD_BUTTON_LABEL_X: return "X";
  case SDL_GAMEPAD_BUTTON_LABEL_Y: return "Y";
  case SDL_GAMEPAD_BUTTON_LABEL_CROSS: return "CROSS";
  case SDL_GAMEPAD_BUTTON_LABEL_CIRCLE: return "CIRCLE";
  case SDL_GAMEPAD_BUTTON_LABEL_SQUARE: return "SQUARE";
  case SDL_GAMEPAD_BUTTON_LABEL_TRIANGLE: return "TRIANGLE";
  default:
    return button == SDL_GAMEPAD_BUTTON_SOUTH ? "SOUTH" :
           button == SDL_GAMEPAD_BUTTON_EAST ? "EAST" : "NORTH";
  }
}

static void control_hint(const char *keyboard, const char *pad, float y, float size, const char *color) {
  char buf[160];
  if (gamepad && gamepad_active) {
    snprintf(buf, sizeof buf, pad, gamepad_button_name(SDL_GAMEPAD_BUTTON_SOUTH),
             gamepad_button_name(SDL_GAMEPAD_BUTTON_EAST));
    text(buf, W / 2.0f, y, size, color);
  } else {
    text(keyboard, W / 2.0f, y, size, color);
  }
}

static void title_text(const char *str, float x, float y, float size, const char *color) {
  font_draw(fb, str, x + size * 0.06f, y + size * 0.06f, size, FONT_BOLD, ALIGN_CENTER, rgb(0, 0, 0));
  font_draw(fb, str, x, y, size, FONT_BOLD, ALIGN_CENTER, color_parse(color));
}

static float draw_wordmark(float x, float y, float size, TextAlign align, const char *color) {
  float emblem_size = size * .96f;
  float emblem_width = emblem_size * .75f;
  float gap = size * .32f;
  float width = emblem_width + gap + font_measure("FORTRESS", size, FONT_BOLD);
  if (align == ALIGN_CENTER) x -= width / 2;
  else if (align == ALIGN_RIGHT) x -= width;
  // Align the shield's visual centre with the capitals, above the text baseline.
  ui_emblem(fb, x + emblem_width / 2, y - size * .36f - emblem_size / 32, emblem_size);
  font_draw(fb, "FORTRESS", x + emblem_width + gap, y, size, FONT_BOLD, ALIGN_LEFT, color_parse(color));
  return width;
}

static void format_time(char *buf, size_t size, float t) {
  int m = (int)floorf(t / 60);
  int s = (int)floorf(fmodf(t, 60));
  snprintf(buf, size, "%d:%02d", m, s);
}

static void draw_menu(Menu m, int index, MenuSpan span) {
  MenuLayout l = menu_layout(m, span);
  for (int i = 0; i < m.count; i++) {
    float y = l.cy + i * l.line_h;
    float top = y - l.size * .9f - 3;
    bool selected = i == index;
    if (selected) {
      for (int x = 0; x < (int)span.width; x++)
        gfx_fill_rect(fb, span.x + x, top, 1, l.line_h - 3, rgba(143, 116, 72, .24f * (1 - x / span.width)));
      gfx_fill_rect(fb, span.x, top, 2, l.line_h - 3, rgb(205, 174, 119));
      gfx_fill_rect(fb, span.x + 2, top, span.width - 2, .5f, rgba(190, 158, 102, .3f));
      ui_diamond(fb, span.x + span.width - 14, y - l.size * .34f, 2.5f, rgb(205, 174, 119));
    }
    text_align(m.items[i].label(), span.x + 14, y, l.size, selected ? "#f1e4c8" : "#a9afb0", ALIGN_LEFT);
  }
}

static void menu_background(void) {
  comic_draw_cover(&comic_art, fb, clock_time);
}

static void menu_footer(const char *action) {
  gfx_fill_rect(fb, 40, 357, 560, 1, rgba(180, 159, 119, .23f));
  if (gamepad && gamepad_active) {
    char hint[128];
    snprintf(hint, sizeof hint, "D-PAD  SELECT     %s  %s     %s  BACK", gamepad_button_name(SDL_GAMEPAD_BUTTON_SOUTH),
             action, gamepad_button_name(SDL_GAMEPAD_BUTTON_EAST));
    text_align(hint, 42, 378, 9, "#b4b6b1", ALIGN_LEFT);
  } else {
    char hint[128];
    snprintf(hint, sizeof hint, "ARROWS / MOUSE  SELECT     ENTER / CLICK  %s     ESC  BACK", action);
    text_align(hint, 42, 378, 9, "#b4b6b1", ALIGN_LEFT);
  }
}

static void draw_center_screen(const char *bg, const char *line) {
  fill(bg);
  for (int y = 0; y < H; y++)
    gfx_fill_rect(fb, 0, y, W, 1, rgba(8, 19, 27, .4f * (1 - y / (float)H)));
  float width = draw_wordmark(W / 2.0f, 190, 35, ALIGN_CENTER, "#e8ddc6");
  ui_rule(fb, (W - width) / 2, 216, width);
  text(line, W / 2, 252, 12, "#acb7b8");
}

static void draw_error(void) {
  draw_center_screen("#080f16", "");
  ui_panel(fb, 40, 270, 560, 105);
  text("UNABLE TO START", W / 2, 295, 14, "#d3aa87");
  text(error_msg, W / 2, 320, 11, "#d6dedb");
  text("Keep the complete assets and levels folders beside the game.", W / 2, 351, 11, "#9fadb2");
}

static void screen_card(const char *title) {
  menu_background();
  gfx_fill_rect(fb, 0, 0, W, H, rgba(3, 8, 13, .76f));
  ui_panel(fb, 110, 35, 420, 326);
  title_text(title, W / 2, 101, 27, "#e8dcc3");
  ui_rule(fb, 150, 118, 340);
}

static void draw_intro(void) {
  menu_background();
  float width = draw_wordmark(42, 164, 42, ALIGN_LEFT, "#ebe0c9");
  ui_rule(fb, 42, 185, width);
  font_draw(fb, "Betrayed. Imprisoned. Still alive.", 42, 219, 13, FONT_REGULAR, ALIGN_LEFT, rgb(188, 194, 191));
  font_draw(fb, "Five floors between you and freedom.", 42, 241, 12, FONT_REGULAR, ALIGN_LEFT, rgb(150, 162, 165));
  float pulse = .8f + .2f * sinf(clock_time * 2);
  gfx_fill_rect(fb, 42, 280, 225, 35, rgba(13, 20, 26, .82f));
  gfx_fill_rect(fb, 42, 280, 2, 35, rgba(205, 174, 119, pulse));
  char hint[64];
  snprintf(hint, sizeof hint, "%s  MAIN MENU",
           gamepad && gamepad_active ? gamepad_button_name(SDL_GAMEPAD_BUTTON_SOUTH) : "ENTER");
  text_align(hint, 56, 302, 11, "#e5d4b2", ALIGN_LEFT);
}

static void draw_story(void) {
  comic_draw(&comic_art, fb, &comic);
  bool last = comic.panel == COMIC_PANELS - 1;
  const char *next = !last ? "ENTER / SPACE / CLICK — NEXT" :
                     comic.kind == COMIC_INTRO ? "ENTER / CLICK — BEGIN" : "ENTER / CLICK — RESULTS";
  const char *pad = !last ? "%s — NEXT    %s — SKIP" :
                    comic.kind == COMIC_INTRO ? "%s — BEGIN    %s — SKIP" : "%s — RESULTS    %s — SKIP";
  if (gamepad && gamepad_active) control_hint("", pad, 385, 11, "#d8cbaa");
  else {
    text_align(next, 18, 385, 11, "#d8cbaa", ALIGN_LEFT);
    text_align("ESC / SKIP >>", 622, 385, 11, "#d6b570", ALIGN_RIGHT);
  }
}

static void draw_main_menu(void) {
  menu_background();
  draw_wordmark(40, 94, 35, ALIGN_LEFT, "#ebe0c9");
  ui_rule(fb, MAIN_MENU_SPAN.x, 116, MAIN_MENU_SPAN.width);
  draw_menu(main_menu(), main_menu_index, MAIN_MENU_SPAN);
  if (notice_timer > 0) text_align(notice, 320, 342, 11, "#ecdbb7", ALIGN_LEFT);
  menu_footer("CONFIRM");
}

static void draw_difficulty(void) {
  menu_background();
  text_align("Choose your fate", 40, 96, 30, "#ebe0c9", ALIGN_LEFT);
  draw_menu(difficulty_menu(), difficulty_menu_index, DIFFICULTY_MENU_SPAN);
  ui_panel(fb, 325, 145, 275, 152);
  if (difficulty_menu_index < DIFFICULTY_COUNT) {
    static const char *lines[][2] = {{"Weaker foes. Half the damage.", "More bolts for the road ahead."},
                                     {"Standard enemy health and damage.", "Standard bolt supplies."},
                                     {"Stronger foes. Deadlier encounters.", "Fewer bolts."}};
    int i = difficulty_menu_index;
    for (int j = 0; j < 2; j++)
      font_draw(fb, lines[i][j], 344, 184 + j * 22, 11, FONT_REGULAR, ALIGN_LEFT, rgb(177, 186, 187));
    ui_rule(fb, 344, 241, 235);
    char buf[80];
    const DifficultySettings *settings = game_difficulty_settings(DIFFICULTY_CHOICES[i]);
    snprintf(buf, sizeof buf, "%d STARTING BOLTS  /  THREE LIVES", (int)round_half_up(16 * settings->ammo));
    text_align(buf, 344, 274, 9, "#b5a07b", ALIGN_LEFT);
  } else {
    text("RETURN TO THE MAIN MENU", 462, 223, 13, "#d5c7ac");
  }
  text_align("Your difficulty stays with you for the whole escape.", 42, 331, 11, "#9ba6a8", ALIGN_LEFT);
  menu_footer("BEGIN");
}

static void draw_controls(void) {
  menu_background();
  gfx_fill_rect(fb, 0, 0, W, H, rgba(3, 8, 13, .77f));
  title_text("CONTROLS", W / 2.0f, 70, 36, "#d8bd87");
  ui_panel(fb, 32, 95, 576, 242);
  static const char *keyboard_controls[][2] = {
      {"W A S D / ARROWS", "move & turn"},
      {"MOUSE (CLICK VIEW)", "turn & shoot"},
      {"CTRL / J", "shoot"},
      {"E / SPACE", "doors, levers, gate, read / close notes"},
      {"1 / 2 / 3", "dagger, crossbow, repeater"},
      {"SHIFT / TAB / M", "run / map / sound"},
      {"F", "fullscreen"},
      {"F5 / F9", "save / load game"},
      {"ESC", "pause menu"},
  };
  char use[32], map[48];
  if (gamepad) {
    snprintf(use, sizeof use, "%s", gamepad_button_name(SDL_GAMEPAD_BUTTON_SOUTH));
    snprintf(map, sizeof map, "%s / VIEW", gamepad_button_name(SDL_GAMEPAD_BUTTON_NORTH));
  }
  const char *pad_controls[][2] = {
      {"LEFT STICK", "move & strafe"},
      {"RIGHT STICK", "turn"},
      {"RT / R2", "shoot"},
      {use, "doors, levers, gate, read / close notes"},
      {"LB / RB (L1 / R1)", "previous / next weapon"},
      {"LT / L2 / L3", "run (hold)"},
      {map, "map"},
      {"START / OPTIONS", "pause, save & load"},
      {"D-PAD / LEFT STICK", "select menu item"},
  };
  const char *(*controls)[2] = gamepad && gamepad_active ? pad_controls : keyboard_controls;
  for (int i = 0; i < (int)(sizeof keyboard_controls / sizeof keyboard_controls[0]); i++) {
    float y = 121.0f + i * 25;
    if (i % 2 == 0) gfx_fill_rect(fb, 40, y - 17, 560, 24, rgba(102, 126, 141, .045f));
    text_align(controls[i][0], 50, y, 10, "#c3ab80", ALIGN_LEFT);
    font_draw(fb, controls[i][1], 249, y, 12, FONT_REGULAR, ALIGN_LEFT, rgb(196, 202, 199));
  }
  control_hint("ESC / ENTER — BACK", "%s / %s — BACK", 369, 11, "#c7b694");
}

static void draw_quit(void) {
  screen_card("Quit the game?");
  ui_emblem(fb, W / 2, 174, 38);
  font_draw(fb, "Unsaved progress will be lost.", W / 2, 225, 14, FONT_REGULAR, ALIGN_CENTER, rgb(178, 189, 191));
  ui_rule(fb, 170, 251, 300);
  control_hint("ENTER  /  QUIT GAME", "%s  /  QUIT GAME", 287, 13, "#e1c99d");
  char hint[80];
  snprintf(hint, sizeof hint, "%s  /  BACK TO MENU",
           gamepad && gamepad_active ? gamepad_button_name(SDL_GAMEPAD_BUTTON_EAST) : "ESC");
  text(hint, W / 2, 322, 11, "#a5b2b6");
}

static void draw_level_start(void) {
  char buf[80];
  menu_background();
  gfx_fill_rect(fb, 0, 0, W, H, rgba(3, 8, 13, .79f));
  ui_emblem(fb, W / 2, 68, 32);
  snprintf(buf, sizeof buf, "CHAPTER %02d / %02d", game->level_index + 1, LEVEL_COUNT);
  text(buf, W / 2, 117, 11, "#a7b2b5");
  title_text(game->level->name, W / 2.0f, 165, 36, "#e8dcc4");
  ui_rule(fb, 164, 187, 312);
  font_draw(fb, "Find three keys. Open the gate.", W / 2, 222, 14, FONT_REGULAR, ALIGN_CENTER,
            rgb(179, 190, 193));
  snprintf(buf, sizeof buf, "%d LIVES    /    %06d SCORE    /    %s", session.lives, session.score,
           game_difficulty_settings(session.difficulty)->name);
  text(buf, W / 2, 258, 10, "#b5a17c");
  ui_panel(fb, 219, 291, 202, 32);
  control_hint("ENTER  /  BEGIN", "%s  /  BEGIN", 312, 12, "#eadcc1");
  for (int i = 0; i < LEVEL_COUNT; i++) {
    float x = W / 2 - 40 + i * 20;
    if (i < LEVEL_COUNT - 1) gfx_line(fb, x + 3, 361, x + 17, 361, 1, rgba(181, 151, 101, .25f));
    ui_diamond(fb, x, 361, i == game->level_index ? 3 : 2,
               i <= game->level_index ? rgb(195, 167, 116) : rgb(60, 73, 80));
  }
}

static void draw_pause_overlay(void) {
  gfx_fill_rect(fb, 0, 0, W, VIEW_H, rgba(2, 6, 10, .72f));
  ui_panel(fb, 174, 40, 292, 275);
  title_text("PAUSED", W / 2.0f, 99, 29, "#e6d6b7");
  draw_menu(pause_menu(), pause_menu_index, PAUSE_MENU_SPAN);
  if (notice_timer > 0) text(notice, W / 2.0f, 284, 11, "#ead9b0");
  char buf[32];
  snprintf(buf, sizeof buf, "DIFFICULTY: %s", game_difficulty_settings(session.difficulty)->name);
  text(buf, W / 2.0f, 304, 11, "#c9a24a");
  if (gamepad && gamepad_active)
    control_hint("", "%s — CONFIRM    %s / START — RESUME", 316, 9, "#a89878");
}

static void draw_death_fade(void) {
  gfx_fill_rect(fb, 0, 0, W, VIEW_H, rgba(game->pit_fall > 0 ? 0 : 120, 0, 0, minf(0.7f, death_delay * 0.6f)));
}

static void draw_died_overlay(void) {
  gfx_fill_rect(fb, 0, 0, W, VIEW_H, rgba(9, 8, 12, .75f));
  ui_panel(fb, 125, 65, 390, 189);
  title_text("YOU HAVE FALLEN", W / 2, 132, 28, "#ddc4a9");
  ui_rule(fb, 175, 151, 290);
  char lives[48];
  snprintf(lives, sizeof lives, "%d LIVES REMAIN", session.lives);
  text(lives, W / 2, 181, 12, "#a9b5b7");
  control_hint("ENTER  /  TRY THIS FLOOR AGAIN", "%s  /  TRY THIS FLOOR AGAIN", 224, 12, "#d4bd96");
}

static void stat_rows(const char *rows[][2], int n, float y0, float step, float size) {
  for (int i = 0; i < n; i++) {
    float y = y0 + i * step;
    if (i % 2 == 0) gfx_fill_rect(fb, 140, y - size - 3, 360, step - 1, rgba(103, 124, 136, .065f));
    text_align(rows[i][0], 153, y, size, "#a2b1b6", ALIGN_LEFT);
    text_align(rows[i][1], 487, y, size, "#e6d7ba", ALIGN_RIGHT);
  }
}

static int percent(int part, int whole) { return whole ? (int)round_half_up(100.0 * part / whole) : 100; }

static void draw_level_complete(void) {
  char title[48], time[16], kills[48], treasure[48], secrets[32], bonus[24], score[24];
  snprintf(title, sizeof title, "FLOOR %02d CLEARED", game->level_index + 1);
  screen_card(title);

  format_time(time, sizeof time, game->time);
  snprintf(kills, sizeof kills, "%d/%d  (%d%%)", game->kills, game->total_enemies,
           percent(game->kills, game->total_enemies));
  snprintf(treasure, sizeof treasure, "%d/%d  (%d%%)", game->treasure, game->total_treasure,
           percent(game->treasure, game->total_treasure));
  snprintf(secrets, sizeof secrets, "%d/%d", game->secrets_found, game->total_secrets);
  snprintf(bonus, sizeof bonus, "+%d", game->bonus);
  snprintf(score, sizeof score, "%d", session.score);
  const char *rows[][2] = {{"TIME", time},       {"KILLS", kills}, {"TREASURE", treasure},
                           {"SECRETS", secrets}, {"BONUS", bonus}, {"SCORE", score}};
  stat_rows(rows, 6, 150, 26, 13);

  bool last = game->level_index + 1 >= LEVEL_COUNT;
  control_hint(last ? "ENTER  /  CONTINUE" : "ENTER  /  THE NEXT FLOOR",
               last ? "%s  /  CONTINUE" : "%s  /  THE NEXT FLOOR", 337, 12, "#d8c098");
}

static void draw_game_over(void) {
  screen_card("GAME OVER");
  ui_emblem(fb, W / 2, 161, 37);
  char buf[64];
  snprintf(buf, sizeof buf, "%06d", session.score);
  text("FINAL SCORE", W / 2, 212, 9, "#9caaaf");
  text(buf, W / 2, 244, 28, "#e6d7ba");
  snprintf(buf, sizeof buf, "%d ENEMIES DEFEATED", session.total_kills);
  text(buf, W / 2, 271, 10, "#a1afb3");
  control_hint("ENTER  /  PLAY AGAIN", "%s  /  PLAY AGAIN", 310, 12, "#d8c098");
  snprintf(buf, sizeof buf, "%s  /  TITLE SCREEN",
           gamepad && gamepad_active ? gamepad_button_name(SDL_GAMEPAD_BUTTON_EAST) : "ESC");
  text(buf, W / 2, 338, 10, "#9eafb5");
}

static void draw_victory(void) {
  char time[16], kills[48], treasure[48], secrets[32], lives[16], score[24];
  screen_card("YOU ESCAPED");

  format_time(time, sizeof time, session.total_time);
  snprintf(kills, sizeof kills, "%d/%d  (%d%%)", session.total_kills, session.total_enemies,
           percent(session.total_kills, session.total_enemies));
  snprintf(treasure, sizeof treasure, "%d/%d  (%d%%)", session.total_treasure, session.max_treasure,
           percent(session.total_treasure, session.max_treasure));
  snprintf(secrets, sizeof secrets, "%d/%d", session.total_secrets_found, session.max_secrets);
  snprintf(lives, sizeof lives, "%d", session.lives);
  snprintf(score, sizeof score, "%d", session.score);
  const char *rows[][2] = {{"TOTAL TIME", time},  {"KILLS", kills},      {"TREASURE", treasure},
                           {"SECRETS", secrets}, {"LIVES LEFT", lives}, {"FINAL SCORE", score}};
  stat_rows(rows, 6, 150, 26, 13);

  control_hint("ENTER  /  TITLE SCREEN", "%s  /  TITLE SCREEN", 337, 12, "#d8c098");
}

static void render(void) {
  switch (state) {
  case ST_LOADING: draw_center_screen("#101018", "LOADING..."); break;
  case ST_ERROR: draw_error(); break;
  case ST_INTRO: draw_intro(); break;
  case ST_STORY_INTRO:
  case ST_STORY_OUTRO: draw_story(); break;
  case ST_MENU: draw_main_menu(); break;
  case ST_DIFFICULTY: draw_difficulty(); break;
  case ST_CONTROLS: draw_controls(); break;
  case ST_QUIT: draw_quit(); break;
  case ST_LEVEL_START: draw_level_start(); break;
  case ST_PLAYING:
  case ST_PAUSED:
  case ST_DIED:
    render_view(&renderer, fb, game);
    if (game->show_map && state == ST_PLAYING)
      render_map(&renderer, fb, game, gamepad && gamepad_active ? gamepad_button_name(SDL_GAMEPAD_BUTTON_NORTH) : NULL);
    render_hud(&renderer, fb, game);
    if (state == ST_PAUSED) draw_pause_overlay();
    if (state == ST_DIED) draw_died_overlay();
    if (game->player_dead && state == ST_PLAYING) draw_death_fade();
    break;
  case ST_LEVEL_COMPLETE: draw_level_complete(); break;
  case ST_GAME_OVER: draw_game_over(); break;
  case ST_VICTORY: draw_victory(); break;
  }
  if (notice_timer > 0 && state != ST_PLAYING && state != ST_PAUSED && state != ST_MENU)
    text(notice, W / 2.0f, H - 10, 11, "#ead9b0");
}

static void present(void) {
  SDL_UpdateTexture(screen_tex, NULL, fb->px, fb->w * (int)sizeof(uint32_t));
  SDL_SetRenderDrawColor(sdl_renderer, 0, 0, 0, 255);
  SDL_RenderClear(sdl_renderer);
  SDL_RenderTexture(sdl_renderer, screen_tex, NULL, NULL);
  SDL_RenderPresent(sdl_renderer);
}

// ------------------------------------------------------------------ boot

static char *read_level_file(int n) {
  char path[1024];
  const char *base = SDL_GetBasePath();
  if (base) {
    snprintf(path, sizeof path, "%slevels/%d.txt", base, n);
    char *data = SDL_LoadFile(path, NULL);
    if (data) return data;
  }
  snprintf(path, sizeof path, "levels/%d.txt", n);
  return SDL_LoadFile(path, NULL);
}

static bool load_levels(void) {
  for (int i = 0; i < LEVEL_COUNT; i++) {
    char *text = read_level_file(i + 1);
    if (!text) {
      snprintf(error_msg, sizeof error_msg, "Cannot read levels/%d.txt", i + 1);
      return false;
    }
    char err[128];
    bool ok = level_parse(&levels[i], text, err, sizeof err);
    SDL_free(text);
    if (!ok) {
      snprintf(error_msg, sizeof error_msg, "levels/%d.txt: %s", i + 1, err);
      return false;
    }
    levels_loaded++;
  }
  return true;
}

#if FORTRESS_DEBUG_TOOLS
// debug: --level N skips the title screen and starts on that level
static int warp_arg(int argc, char **argv) {
  for (int i = 1; i + 1 < argc; i++) {
    if (!strcmp(argv[i], "--level") || !strcmp(argv[i], "-l")) {
      int n = atoi(argv[i + 1]);
      if (n >= 1 && n <= LEVEL_COUNT) return n;
    }
  }
  return 0;
}
#endif

int main(int argc, char **argv) {
  if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_GAMEPAD)) {
    SDL_Log("SDL_Init failed: %s", SDL_GetError());
    return 1;
  }
  find_gamepad();
  if (!SDL_CreateWindowAndRenderer("Fortress", W * 2, H * 2, SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY,
                                   &window, &sdl_renderer)) {
    SDL_Log("Cannot create the window: %s", SDL_GetError());
    SDL_Quit();
    return 1;
  }
  SDL_SetWindowMinimumSize(window, W, H);
  SDL_SetRenderVSync(sdl_renderer, 1);
  SDL_SetRenderLogicalPresentation(sdl_renderer, W, H, SDL_LOGICAL_PRESENTATION_LETTERBOX);
  screen_tex = SDL_CreateTexture(sdl_renderer, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, W, H);
  SDL_SetTextureBlendMode(screen_tex, SDL_BLENDMODE_NONE);
#if SDL_VERSION_ATLEAST(3, 4, 0)
  SDL_SetTextureScaleMode(screen_tex, SDL_SCALEMODE_PIXELART);
#else
  SDL_SetTextureScaleMode(screen_tex, SDL_SCALEMODE_NEAREST);
#endif
  cursor_default = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_CROSSHAIR);
  cursor_pointer = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_POINTER);
  SDL_SetCursor(cursor_default);

  random_seed(SDL_GetPerformanceCounter());
  fb = image_new(W, H);

  // show something while the art is painted and the sounds synthesized
  render();
  present();
  audio_init();
  bool art_ready = assets_build(&assets);
  bool story_ready = comic_art_build(&comic_art);
  renderer_init(&renderer, &assets);

  if (!art_ready || !story_ready) {
    snprintf(error_msg, sizeof error_msg, "%s",
             !art_ready ? assets_error() : "Missing or invalid title/story artwork in assets/");
    set_state(ST_ERROR);
  } else if (!load_levels()) {
    set_state(ST_ERROR);
  } else {
#if FORTRESS_DEBUG_TOOLS
    int warp = warp_arg(argc, argv);
    if (warp)
      new_game(warp - 1, selected_difficulty);
    else
#endif
      set_state(ST_INTRO);
  }

  Uint64 last = SDL_GetTicksNS();
  while (running) {
    SDL_Event e;
    while (SDL_PollEvent(&e)) handle_event(&e);
    Uint64 now = SDL_GetTicksNS();
    float dt = minf(0.05f, (float)((now - last) / 1e9));
    last = now;
    clock_time += dt;
    state_timer += dt;
    update(dt);
    render();
    present();
  }

  game_destroy(game);
  free(save_path);
  for (int i = 0; i < levels_loaded; i++) level_free(&levels[i]);
  renderer_free(&renderer);
  assets_free(&assets);
  comic_art_free(&comic_art);
  font_shutdown();
  audio_shutdown();
  image_free(fb);
  SDL_DestroyCursor(cursor_default);
  SDL_DestroyCursor(cursor_pointer);
  SDL_DestroyTexture(screen_tex);
  SDL_DestroyRenderer(sdl_renderer);
  SDL_DestroyWindow(window);
  if (gamepad) SDL_CloseGamepad(gamepad);
  SDL_Quit();
  return 0;
}
