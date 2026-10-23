// Procedural sound effects. Every effect is synthesized once at startup
// into a sample buffer; playing a sound only starts a cheap mixer voice.

#ifndef FORTRESS_AUDIO_H
#define FORTRESS_AUDIO_H

#include <stdbool.h>

typedef enum {
  // weapons
  SND_CROSSBOW,
  SND_ARBALEST,
  SND_DAGGER,
  SND_EMPTY,
  SND_ENEMY_SHOT,
  // traps
  SND_TRAP_CLICK,
  SND_PIT_FALL,
  SND_PENDULUM,
  SND_ARROW_HIT,
  SND_EXPLOSION,
  SND_WALL_HIT,
  SND_WALL_BREAK,
  // player
  SND_PLAYER_PAIN,
  SND_PLAYER_DEATH,
  SND_STEP,
  SND_VENOM,
  // enemies
  SND_ALERT,
  SND_KNIFE_SLASH,
  SND_PUNCH,
  SND_ENEMY_PAIN,
  SND_ENEMY_DEATH,
  // bats
  SND_BAT_ALERT,
  SND_BAT_SQUEAK,
  SND_BAT_BITE,
  SND_BAT_DEATH,
  SND_FLUTTER,
  // dogs
  SND_DOG_BARK,
  SND_DOG_GROWL,
  SND_DOG_BITE,
  SND_DOG_PAIN,
  SND_DOG_DEATH,
  // rats
  SND_RAT_ALERT,
  SND_RAT_SQUEAK,
  SND_RAT_SKITTER,
  SND_RAT_BITE,
  SND_RAT_DEATH,
  // spiders
  SND_SPIDER_HISS,
  SND_SPIDER_CHITTER,
  SND_SPIDER_SKITTER,
  SND_SPIDER_BITE,
  SND_SPIDER_PAIN,
  SND_SPIDER_DEATH,
  // pickups
  SND_PICKUP,
  SND_PICKUP_AMMO,
  SND_PICKUP_HEALTH,
  SND_PICKUP_WEAPON,
  SND_PICKUP_KEY,
  SND_EXTRA_LIFE,
  // doors / exit
  SND_DOOR,
  SND_LEVER,
  SND_SECRET,
  SND_PORTAL,
  SND_LOCKED,
  SND_EXIT_OPEN,
  // jingles
  SND_LEVEL_DONE,
  SND_GAME_OVER,
  SND_VICTORY,
  SND_BLIP,
  SND_START,
  SND_SPLASH,
  SND_WATER_FLOW,
  SND_HORROR_WIND,
  SND_HORROR_KNOCK,
  SND_HORROR_SCRAPE,
  SND_HORROR_STEPS,
  SND_HORROR_BREATH,
  SND_HORROR_BELL,
  SND_WARDEN_STEP,
  SND_WARDEN_CHAIN,
  SND_PARCHMENT,
  SND_COUNT
} SoundId;

// Where the player stands and faces, for positional sounds.
typedef struct Listener {
  float x, y, dir_x, dir_y;
} Listener;

// Number of sound-blocking walls between the listener (x0, y0) and a source
// (x1, y1); 0 means an open line.
typedef float (*OcclusionFn)(void *ctx, float x0, float y0, float x1, float y1);

// Opens the playback device and synthesizes every effect. Returns false
// (and the game simply stays silent) when no audio device is available.
bool audio_init(void);
void audio_shutdown(void);

void audio_set_enabled(bool enabled);
bool audio_enabled(void);

// Positional sounds use this to muffle sources behind walls; NULL disables it.
void audio_set_occlusion(OcclusionFn fn, void *ctx);

// Plain playback: volume and playback rate (1 = normal pitch).
void audio_play(SoundId id, float volume, float rate);
// pan in -1..1 (0 = centered), cutoff > 0 routes the voice through a
// low-pass filter at that frequency in Hz.
void audio_play_ex(SoundId id, float volume, float rate, float pan, float cutoff);
// World sound at (x, y): attenuated by distance, muffled by occluding walls,
// panned by where it lies relative to the listener's facing.
void audio_play_at(SoundId id, float x, float y, const Listener *listener, float volume, float rate);

#endif
