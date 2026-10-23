#ifndef FORTRESS_ART_H
#define FORTRESS_ART_H
#include "assets.h"

// Art coordinates are expressed on a 64-unit canvas and rasterised at TEX.
void art_rect(Image *im, float x, float y, float w, float h, Color color);
void art_line(Image *im, float x, float y, float xx, float yy, float width, Color color);
void art_poly(Image *im, const float *points, int count, Color color);
void art_oval(Image *im, float x, float y, float rx, float ry, float width, Color color);
Image *art_cell(const Image *atlas, int columns, int index);
Image *art_motion(const Image *source, int frame, int kind);
uint32_t art_sample(const Image *source, float x, float y);
void materials_build(Assets *assets, const Image *limestone, const Image *atlas, const Image *props);

#endif
