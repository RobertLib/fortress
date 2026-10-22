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
#include "font.h"
#include "game.h"
#include "gfx.h"
#include "level.h"
#include "render.h"
#include "savegame.h"

#define W SCREEN_W
#define H SCREEN_H

typedef enum {
  ST_LOADING,
  ST_INTRO,
  ST_MENU,
  ST_DIFFICULTY,
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

// ------------------------------------------------------------------ helpers

static bool fullscreen(void) { return (SDL_GetWindowFlags(window) & SDL_WINDOW_FULLSCREEN) != 0; }

static void toggle_fullscreen(void) { SDL_SetWindowFullscreen(window, !fullscreen()); }

static bool mouse_captured(void) { return SDL_GetWindowRelativeMouseMode(window); }

static void capture_mouse(bool on) { SDL_SetWindowRelativeMouseMode(window, on); }

static void blip(void) { audio_play(SND_BLIP, 1, 1); }

static void clear_input(void) {
  input.forward = input.back = input.strafe_left = input.strafe_right = false;
  input.turn_left = input.turn_right = input.run = input.strafe_mod = false;
  input.fire = input.use = false;
  input.weapon = -1;
  input.mouse_dx = 0;
}

// ------------------------------------------------------------------ flow

static void set_state(State s) {
  state = s;
  state_timer = 0;
  clear_input();
  notice_timer = 0;
  // once the run has ended there is nothing left to continue
  if (s == ST_GAME_OVER || s == ST_VICTORY) resumable = false;
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
    audio_play(SND_VICTORY, 1, 1);
    set_state(ST_VICTORY);
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

// vertical spans the fitted menus are drawn into: top, bottom, max line
// height, max font size
typedef struct {
  float top, bottom, max_line_h, max_size;
} MenuSpan;

static const MenuSpan MAIN_MENU_SPAN = {100, 335, 42, 24};
static const MenuSpan PAUSE_MENU_SPAN = {120, 275, 28, 17};
static const MenuSpan DIFFICULTY_MENU_SPAN = {120, 285, 38, 24};

typedef struct {
  float cy, line_h, size;
} MenuLayout;

// Fit a menu into its vertical span: full size while it fits,
// proportionally smaller line height and font once items no longer do.
static MenuLayout menu_layout(Menu m, MenuSpan span) {
  MenuLayout l;
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
    if (y < by - l.size * 0.9f || y > by + l.size * 0.35f) continue;
    float half = font_measure(m.items[i].label(), l.size, FONT_BOLD) / 2 + 28;
    if (fabsf(x - W / 2.0f) <= half) return i;
  }
  return -1;
}

// screens where a click anywhere acts like ENTER
static bool click_state(State s) {
  switch (s) {
  case ST_INTRO:
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
  if (state == ST_MENU) {
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
  if (sc == SDL_SCANCODE_TAB && game && state == ST_PLAYING) game->show_map = !game->show_map;
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
  case SDL_EVENT_WINDOW_FOCUS_LOST: clear_input(); break;
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
  if (state == ST_LEVEL_START && state_timer > 3) begin_level();
  if (state != ST_PLAYING) return;
  game_update(game, dt, &input);

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

static bool blink(void) { return (int)floorf(clock_time * 2) % 2 == 0; }

static void fill(const char *color) { gfx_fill_rect(fb, 0, 0, W, H, color_parse(color)); }

static void text_align(const char *str, float x, float y, float size, const char *color, TextAlign align) {
  font_draw(fb, str, x, y, size, FONT_BOLD, align, color_parse(color));
}

static void text(const char *str, float x, float y, float size, const char *color) {
  text_align(str, x, y, size, color, ALIGN_CENTER);
}

static void title_text(const char *str, float x, float y, float size, const char *color) {
  font_draw(fb, str, x + size * 0.06f, y + size * 0.06f, size, FONT_BOLD, ALIGN_CENTER, rgb(0, 0, 0));
  font_draw(fb, str, x, y, size, FONT_BOLD, ALIGN_CENTER, color_parse(color));
}

static void rule(float cx, float y, float half_w) { gfx_fill_rect(fb, cx - half_w, y, half_w * 2, 2, color_parse("#c9a24a")); }

// torch glow behind a heading
static void torch_glow(float cy, float radius) {
  GradientStop stops[2] = {{0, {200, 120, 40, 0.22f}}, {1, {200, 120, 40, 0}}};
  gfx_radial(fb, 0, 0, W, H, W / 2.0f, cy, 10, radius, stops, 2, BLEND_OVER);
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
    const char *label = m.items[i].label();
    if (i == index) {
      title_text(label, W / 2.0f, y, l.size, blink() ? "#e9c93c" : "#ead9b0");
      // a bolt-tip pointer beside the chosen line
      float half = font_measure(label, l.size, FONT_BOLD) / 2;
      float py = y - l.size * 0.34f;
      float tri[6] = {W / 2.0f - half - 26, py - l.size * 0.3f, W / 2.0f - half - 26, py + l.size * 0.3f,
                      W / 2.0f - half - 12, py};
      gfx_fill_poly(fb, tri, 3, color_parse(blink() ? "#e9c93c" : "#c9a24a"));
    } else {
      text(label, W / 2.0f, y, l.size, "#a89878");
    }
  }
}

static void draw_center_screen(const char *bg, const char *line) {
  fill(bg);
  text(line, W / 2.0f, H / 2.0f, 16, "#e8e8e8");
}

static void draw_error(void) {
  fill("#180808");
  text("FAILED TO LOAD LEVELS", W / 2.0f, 150, 22, "#e05050");
  text(error_msg, W / 2.0f, 190, 12, "#c8c8c8");
  text("The levels folder must sit next to the game", W / 2.0f, 240, 14, "#e8e8e8");
  text("or in the directory it is started from.", W / 2.0f, 268, 14, "#e8e8e8");
}

static void draw_intro(void) {
  fill("#140e08");
  torch_glow(105, 280);
  title_text("F O R T R E S S", W / 2.0f, 110, 52, "#e9c93c");
  rule(W / 2.0f, 130, 150);
  text("A MEDIEVAL RAYCASTING ADVENTURE", W / 2.0f, 158, 11, "#a89878");

  static const char *story[] = {
      "Betrayed and thrown into the fortress dungeons,",
      "you have slipped your chains. Five floors of the",
      "garrison stand between you and the night. Find the",
      "3 KEYS on each floor, unlock the GATE, and escape.",
  };
  for (int i = 0; i < 4; i++) text(story[i], W / 2.0f, 205.0f + i * 20, 12, "#d8cbaa");

  if (blink()) title_text("PRESS ENTER", W / 2.0f, 335, 18, "#ead9b0");
}

static void draw_main_menu(void) {
  fill("#140e08");
  torch_glow(80, 240);
  title_text("F O R T R E S S", W / 2.0f, 80, 34, "#e9c93c");
  rule(W / 2.0f, 98, 120);
  // the block lives between the heading rule (~y100) and the hint line (~y360)
  draw_menu(main_menu(), main_menu_index, MAIN_MENU_SPAN);
  if (notice_timer > 0) text(notice, W / 2.0f, 350, 12, "#ead9b0");
  text("ARROWS / MOUSE — SELECT      ENTER / CLICK — CONFIRM      ESC — BACK", W / 2.0f, 370, 10, "#786850");
}

static void draw_difficulty(void) {
  fill("#140e08");
  torch_glow(80, 240);
  title_text("CHOOSE DIFFICULTY", W / 2.0f, 80, 32, "#e9c93c");
  rule(W / 2.0f, 98, 120);
  draw_menu(difficulty_menu(), difficulty_menu_index, DIFFICULTY_MENU_SPAN);

  if (difficulty_menu_index < DIFFICULTY_COUNT) {
    static const char *descriptions[] = {
        "WEAKER FOES. HALF DAMAGE. MORE BOLTS.",
        "THE ORIGINAL FORTRESS CHALLENGE.",
        "TOUGHER FOES. MORE DAMAGE. FEWER BOLTS.",
    };
    char buf[64];
    Difficulty difficulty = DIFFICULTY_CHOICES[difficulty_menu_index];
    const DifficultySettings *settings = game_difficulty_settings(difficulty);
    text(descriptions[difficulty_menu_index], W / 2.0f, 310, 13, "#d8cbaa");
    snprintf(buf, sizeof buf, "START WITH %d BOLTS. CHOSEN FOR THE WHOLE RUN.",
             (int)round_half_up(16 * settings->ammo));
    text(buf, W / 2.0f, 335, 11, "#a89878");
  }
  text("ARROWS / MOUSE — SELECT      ENTER / CLICK — START      ESC — BACK", W / 2.0f, 370, 10, "#786850");
}

static void draw_controls(void) {
  fill("#140e08");
  title_text("CONTROLS", W / 2.0f, 70, 36, "#e9c93c");
  rule(W / 2.0f, 88, 120);
  static const char *controls[][2] = {
      {"W A S D / ARROWS", "move & turn"},
      {"MOUSE (CLICK VIEW)", "turn & shoot"},
      {"CTRL / J", "shoot"},
      {"E / SPACE", "open doors, pull levers, use the gate"},
      {"1 / 2 / 3", "dagger, crossbow, repeater"},
      {"SHIFT / TAB / M", "run / map / sound"},
      {"F", "fullscreen"},
      {"F5 / F9", "save / load game"},
      {"ESC", "pause menu"},
  };
  for (int i = 0; i < (int)(sizeof controls / sizeof controls[0]); i++) {
    float y = 130.0f + i * 24;
    text_align(controls[i][0], W / 2.0f - 12, y, 13, "#c9a24a", ALIGN_RIGHT);
    text_align(controls[i][1], W / 2.0f + 2, y, 13, "#d8cbaa", ALIGN_LEFT);
  }
  if (blink()) text("ESC / ENTER — BACK", W / 2.0f, 350, 13, "#ead9b0");
}

static void draw_quit(void) {
  fill("#140e08");
  title_text("FLEEING ALREADY?", W / 2.0f, 140, 32, "#e9c93c");
  text("The fortress will be waiting for your return.", W / 2.0f, 185, 14, "#d8cbaa");
  if (blink()) text("ENTER — LEAVE THE FORTRESS", W / 2.0f, 250, 15, "#e9c93c");
  text("ESC — STAY AND FIGHT", W / 2.0f, 278, 13, "#d8cbaa");
}

static void draw_level_start(void) {
  char buf[64];
  fill("#140e08");
  snprintf(buf, sizeof buf, "FLOOR %d OF %d", game->level_index + 1, LEVEL_COUNT);
  text(buf, W / 2.0f, 130, 16, "#a89878");
  title_text(game->level->name, W / 2.0f, 180, 36, "#e9c93c");
  text("FIND 3 KEYS AND REACH THE GATE", W / 2.0f, 230, 14, "#d8cbaa");
  snprintf(buf, sizeof buf, "LIVES: %d    SCORE: %d", session.lives, session.score);
  text(buf, W / 2.0f, 262, 13, "#a89878");
  snprintf(buf, sizeof buf, "DIFFICULTY: %s", game_difficulty_settings(session.difficulty)->name);
  text(buf, W / 2.0f, 286, 12, "#c9a24a");
  if (blink()) text("PRESS ENTER", W / 2.0f, 320, 16, "#ead9b0");
}

static void draw_pause_overlay(void) {
  gfx_fill_rect(fb, 0, 0, W, VIEW_H, rgba(0, 0, 0, 0.6f));
  title_text("PAUSED", W / 2.0f, 110, 34, "#e9c93c");
  draw_menu(pause_menu(), pause_menu_index, PAUSE_MENU_SPAN);
  if (notice_timer > 0) text(notice, W / 2.0f, 284, 11, "#ead9b0");
  char buf[32];
  snprintf(buf, sizeof buf, "DIFFICULTY: %s", game_difficulty_settings(session.difficulty)->name);
  text(buf, W / 2.0f, 304, 11, "#c9a24a");
}

static void draw_death_fade(void) { gfx_fill_rect(fb, 0, 0, W, VIEW_H, rgba(120, 0, 0, minf(0.7f, death_delay * 0.6f))); }

static void draw_died_overlay(void) {
  char buf[32];
  gfx_fill_rect(fb, 0, 0, W, VIEW_H, rgba(60, 0, 0, 0.7f));
  title_text("YOU HAVE FALLEN", W / 2.0f, 130, 36, "#e05050");
  snprintf(buf, sizeof buf, "LIVES LEFT: %d", session.lives);
  text(buf, W / 2.0f, 175, 16, "#ead9b0");
  if (blink()) text("PRESS ENTER TO TRY THIS FLOOR AGAIN", W / 2.0f, 220, 14, "#e9c93c");
}

static void stat_rows(const char *rows[][2], int n, float y0, float step, float size) {
  for (int i = 0; i < n; i++) {
    float y = y0 + i * step;
    text_align(rows[i][0], W / 2.0f - 30, y, size, "#a89878", ALIGN_RIGHT);
    text_align(rows[i][1], W / 2.0f - 5, y, size, "#ead9b0", ALIGN_LEFT);
  }
}

static int percent(int part, int whole) { return whole ? (int)round_half_up(100.0 * part / whole) : 100; }

static void draw_level_complete(void) {
  char title[48], time[16], kills[48], treasure[48], secrets[32], bonus[24], score[24];
  fill("#140e08");
  snprintf(title, sizeof title, "FLOOR %d CLEARED!", game->level_index + 1);
  text(title, W / 2.0f, 90, 26, "#e9c93c");

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
  stat_rows(rows, 6, 145, 28, 15);

  bool last = game->level_index + 1 >= LEVEL_COUNT;
  if (blink()) text(last ? "PRESS ENTER" : "PRESS ENTER FOR THE NEXT FLOOR", W / 2.0f, 330, 15, "#e9c93c");
}

static void draw_game_over(void) {
  char buf[48];
  fill("#180808");
  title_text("GAME OVER", W / 2.0f, 150, 52, "#c02020");
  snprintf(buf, sizeof buf, "FINAL SCORE: %d", session.score);
  text(buf, W / 2.0f, 205, 18, "#ead9b0");
  snprintf(buf, sizeof buf, "KILLS: %d", session.total_kills);
  text(buf, W / 2.0f, 235, 13, "#a89878");
  if (blink()) text("ENTER — PLAY AGAIN", W / 2.0f, 290, 15, "#e9c93c");
  text("ESC — TITLE SCREEN", W / 2.0f, 318, 13, "#d8cbaa");
}

static void draw_victory(void) {
  char time[16], kills[48], treasure[48], secrets[32], lives[16], score[24];
  fill("#100c14");
  title_text("YOU ESCAPED THE FORTRESS!", W / 2.0f, 90, 30, "#e9c93c");
  text("WELL FOUGHT — YOU ARE FREE.", W / 2.0f, 128, 14, "#d8cbaa");
  text("Thanks for playing this little retro adventure.", W / 2.0f, 150, 12, "#a89878");

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
  stat_rows(rows, 6, 190, 25, 14);

  if (blink()) text("PRESS ENTER FOR THE TITLE SCREEN", W / 2.0f, 350, 14, "#e9c93c");
}

static void render(void) {
  switch (state) {
  case ST_LOADING: draw_center_screen("#101018", "LOADING..."); break;
  case ST_ERROR: draw_error(); break;
  case ST_INTRO: draw_intro(); break;
  case ST_MENU: draw_main_menu(); break;
  case ST_DIFFICULTY: draw_difficulty(); break;
  case ST_CONTROLS: draw_controls(); break;
  case ST_QUIT: draw_quit(); break;
  case ST_LEVEL_START: draw_level_start(); break;
  case ST_PLAYING:
  case ST_PAUSED:
  case ST_DIED:
    render_view(&renderer, fb, game);
    if (game->show_map && state == ST_PLAYING) render_map(&renderer, fb, game);
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
  if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO)) {
    SDL_Log("SDL_Init failed: %s", SDL_GetError());
    return 1;
  }
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
  assets_build(&assets);
  renderer_init(&renderer, &assets);

  if (!load_levels()) {
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
  font_shutdown();
  audio_shutdown();
  image_free(fb);
  SDL_DestroyCursor(cursor_default);
  SDL_DestroyCursor(cursor_pointer);
  SDL_DestroyTexture(screen_tex);
  SDL_DestroyRenderer(sdl_renderer);
  SDL_DestroyWindow(window);
  SDL_Quit();
  return 0;
}
