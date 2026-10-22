// One versioned save slot. File paths are supplied by the UI so tests can use
// an isolated directory. A failed load leaves the current run untouched.
#ifndef FORTRESS_SAVEGAME_H
#define FORTRESS_SAVEGAME_H

#include "game.h"

bool savegame_write(const char *path, Game *game, char *err, size_t err_size);
bool savegame_load(const char *path, const Level *levels, int level_count,
                   Game **current, Session *session, char *err, size_t err_size);

#endif
