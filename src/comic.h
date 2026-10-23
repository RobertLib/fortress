// Procedural, animated comic pages for the beginning and end of a run.
#ifndef FORTRESS_COMIC_H
#define FORTRESS_COMIC_H

#include "gfx.h"

#define COMIC_PANELS 3

typedef enum { COMIC_INTRO, COMIC_OUTRO, COMIC_KIND_COUNT } ComicKind;

typedef struct Comic {
  ComicKind kind;
  int panel;
  float age[COMIC_PANELS]; // time since each panel was revealed
} Comic;

typedef struct ComicArt {
  Image *scene[COMIC_KIND_COUNT][COMIC_PANELS];
  Image *scratch;
  Image *cover;
} ComicArt;

bool comic_art_build(ComicArt *art);
void comic_art_free(ComicArt *art);
void comic_start(Comic *comic, ComicKind kind);
// Return true after the final panel. The caller chooses the next game state.
bool comic_next(Comic *comic);
bool comic_update(Comic *comic, float dt);
void comic_draw(ComicArt *art, Image *fb, const Comic *comic);
void comic_draw_cover(const ComicArt *art, Image *fb, float time);

#endif
