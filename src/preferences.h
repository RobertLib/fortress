#ifndef FORTRESS_PREFERENCES_H
#define FORTRESS_PREFERENCES_H
#include <stdbool.h>
typedef enum { ACTION_FORWARD,ACTION_BACK,ACTION_LEFT,ACTION_RIGHT,ACTION_FIRE,ACTION_USE,
  ACTION_RUN,ACTION_MAP,ACTION_JOURNAL,ACTION_DECOY,ACTION_BRACE,ACTION_COUNT } KeyAction;
typedef struct Preferences {
  int sensitivity,brightness,bob,shake,text,effects,music;
  bool tutorials,autosave,arcade,sound,fullscreen;
  int keys[ACTION_COUNT];
} Preferences;
Preferences *preferences_get(void);
void preferences_defaults(void);
bool preferences_load(const char *path);
bool preferences_save(const char *path);
bool preferences_bind(KeyAction action,int scancode);
const char *preferences_action_name(KeyAction action);
int preferences_action_for_key(int scancode);
#endif
