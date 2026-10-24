// Authored objectives, discoveries and tactical tools. Custom maps opt in.
#ifndef FORTRESS_CAMPAIGN_H
#define FORTRESS_CAMPAIGN_H
#include "common.h"
#include <stddef.h>

#define CAMPAIGN_STATIONS 8
#define CAMPAIGN_RELICS 5
#define CAMPAIGN_BRACES 4
typedef enum { MECHANISM_WINCH, MECHANISM_VALVE, MECHANISM_BELL, MECHANISM_SEAL,
               MECHANISM_GRAVE, MECHANISM_OATH, MECHANISM_COUNT } MechanismKind;
typedef enum { RELIC_QUIET, RELIC_STEADY, RELIC_MERCY, RELIC_VIGIL, RELIC_WARD } RelicKind;
typedef struct Mechanism {
  int x, y, requires;
  MechanismKind kind;
  float duration;
  char name[48], hint[96];
} Mechanism;
typedef struct RelicSpawn { int x, y; RelicKind kind; } RelicSpawn;
typedef struct Errand {
  bool present;
  int x, y, monk;
  char name[48], request[224], thanks[224];
} Errand;
typedef struct CampaignLayout {
  bool enabled;
  uint32_t signature;
  Mechanism stations[CAMPAIGN_STATIONS];
  int count;
  RelicSpawn relics[CAMPAIGN_RELICS];
  int relic_count;
  Errand errand;
  bool has_errand_door;
  int errand_door_x,errand_door_y;
  int drain_x, drain_y, drain_w, drain_h, drain_requires;
} CampaignLayout;
typedef struct DoorBrace { int door; float remaining; } DoorBrace;
typedef struct CampaignState {
  uint32_t done, relics_taken, lessons;
  int active;
  float work;
  bool errand_found, errand_done, finale;
  int decoys, braces;
  DoorBrace brace[CAMPAIGN_BRACES];
  float decoy_x, decoy_y, decoy_timer, decoy_pulse;
  float lesson_timer, objective_flash;
  bool journal, checkpoint_requested; // checkpoint request is transient
  int journal_page;
} CampaignState;
struct Level;
struct Game;
bool campaign_configure(struct Level *lv, const char *text, char *err, size_t size);
bool campaign_reserved(const struct Level *lv, int x, int y);
void campaign_init(struct Game *g);
void campaign_update(struct Game *g, float dt);
int campaign_near_station(const struct Game *g);
bool campaign_use(struct Game *g);
bool campaign_gate_ready(struct Game *g);
bool campaign_drained(const struct Game *g, int x, int y);
bool campaign_door_braced(const struct Game *g, int door);
void campaign_throw_decoy(struct Game *g);
void campaign_brace_door(struct Game *g);
void campaign_converse(struct Game *g, int note);
int campaign_dialogue_pages(const struct Game *g, int note);
const char *campaign_dialogue(const struct Game *g, int note, int page);
const char *campaign_objective(const struct Game *g);
const char *campaign_relic_name(RelicKind kind);
bool campaign_has_relic(const struct Game *g, RelicKind kind);
#endif
