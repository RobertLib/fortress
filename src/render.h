// Raycasting renderer: walls via DDA (with sliding doors), billboard
// sprites clipped against a z-buffer, POV weapon, HUD and automap.
// Torchlit atmosphere: everything fades into darkness with distance.

#ifndef FORTRESS_RENDER_H
#define FORTRESS_RENDER_H

#include "assets.h"
#include "game.h"
#include "gfx.h"

#define MAX_OVERLAYS 16

// A see-through surface (bars) a ray passed on its way to the opaque wall.
typedef struct Overlay {
  float dist;
  int side;
  int tex;
  float wall_x;
} Overlay;

typedef struct SurfaceTint {
  float k, r, g, b;
} SurfaceTint;

typedef struct Renderer {
  const Assets *assets;
  float zbuffer[SCREEN_W];
  Overlay overlays[SCREEN_W][MAX_OVERLAYS];
  int overlay_count[SCREEN_W];
  Image *vignette;
  SurfaceTint *plane_tints; // smoothly interpolated light at map-grid corners
  size_t plane_tint_count;
  uint32_t *shake_buf;
} Renderer;

void renderer_init(Renderer *r, const Assets *assets);
void renderer_free(Renderer *r);

void render_view(Renderer *r, Image *fb, const Game *g);
void render_map(Renderer *r, Image *fb, const Game *g, const char *close_button); // NULL defaults to TAB
void render_hud(Renderer *r, Image *fb, const Game *g);

#endif
