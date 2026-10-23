#ifndef FORTRESS_UI_H
#define FORTRESS_UI_H

#include "gfx.h"

// Shared dark iron / antique brass visual language, in logical screen pixels.
void ui_diamond(Image *im, float x, float y, float radius, Color color);
void ui_rule(Image *im, float x, float y, float width);
void ui_panel(Image *im, float x, float y, float width, float height);
void ui_emblem(Image *im, float x, float y, float size);

#endif
