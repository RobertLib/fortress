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
#include "preferences.h"

#define W SCREEN_W
#define H SCREEN_H

typedef enum {
  ST_LOADING,
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
  ST_ERROR,
  ST_OPTIONS,
  ST_BINDINGS,
  ST_SLOTS
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
static State options_return=ST_MENU,slots_return=ST_MENU;
static int options_page,options_row,binding_row,slot_row,selected_slot;
static bool binding_wait,slot_save_mode;
static float autosave_timer;
static float combat_music_tail;
static char *manual_paths[2],*automatic_path,*checkpoint_path;
static SaveInfo slot_info[5];
static bool slot_valid[5];
static void refresh_save_slots(void);
static void checkpoint_game(void);
static bool restore_checkpoint(void);
static const char *gamepad_button_name(SDL_GamepadButton button);

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

static void pause_play(void) {
  set_state(ST_PAUSED);
  pause_menu_index = 0;
  capture_mouse(false);
  blip();
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
  session.arcade=preferences_get()->arcade;
  session.run_id=(int)((SDL_GetTicksNS()^random_state())&0x7fffffffu);
  if (!session.run_id) session.run_id=1;
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
  checkpoint_game();
  blip();
}

static void restart_level(void) {
  if (!session.arcade && restore_checkpoint()) return;
  // Completion percentages describe the surviving attempt. Arcade score,
  // lives and relics still carry over, but rediscoveries must not count twice.
  session.total_kills = maxi(0, session.total_kills - game->kills);
  session.total_treasure = maxi(0, session.total_treasure - game->treasure);
  session.total_secrets_found = maxi(0, session.total_secrets_found - game->secrets_found);
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

static char *user_file(const char *name) {
  const char *override=SDL_getenv("FORTRESS_USER_DATA");
  char *directory=override?NULL:SDL_GetPrefPath("Fortress","Fortress");
  const char *base=override?override:directory;
  if (!base) return NULL;
  size_t size=strlen(base)+strlen(name)+2;
  char *path=malloc(size);
  if (path) snprintf(path,size,"%s/%s",base,name);
  SDL_free(directory); return path;
}
static const char *get_save_path(void) {
  if (save_path) return save_path;
  save_path=user_file("savegame.dat");
  return save_path;
}
static const char *slot_path(int slot) {
  if (slot==0) return get_save_path();
  if (slot>=1 && slot<=2) {
    if (!manual_paths[slot-1]) manual_paths[slot-1]=user_file(slot==1?"savegame-2.dat":"savegame-3.dat");
    return manual_paths[slot-1];
  }
  if (slot==3) { if (!automatic_path) automatic_path=user_file("autosave.dat"); return automatic_path; }
  if (!checkpoint_path) checkpoint_path=user_file("checkpoint.dat");
  return checkpoint_path;
}

static void notify_save(const char *message) {
  snprintf(notice, sizeof notice, "%s", message);
  notice_timer = 4;
  if (game && state == ST_PLAYING) game_say(game, message, 3);
}

static void act_save_game(void) {
  const char *path = slot_path(selected_slot);
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
  const char *path = slot_path(selected_slot);
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

static void checkpoint_game(void) {
  if (!game || !preferences_get()->autosave || session.arcade || game->player_dead || game->completed) return;
  const char *path=slot_path(4); char err[96];
  if (path && savegame_write(path,game,err,sizeof err)) autosave_timer=0;
  path=slot_path(3);
  if (path) savegame_write(path,game,err,sizeof err);
}
static bool restore_checkpoint(void) {
  for (int slot=3;slot<=4;slot++) {
    const char *path=slot_path(slot); SaveInfo info; char err[96];
    if (path && session.run_id && savegame_inspect(path,levels,levels_loaded,&info) &&
        info.level==game->level_index && info.run_id==session.run_id &&
        savegame_load(path,levels,levels_loaded,&game,&session,err,sizeof err)) {
      death_delay=0; set_state(ST_PLAYING); game_say(game,"SAFE CHECKPOINT RESTORED",3); return true;
    }
  }
  return false;
}
static bool safe_to_autosave(void) {
  if (!game || game->player.health<35 || game->player.venom || game->horror.reading>=0 || game->campaign.journal ||
      game->player_dead || game->completed || game->horror.warden.state!=WARDEN_DORMANT ||
      (game->horror.pending_hunts && !horror_in_haven(game,game->player.x,game->player.y))) return false;
  for (int i=0;i<game->enemy_count;i++) {
    const Enemy *e=&game->enemies[i];
    if (!enemy_dead(e) && hypotf(e->x-game->player.x,e->y-game->player.y)<6) return false;
  }
  for (int i=0;i<game->projectile_count;i++) {
    const Projectile *p=&game->projectiles[i];
    if ((p->kind==SHOT_HOSTILE || p->kind==SHOT_TRAP || p->kind==SHOT_AT_PLAYER) &&
        hypotf(p->x-game->player.x,p->y-game->player.y)<8) return false;
  }
  for (int i=0;i<game->level->pendulum_count;i++) {
    const PendulumSpawn *p=&game->level->pendulums[i];
    if (hypotf(p->x+.5f-game->player.x,p->y+.5f-game->player.y)<1.5f) return false;
  }
  return true;
}
static void autosave_game(float dt) {
  if (session.arcade || !preferences_get()->autosave) return;
  autosave_timer+=dt;
  if ((autosave_timer<90 && !game->campaign.checkpoint_requested) || !safe_to_autosave()) return;
  const char *path=slot_path(3); char err[96];
  if (path && savegame_write(path,game,err,sizeof err)) {
    autosave_timer=0; game->campaign.checkpoint_requested=false;
    notify_save("PROGRESS AUTOSAVED");
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
static const char *label_options(void) { return "OPTIONS"; }
static const char *label_slots(void) { return "SAVE SLOTS"; }
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
static void act_options(void) {
  options_return=state; options_page=0; options_row=0; set_state(ST_OPTIONS); blip();
}
static void act_slots(void) {
  slots_return=state; slot_save_mode=state==ST_PAUSED; slot_row=selected_slot;
  refresh_save_slots();
  set_state(ST_SLOTS); blip();
}
static void save_preferences(void) {
  char *path=user_file("options.cfg");
  if (!path || !preferences_save(path)) notify_save("COULD NOT SAVE OPTIONS");
  free(path);
}
static int option_rows(void) { static const int rows[]={4,2,3,3}; return rows[options_page]; }
static void adjust_option(int direction) {
  Preferences *p=preferences_get();
  if (options_page==0) {
    int *values[]={&p->brightness,&p->bob,&p->shake,&p->text};
    int lo[]={70,0,0,100},hi[]={140,100,100,150},step[]={10,25,25,25};
    *values[options_row]=clampi(*values[options_row]+step[options_row]*direction,lo[options_row],hi[options_row]);
  } else if (options_page==1) {
    int *value=options_row==0?&p->effects:&p->music;
    *value=clampi(*value+5*direction,0,100);
    audio_set_mix(p->effects,p->music);
  } else if (options_page==2) {
    bool *values[]={&p->tutorials,&p->autosave,&p->arcade}; *values[options_row]=!*values[options_row];
  } else if (options_row==0) p->sensitivity=clampi(p->sensitivity+25*direction,25,250);
  else if (options_row==1) { binding_row=0; binding_wait=false; set_state(ST_BINDINGS); }
  else { preferences_defaults(); audio_set_mix(p->effects,p->music); }
  blip();
}
static void activate_slot(void) {
  const char *path=slot_path(slot_row); char err[96];
  if (!path) { notify_save("SAVE FOLDER UNAVAILABLE"); return; }
  if (slot_save_mode && slot_row<3) {
    if (savegame_write(path,game,err,sizeof err)) { selected_slot=slot_row; refresh_save_slots(); notify_save("GAME SAVED"); blip(); }
    else notify_save(err);
  } else if (!slot_save_mode) {
    if (savegame_load(path,levels,levels_loaded,&game,&session,err,sizeof err)) {
      if (slot_row<3) selected_slot=slot_row;
      selected_difficulty=session.difficulty; resumable=false; death_delay=0;
      set_state(ST_PLAYING); notify_save("GAME LOADED"); blip();
    } else notify_save(err);
  } else notify_save("AUTOMATIC SLOTS ARE READ ONLY");
}
static void refresh_save_slots(void) {
  for (int i=0;i<5;i++) {
    const char *path=slot_path(i);
    slot_valid[i]=path && savegame_inspect(path,levels,levels_loaded,&slot_info[i]);
  }
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
    {label_options,act_options}, {label_slots,act_slots}, {label_quit, act_quit},
};

static const MenuItem PAUSE_MENU[] = {
    {label_resume, act_resume},
    {label_save_game, act_save_game},
    {label_load_game, act_load_game},
    {label_slots,act_slots},
    {label_options,act_options},
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
  case ST_OPTIONS:
    if (esc) { set_state(options_return); save_preferences(); }
    else if (sc==SDL_SCANCODE_TAB) { options_page=(options_page+1)%4; options_row=0; blip(); }
    else if (sc==SDL_SCANCODE_UP || sc==SDL_SCANCODE_DOWN) options_row=menu_nav(sc,option_rows(),options_row);
    else if (sc==SDL_SCANCODE_LEFT || sc==SDL_SCANCODE_RIGHT || enter) adjust_option(sc==SDL_SCANCODE_LEFT?-1:1);
    break;
  case ST_BINDINGS:
    if (esc) { if (binding_wait) binding_wait=false; else set_state(ST_OPTIONS); }
    else if (sc==SDL_SCANCODE_UP || sc==SDL_SCANCODE_DOWN) binding_row=menu_nav(sc,ACTION_COUNT,binding_row);
    else if (enter) { binding_wait=true; blip(); }
    break;
  case ST_SLOTS:
    if (esc) set_state(slots_return);
    else if (sc==SDL_SCANCODE_UP || sc==SDL_SCANCODE_DOWN) slot_row=menu_nav(sc,5,slot_row);
    else if (sc==SDL_SCANCODE_LEFT || sc==SDL_SCANCODE_RIGHT) {
      if (game && !game->player_dead && !game->completed) slot_save_mode=!slot_save_mode;
      else slot_save_mode=false;
    } else if (enter) activate_slot();
    break;
  case ST_MENU: {
    Menu m = main_menu();
    main_menu_index = menu_nav(sc, m.count, main_menu_index);
    if (enter) {
      m.items[main_menu_index].action();
    } else if (esc) {
      act_quit();
    }
#if FORTRESS_DEBUG_TOOLS
    else if (sc >= SDL_SCANCODE_1 && sc <= SDL_SCANCODE_9) {
      // debug: a number key in the main menu warps to that level
      int n = sc - SDL_SCANCODE_1 + 1;
      if (n <= LEVEL_COUNT) {
        audio_play(SND_START, 1, 1);
        new_game(n - 1, selected_difficulty);
      }
    }
#endif
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
      if (game->campaign.journal) { game->campaign.journal=false; clear_input(); return; }
      pause_play();
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
      main_menu_index = 0;
      set_state(ST_MENU);
    }
    break;
  case ST_VICTORY:
    if (enter || esc) {
      main_menu_index = 0;
      set_state(ST_MENU);
    }
    break;
  default: break;
  }
}

static void handle_state_click(float x, float y) {
  if (state==ST_OPTIONS) {
    if (y>=83 && y<=108 && x>=40 && x<=600) { options_page=clampi((int)((x-40)/140),0,3); options_row=0; blip(); }
    else if (y>=119 && y<119+option_rows()*36 && x>=70 && x<=570) {
      options_row=(int)((y-119)/36);
      if (options_page==2 || (options_page==3 && options_row>0)) adjust_option(1);
      else if (x>=470) adjust_option(x<530?-1:1);
    } else if (y>=359) { set_state(options_return); save_preferences(); }
  } else if (state==ST_BINDINGS) {
    if (y>=91 && y<91+ACTION_COUNT*19 && x>=65 && x<=575) { binding_row=(int)((y-91)/19); binding_wait=true; }
    else if (y>=359) { binding_wait=false; set_state(ST_OPTIONS); }
  } else if (state==ST_SLOTS) {
    if (y>=112 && y<112+5*39 && x>=65 && x<=575) { slot_row=(int)((y-112)/39); activate_slot(); }
    else if (y>=83 && y<=108) {
      slot_save_mode=x>320 && game && !game->player_dead && !game->completed;
    } else if (y>=359) set_state(slots_return);
  } else if (story_state()) {
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
  if (state==ST_OPTIONS) {
    if (x>=70 && x<=570 && y>=119 && y<119+option_rows()*36) { hit=(int)((y-119)/36); options_row=hit; }
    else if (x>=40 && x<=600 && y>=83 && y<=108) hit=0;
  } else if (state==ST_BINDINGS) {
    if (!binding_wait && x>=65 && x<=575 && y>=91 && y<91+ACTION_COUNT*19) { hit=(int)((y-91)/19); binding_row=hit; }
  } else if (state==ST_SLOTS) {
    if (x>=65 && x<=575 && y>=112 && y<112+5*39) { hit=(int)((y-112)/39); slot_row=hit; }
    else if (y>=83 && y<=108) hit=0;
  } else if (state == ST_MENU) {
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
    pause_play();
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
  if (state==ST_OPTIONS && (button==SDL_GAMEPAD_BUTTON_LEFT_SHOULDER || button==SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER)) {
    options_page=(options_page+(button==SDL_GAMEPAD_BUTTON_LEFT_SHOULDER?3:1))%4; options_row=0; blip(); return;
  }
  if ((state==ST_OPTIONS || state==ST_SLOTS) && (button==SDL_GAMEPAD_BUTTON_DPAD_LEFT || button==SDL_GAMEPAD_BUTTON_DPAD_RIGHT)) {
    handle_state_key(button==SDL_GAMEPAD_BUTTON_DPAD_LEFT?SDL_SCANCODE_LEFT:SDL_SCANCODE_RIGHT); return;
  }
  if (button == SDL_GAMEPAD_BUTTON_START) {
    handle_state_key(state == ST_PLAYING || state == ST_PAUSED || state==ST_OPTIONS || state==ST_BINDINGS || state==ST_SLOTS ? SDL_SCANCODE_ESCAPE : SDL_SCANCODE_RETURN);
  } else if (state != ST_PLAYING) {
    if (button == SDL_GAMEPAD_BUTTON_SOUTH) handle_state_key(SDL_SCANCODE_RETURN);
    if (button == SDL_GAMEPAD_BUTTON_EAST) handle_state_key(SDL_SCANCODE_ESCAPE);
    if ((state == ST_MENU || state == ST_DIFFICULTY || state == ST_PAUSED || state==ST_OPTIONS || state==ST_BINDINGS || state==ST_SLOTS) &&
        (button == SDL_GAMEPAD_BUTTON_DPAD_UP || button == SDL_GAMEPAD_BUTTON_DPAD_DOWN)) {
      // Handle the press immediately so a following confirm in the same frame
      // selects the new item. Polling below supplies only the held repeats.
      gamepad_menu_direction = button == SDL_GAMEPAD_BUTTON_DPAD_UP ? -1 : 1;
      gamepad_menu_timer = 0.4f;
      gamepad_menu_blocked = false;
      handle_state_key(gamepad_menu_direction < 0 ? SDL_SCANCODE_UP : SDL_SCANCODE_DOWN);
    }
  } else {
    if (game->campaign.journal) {
      if (button==SDL_GAMEPAD_BUTTON_EAST || button==SDL_GAMEPAD_BUTTON_SOUTH || button==SDL_GAMEPAD_BUTTON_DPAD_RIGHT)
        game->campaign.journal=false;
      if (button==SDL_GAMEPAD_BUTTON_LEFT_SHOULDER || button==SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER)
        game->campaign.journal_page=1-game->campaign.journal_page;
      clear_input(); return;
    }
    switch (button) {
    case SDL_GAMEPAD_BUTTON_SOUTH: input.use = true; break;
    case SDL_GAMEPAD_BUTTON_NORTH:
    case SDL_GAMEPAD_BUTTON_BACK:
    case SDL_GAMEPAD_BUTTON_TOUCHPAD:
      if (game->horror.reading < 0) game->show_map = !game->show_map;
      break;
    case SDL_GAMEPAD_BUTTON_LEFT_SHOULDER: cycle_weapon(-1); break;
    case SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER: cycle_weapon(1); break;
    case SDL_GAMEPAD_BUTTON_WEST:
      if (game->horror.reading<0 && !game->show_map) campaign_throw_decoy(game);
      break;
    case SDL_GAMEPAD_BUTTON_DPAD_LEFT:
      if (game->horror.reading<0 && !game->show_map) campaign_brace_door(game);
      break;
    case SDL_GAMEPAD_BUTTON_DPAD_RIGHT:
      if (game->horror.reading<0) { game->campaign.journal=true; game->show_map=false; clear_input(); }
      break;
    default: break;
    }
  }
}

static void update_gamepad(float dt) {
  if (!gamepad || !input_focused) return;
  // A held trigger must be released after a pause, load or state change.
  if (!gamepad_firing()) gamepad_fire_ready = true;
  if (state != ST_MENU && state != ST_DIFFICULTY && state != ST_PAUSED && state!=ST_OPTIONS && state!=ST_BINDINGS && state!=ST_SLOTS) return;
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
  switch (preferences_action_for_key(sc)) {
  case ACTION_FORWARD: return &input.forward;
  case ACTION_BACK: return &input.back;
  case ACTION_LEFT: return &input.strafe_left;
  case ACTION_RIGHT: return &input.strafe_right;
  case ACTION_FIRE: return &input.fire;
  case ACTION_RUN: return &input.run;
  default: break;
  }
  switch (sc) {
  case SDL_SCANCODE_UP: return &input.forward;
  case SDL_SCANCODE_DOWN: return &input.back;
  case SDL_SCANCODE_LEFT: return &input.turn_left;
  case SDL_SCANCODE_RIGHT: return &input.turn_right;
  case SDL_SCANCODE_LALT:
  case SDL_SCANCODE_RALT: return &input.strafe_mod;
  case SDL_SCANCODE_LCTRL:
  case SDL_SCANCODE_RCTRL: return &input.fire;
  default: return NULL;
  }
}

static void on_key_down(SDL_Scancode sc) {
  gamepad_active = false;
  int action=preferences_action_for_key(sc);
  if (state==ST_BINDINGS && binding_wait) {
    if (sc==SDL_SCANCODE_ESCAPE) { binding_wait=false; return; }
    if (preferences_bind((KeyAction)binding_row,sc)) { binding_wait=false; blip(); }
    else notify_save("THIS KEY IS RESERVED - CHOOSE ANOTHER");
    return;
  }
  if (sc == SDL_SCANCODE_F5 && (state == ST_PLAYING || state == ST_PAUSED)) {
    act_save_game(); return;
  }
  if (sc == SDL_SCANCODE_F9 && state != ST_LOADING && state != ST_ERROR && state != ST_QUIT) {
    act_load_game(); return;
  }
  if (state==ST_PLAYING && game && game->campaign.journal) {
    if (sc==SDL_SCANCODE_LEFT || sc==SDL_SCANCODE_RIGHT) game->campaign.journal_page=1-game->campaign.journal_page;
    if (action==ACTION_JOURNAL || sc==SDL_SCANCODE_ESCAPE || sc==SDL_SCANCODE_RETURN || sc==SDL_SCANCODE_SPACE)
      game->campaign.journal=false;
    clear_input(); return;
  }
  if (state==ST_PLAYING && game && game->horror.reading<0) {
    if (action==ACTION_JOURNAL) { game->campaign.journal=true; game->show_map=false; clear_input(); return; }
    if (action==ACTION_DECOY && !game->show_map) { campaign_throw_decoy(game); return; }
    if (action==ACTION_BRACE && !game->show_map) { campaign_brace_door(game); return; }
  }
  bool *b = key_binding(sc);
  if (b) *b = true;
  if (action==ACTION_USE || sc == SDL_SCANCODE_SPACE) input.use = true;
  if (sc == SDL_SCANCODE_1) input.weapon = WEAPON_DAGGER;
  if (sc == SDL_SCANCODE_2) input.weapon = WEAPON_CROSSBOW;
  if (sc == SDL_SCANCODE_3) input.weapon = WEAPON_ARBALEST;
  if (sc == SDL_SCANCODE_M) {
    audio_set_enabled(!audio_enabled());
    if (game) game_say(game, audio_enabled() ? "SOUND ON" : "SOUND OFF", 1.2f);
  }
  if (sc == SDL_SCANCODE_F) toggle_fullscreen();
  if (action==ACTION_MAP && game && state == ST_PLAYING && game->horror.reading < 0) game->show_map = !game->show_map;
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
    if (state == ST_PLAYING) pause_play();
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
      if (game->campaign.journal) break;
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
  bool music_active=state==ST_PLAYING && input_focused && game && !game->player_dead &&
    !game->campaign.journal && game->horror.reading<0 && !game->show_map;
  float danger=0;
  if (music_active) {
    for (int i=0;i<game->enemy_count;i++) {
      const Enemy *e=&game->enemies[i];
      if (!enemy_dead(e) && e->state>=ES_CHASE && e->state<=ES_PAIN &&
          hypotf(e->x-game->player.x,e->y-game->player.y)<7 &&
          game_line_of_sight(game,e->x,e->y,game->player.x,game->player.y)) { combat_music_tail=5; break; }
    }
    combat_music_tail=maxf(0,combat_music_tail-dt);
    danger=maxf(game->horror.tension,combat_music_tail>0?.85f:0);
  }
  bool shelter=music_active && combat_music_tail<=0 && horror_in_haven(game,game->player.x,game->player.y);
  audio_set_scene(game?game->level_index:0,danger,shelter,music_active);
  if (story_state() && input_focused) {
    int previous = comic.panel;
    if (comic_update(&comic, dt)) finish_story();
    else if (comic.panel != previous) story_sound();
    return;
  }
  if (state == ST_LEVEL_START && state_timer > 3 && input_focused) begin_level();
  update_gamepad(dt);
  if (state != ST_PLAYING || !input_focused) return;
  Input frame = input;
  apply_gamepad(&frame);
  game->controller_input=gamepad && gamepad_active;
  if (game->controller_input) {
    snprintf(game->controller_use,sizeof game->controller_use,"%s",gamepad_button_name(SDL_GAMEPAD_BUTTON_SOUTH));
    snprintf(game->controller_decoy,sizeof game->controller_decoy,"%s",gamepad_button_name(SDL_GAMEPAD_BUTTON_WEST));
  }
  if (frame.use && safe_to_autosave()) {
    int station=campaign_near_station(game);
    if (station>=0 && game->level->campaign.stations[station].kind==MECHANISM_OATH && game->player.keys>=3 &&
        (game->campaign.done&game->level->campaign.stations[station].requires)==(uint32_t)game->level->campaign.stations[station].requires)
      checkpoint_game();
  }
  game_update(game, dt, &frame);
  if (!game->campaign.journal && game->horror.reading<0) autosave_game(dt);
  input.use = frame.use;
  input.weapon = frame.weapon;
  input.mouse_dx = frame.mouse_dx;

  if (game->player_dead) {
    death_delay += dt;
    if (death_delay > 1.4f) {
      death_delay = 0;
      if (session.arcade || !game->level->campaign.enabled) session.lives--;
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

static void menu_footer(const char *action, const char *back) {
  gfx_fill_rect(fb, 40, 357, 560, 1, rgba(180, 159, 119, .23f));
  if (gamepad && gamepad_active) {
    char hint[128];
    snprintf(hint, sizeof hint, "D-PAD  SELECT     %s  %s     %s  %s", gamepad_button_name(SDL_GAMEPAD_BUTTON_SOUTH),
             action, gamepad_button_name(SDL_GAMEPAD_BUTTON_EAST), back);
    text_align(hint, 42, 378, 9, "#b4b6b1", ALIGN_LEFT);
  } else {
    char hint[128];
    snprintf(hint, sizeof hint, "ARROWS / MOUSE  SELECT     ENTER / CLICK  %s     ESC  %s", action, back);
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
  menu_footer("CONFIRM", "QUIT");
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
    snprintf(buf, sizeof buf, "%d STARTING BOLTS  /  %s", (int)round_half_up(16 * settings->ammo),preferences_get()->arcade?"THREE LIVES":"STORY CHECKPOINTS");
    text_align(buf, 344, 274, 9, "#b5a07b", ALIGN_LEFT);
  } else {
    text("RETURN TO THE MAIN MENU", 462, 223, 13, "#d5c7ac");
  }
  text_align("Your difficulty stays with you for the whole escape.", 42, 331, 11, "#9ba6a8", ALIGN_LEFT);
  menu_footer("BEGIN", "BACK");
}

static void draw_controls(void) {
  menu_background();
  gfx_fill_rect(fb, 0, 0, W, H, rgba(3, 8, 13, .77f));
  title_text("CONTROLS", W / 2.0f, 70, 36, "#d8bd87");
  ui_panel(fb, 32, 95, 576, 242);
  char movement[80],fire[48],use_key[40],run_key[40],map_journal[80],tools[80];
  Preferences *p=preferences_get();
  snprintf(movement,sizeof movement,"%s %s %s %s / ARROWS",SDL_GetScancodeName(p->keys[ACTION_FORWARD]),SDL_GetScancodeName(p->keys[ACTION_LEFT]),SDL_GetScancodeName(p->keys[ACTION_BACK]),SDL_GetScancodeName(p->keys[ACTION_RIGHT]));
  snprintf(fire,sizeof fire,"CTRL / %s",SDL_GetScancodeName(p->keys[ACTION_FIRE]));
  snprintf(use_key,sizeof use_key,"%s / SPACE",SDL_GetScancodeName(p->keys[ACTION_USE]));
  snprintf(run_key,sizeof run_key,"%s",SDL_GetScancodeName(p->keys[ACTION_RUN]));
  snprintf(map_journal,sizeof map_journal,"%s / %s",SDL_GetScancodeName(p->keys[ACTION_MAP]),SDL_GetScancodeName(p->keys[ACTION_JOURNAL]));
  snprintf(tools,sizeof tools,"%s / %s",SDL_GetScancodeName(p->keys[ACTION_DECOY]),SDL_GetScancodeName(p->keys[ACTION_BRACE]));
  const char *keyboard_controls[][2] = {
      {movement, "move & turn"},
      {"MOUSE (CLICK VIEW)", "turn & shoot"},
      {fire, "shoot"},
      {use_key, "use, speak, read / close notes"},
      {"1 / 2 / 3", "dagger, crossbow, repeater"},
      {run_key, "run"},
      {map_journal, "automap / field journal"},
      {tools,"throw decoy / brace closed door"},
      {"F5 / F9", "save / load game"},
      {"ESC / F / M", "pause / fullscreen / sound"},
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
      {map, "automap"},
      {"X / SQUARE", "throw a noise decoy"},
      {"D-PAD LEFT / RIGHT", "brace door / field journal"},
      {"START / OPTIONS", "pause, save & load"},
  };
  const char *(*controls)[2] = gamepad && gamepad_active ? pad_controls : keyboard_controls;
  for (int i = 0; i < (int)(sizeof keyboard_controls / sizeof keyboard_controls[0]); i++) {
    float y = 117.0f + i * 23;
    if (i % 2 == 0) gfx_fill_rect(fb, 40, y - 17, 560, 22, rgba(102, 126, 141, .045f));
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
  title_text(game->level->name, W / 2.0f, 165, 32, "#e8dcc4");
  ui_rule(fb, 164, 187, 312);
  font_draw(fb, game->level->world.objective[0] ? game->level->world.objective : "Find three keys. Open the gate.", W / 2, 219, 12, FONT_REGULAR, ALIGN_CENTER,
            rgb(179, 190, 193));
  if (game->level->world.destination[0])
    font_draw(fb, game->level->world.destination, W / 2, 239, 10, FONT_REGULAR, ALIGN_CENTER, rgb(155, 170, 173));
  snprintf(buf, sizeof buf, "%s    /    %06d SCORE    /    %s", session.arcade?"ARCADE - THREE LIVES":"STORY - SAFE CHECKPOINTS", session.score,
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
  if (session.arcade) snprintf(lives, sizeof lives, "%d LIVES REMAIN", session.lives);
  else snprintf(lives,sizeof lives,"RETURN TO YOUR SAFE CHECKPOINT");
  text(lives, W / 2, 181, 12, "#a9b5b7");
  control_hint(session.arcade?"ENTER  /  TRY THIS CHAPTER AGAIN":"ENTER  /  RETURN TO SAFE SAVE",
    session.arcade?"%s  /  TRY THIS CHAPTER AGAIN":"%s  /  RETURN TO SAFE SAVE",224,12,"#d4bd96");
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
  snprintf(title, sizeof title, "CHAPTER %02d CLEARED", game->level_index + 1);
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
  control_hint(last ? "ENTER  /  CONTINUE" : "ENTER  /  THE NEXT CHAPTER",
               last ? "%s  /  CONTINUE" : "%s  /  THE NEXT CHAPTER", 337, 12, "#d8c098");
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
  snprintf(buf, sizeof buf, "%s  /  MAIN MENU",
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
  if (session.arcade) snprintf(lives, sizeof lives, "%d", session.lives);
  else snprintf(lives,sizeof lives,"STORY");
  snprintf(score, sizeof score, "%d", session.score);
  const char *rows[][2] = {{"TOTAL TIME", time},  {"KILLS", kills},      {"TREASURE", treasure},
                           {"SECRETS", secrets}, {session.arcade?"LIVES LEFT":"RULES", lives}, {"FINAL SCORE", score}};
  stat_rows(rows, 6, 150, 26, 13);

  control_hint("ENTER  /  MAIN MENU", "%s  /  MAIN MENU", 337, 12, "#d8c098");
}

static void options_card(const char *title) {
  menu_background();
  gfx_fill_rect(fb,0,0,W,H,rgba(2,7,12,.86f));
  ui_panel(fb,30,24,580,349);
  title_text(title,W/2,65,25,"#e8dcc4");
}
static void draw_options(void) {
  options_card("OPTIONS");
  const char *tabs[]={"DISPLAY","SOUND","GAMEPLAY","CONTROLS"};
  for (int i=0;i<4;i++) {
    if (options_page==i) gfx_fill_rect(fb,40+i*140,83,140,25,rgba(141,113,67,.28f));
    text(tabs[i],110+i*140,100,11,options_page==i?"#e4c78e":"#82969e");
  }
  Preferences *p=preferences_get();
  const char *labels[4][4]={{"BRIGHTNESS","WEAPON MOVEMENT","CAMERA SHAKE","READING TEXT"},
    {"SOUND EFFECTS","MUSIC"},{"CONTEXTUAL HINTS","AUTOMATIC SAVING","NEW GAME RULES"},
    {"MOUSE SENSITIVITY","KEYBOARD BINDINGS","RESET ALL OPTIONS"}};
  int numbers[4][4]={{p->brightness,p->bob,p->shake,p->text},{p->effects,p->music},{0,0,0},{p->sensitivity,0,0}};
  for (int i=0;i<option_rows();i++) {
    float y=141+i*36;
    if (i==options_row) { gfx_fill_rect(fb,62,y-22,516,32,rgba(139,116,75,.2f)); gfx_fill_rect(fb,62,y-22,2,32,rgb(193,165,112)); }
    font_draw(fb,labels[options_page][i],77,y,13,FONT_BOLD,ALIGN_LEFT,rgb(196,206,205));
    char value[64];
    if (options_page==2) snprintf(value,sizeof value,"%s",i==0?(p->tutorials?"ON":"OFF"):i==1?(p->autosave?"ON":"OFF"):(p->arcade?"ARCADE":"STORY"));
    else if (options_page==3 && i>0) snprintf(value,sizeof value,"%s",i==1?"CHANGE":"RESTORE");
    else snprintf(value,sizeof value,"<  %d%%  >",numbers[options_page][i]);
    text_align(value,557,y,13,"#d7bc86",ALIGN_RIGHT);
  }
  const char *desc=options_page==0?"Reading text affects notes and conversations. Preview:":
    options_page==1?"Music follows exploration, danger and the silence of refuges.":
    options_page==2?"Story: retry a safe save. Arcade: three lives and fresh equipment.":
    "Choose a binding, then press its new key. Conflicting bindings swap.";
  font_draw(fb,desc,W/2,300,10,FONT_REGULAR,ALIGN_CENTER,rgb(148,168,172));
  if (options_page==0) font_draw(fb,"The white candles remember your name.",W/2,328,13*p->text/100.0f,FONT_REGULAR,ALIGN_CENTER,rgb(218,208,182));
  font_draw(fb,"TAB / SHOULDERS: SECTION   UP / DOWN: SELECT   LEFT / RIGHT: CHANGE",W/2,350,8,FONT_BOLD,ALIGN_CENTER,rgb(173,185,186));
  font_draw(fb,"ESC / BACK - SAVE OPTIONS AND RETURN",W/2,366,9,FONT_BOLD,ALIGN_CENTER,rgb(209,188,146));
}
static void draw_bindings(void) {
  options_card("KEYBOARD BINDINGS");
  for (int i=0;i<ACTION_COUNT;i++) {
    float y=106+i*19;
    if (i==binding_row) gfx_fill_rect(fb,62,y-15,516,19,rgba(141,113,67,.25f));
    text_align(preferences_action_name((KeyAction)i),76,y,11,"#c0cccb",ALIGN_LEFT);
    const char *key=binding_wait && i==binding_row?"PRESS A KEY...":SDL_GetScancodeName((SDL_Scancode)preferences_get()->keys[i]);
    text_align(key,559,y,11,"#dcc18b",ALIGN_RIGHT);
  }
  font_draw(fb,"Arrows, Ctrl, Space, 1-3, F5/F9, F and M remain reserved shortcuts.",W/2,329,9,FONT_REGULAR,ALIGN_CENTER,rgb(144,165,171));
  font_draw(fb,"SELECT / CLICK A ROW TO REBIND    -    ESC / BACK TO RETURN",W/2,366,9,FONT_BOLD,ALIGN_CENTER,rgb(209,188,146));
}
static void draw_slots(void) {
  options_card("SAVED JOURNEYS");
  text("<  LOAD",225,101,12,slot_save_mode?"#82969e":"#e4c78e");
  text("SAVE  >",415,101,12,slot_save_mode?"#e4c78e":"#82969e");
  for (int i=0;i<5;i++) {
    float y=132+i*39;
    if (i==slot_row) { gfx_fill_rect(fb,62,y-20,516,37,rgba(139,116,75,.23f)); gfx_fill_rect(fb,62,y-20,2,37,rgb(193,165,112)); }
    char title[64]; snprintf(title,sizeof title,i<3?"MANUAL SLOT %d":i==3?"AUTOSAVE":"CHAPTER CHECKPOINT",i+1);
    text_align(title,76,y,11,"#d9c18f",ALIGN_LEFT);
    if (slot_valid[i]) {
      SaveInfo *info=&slot_info[i]; char details[80];
      snprintf(details,sizeof details,"%s - %d HP / %d BOLTS",game_difficulty_settings(info->difficulty)->name,info->health,info->ammo);
      text_align(levels[info->level].name,558,y-2,10,"#c1cece",ALIGN_RIGHT);
      text_align(details,558,y+11,8,"#8aa0a7",ALIGN_RIGHT);
    } else text_align("EMPTY OR INCOMPATIBLE",558,y+4,9,"#81939b",ALIGN_RIGHT);
  }
  font_draw(fb,"F5 SAVES TO THE SELECTED MANUAL SLOT. F9 LOADS IT.",W/2,335,9,FONT_REGULAR,ALIGN_CENTER,rgb(148,168,172));
  font_draw(fb,"LEFT / RIGHT: LOAD OR SAVE    ENTER: SELECT    ESC / BACK: RETURN",W/2,366,8,FONT_BOLD,ALIGN_CENTER,rgb(209,188,146));
}
static void render(void) {
  switch (state) {
  case ST_LOADING: draw_center_screen("#101018", "LOADING..."); break;
  case ST_ERROR: draw_error(); break;
  case ST_STORY_INTRO:
  case ST_STORY_OUTRO: draw_story(); break;
  case ST_MENU: draw_main_menu(); break;
  case ST_DIFFICULTY: draw_difficulty(); break;
  case ST_CONTROLS: draw_controls(); break;
  case ST_OPTIONS: draw_options(); break;
  case ST_BINDINGS: draw_bindings(); break;
  case ST_SLOTS: draw_slots(); break;
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
// debug: --level N skips the main menu and starts on that level
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
  char *options_path=user_file("options.cfg");
  if (options_path) preferences_load(options_path);
  free(options_path);
  audio_set_mix(preferences_get()->effects,preferences_get()->music);
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
      set_state(ST_MENU);
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
  for (int i=0;i<2;i++) free(manual_paths[i]);
  free(automatic_path); free(checkpoint_path);
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
