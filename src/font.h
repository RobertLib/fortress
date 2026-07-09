// Procedural serif stroke font: glyphs are described as pen strokes,
// rasterised with anti-aliasing on first use and cached per size.

#ifndef FORTRESS_FONT_H
#define FORTRESS_FONT_H

#include "gfx.h"

typedef enum { FONT_REGULAR, FONT_BOLD } FontWeight;
typedef enum { ALIGN_LEFT, ALIGN_CENTER, ALIGN_RIGHT } TextAlign;

// Width in pixels of a UTF-8 string at the given size (em size in pixels).
float font_measure(const char *text, float size, FontWeight weight);

// Draws a UTF-8 string with its alphabetic baseline at y; x is the left
// edge, the center or the right edge depending on align.
void font_draw(Image *dst, const char *text, float x, float y, float size, FontWeight weight, TextAlign align,
               Color color);

void font_shutdown(void); // frees the glyph cache

#endif
