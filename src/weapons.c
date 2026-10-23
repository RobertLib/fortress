// Standalone access to the painted viewmodels, also used by art tooling.
#include "assets.h"
#include <stddef.h>
Image *weapon_view_build(WeaponKind kind, int frame) {
  Image *atlas = image_load_art("weapons.bmp");
  if (!atlas) return NULL;
  Image *im = image_new(WEAPON_VIEW, WEAPON_VIEW);
  gfx_blit(im, atlas, clampi(frame, 0, 2) * TEX, (int)kind * TEX, TEX, TEX, 0, 0, WEAPON_VIEW, WEAPON_VIEW, 1);
  image_free(atlas);
  return im;
}
