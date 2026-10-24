// Authored hauntings and a persistent, door-aware pursuer. No external assets.
#ifndef FORTRESS_HORROR_H
#define FORTRESS_HORROR_H

#include "common.h"
#include <stddef.h>

#define HORROR_SCENES 16
#define HORROR_NOTES 12
#define HORROR_HAVENS 4
#define HORROR_TRACES 24
#define WARDEN_RADIUS .24f
#define WARDEN_WARNING_TIME 4.5f
#define WARDEN_STAGGER_TIME 2.4f
#define WARDEN_DAGGER_STAGGER .75f

typedef enum { HAUNT_CELL, HAUNT_SHADOW, HAUNT_KNOCK, HAUNT_ARMOR, HAUNT_MIRROR } HauntKind;
typedef struct Haunt {
  HauntKind kind;
  int tx, ty, sx, sy, keys, dx, dy;
} Haunt;
typedef struct HorrorNote { int x, y; char text[224]; } HorrorNote;
typedef struct Haven { int x, y; float radius; } Haven;
typedef enum { TRACE_BLOOD, TRACE_SCRATCH, TRACE_CHAINS } TraceKind;
typedef struct HorrorTrace { int x, y; TraceKind kind; } HorrorTrace;
typedef struct HorrorLayout {
  int chapter;
  Haunt scenes[HORROR_SCENES];
  int scene_count;
  HorrorNote notes[HORROR_NOTES];
  int note_count;
  Haven havens[HORROR_HAVENS];
  int haven_count;
  HorrorTrace traces[HORROR_TRACES];
  int trace_count;
} HorrorLayout;

typedef enum { WARDEN_DORMANT, WARDEN_WARNING, WARDEN_HUNT, WARDEN_STAGGER, WARDEN_FADING } WardenState;
typedef struct Warden {
  WardenState state;
  float x, y, target_x, target_y;
  float state_timer, remaining, lost_timer, attack_timer, step_timer, path_timer;
  float next_x, next_y;
  bool has_target;
  float stuck_timer; // transient movement watchdog; reset after loading
} Warden;
typedef struct HorrorState {
  uint32_t scenes_fired, scenes_changed, notes_read;
  float scene_timer[HORROR_SCENES];
  int keys_seen, pending_hunts, ambient_cycle, reading;
  float key_x[3], key_y[3];
  int opened_door[3];
  float blackout, ambient_timer, calm_timer, tension;
  float noise_x, noise_y, noise_timer;
  float noise_radius;
  bool was_safe;
  Warden warden;
  // Transient BFS scratch, never serialized. Each game owns its own buffers.
  int *distance, *queue;
} HorrorState;

struct Level;
struct Game;
bool horror_configure(struct Level *lv, const char *text, char *err, size_t size);
void horror_init(struct Game *g);
void horror_free(struct Game *g);
void horror_update(struct Game *g, float dt);
bool horror_in_haven(const struct Game *g, float x, float y);
bool horror_holds_door(const struct Game *g, int index);
float horror_torch_strength(const struct Game *g, float x, float y);
bool horror_use_note(struct Game *g);
int horror_near_note(const struct Game *g);
void horror_noise(struct Game *g, float x, float y, float seconds);
float horror_warden_aim(const struct Game *g, float range);
bool horror_hit_warden(struct Game *g, float stagger);
bool horror_warden_solid(const struct Game *g);
bool horror_warden_targetable(const struct Game *g);
bool horror_scene_visible(const struct Game *g, int index, float *x, float *y, float *alpha);

#endif
