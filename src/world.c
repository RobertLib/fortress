#include "world.h"
#include "level.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static bool inside(const Level *lv, int x, int y) { return x >= 0 && y >= 0 && x < lv->w && y < lv->h; }
bool world_outside(const Level *lv, int x, int y) {
  return lv->world.cells && inside(lv, x, y) && lv->world.cells[y * lv->w + x].outside;
}
GroundKind world_ground(const Level *lv, int x, int y) {
  return lv->world.cells && inside(lv, x, y) ? lv->world.cells[y * lv->w + x].ground : GROUND_STONE;
}
bool world_reserved(const Level *lv, int x, int y) {
  if (campaign_reserved(lv,x,y)) return true;
  if (world_outside(lv, x, y)) return true;
  if (lv->world.cells && inside(lv,x,y) && lv->world.cells[y*lv->w+x].authored) return true;
  for (int i = 0; i < lv->world.prop_count; i++)
    if (abs(x - lv->world.props[i].x) <= 1 && abs(y - lv->world.props[i].y) <= 1) return true;
  return false;
}
const Monk *world_monk_note(const Level *lv, int note) {
  for (int i = 0; i < lv->world.monk_count; i++) if (lv->world.monks[i].note == note) return &lv->world.monks[i];
  return NULL;
}
const Landmark *world_landmark(const Level *lv, float x, float y) {
  const Landmark *best = NULL;
  float distance = INFINITY;
  for (int i = 0; i < lv->world.landmark_count; i++) {
    const Landmark *l = &lv->world.landmarks[i];
    float d = hypotf(x - l->x - .5f, y - l->y - .5f);
    if (d < l->radius && d < distance) { best = l; distance = d; }
  }
  return best;
}
float world_prop_radius(DecoKind kind) {
  switch (kind) {
  case DECO_OAK: return .21f;
  case DECO_PINE: return .19f;
  case DECO_WELL: return .38f;
  case DECO_GRAVE: return .22f;
  case DECO_CROSS: return .20f;
  case DECO_BENCH: return .28f;
  case DECO_ALTAR: return .32f;
  case DECO_MONK: return .18f;
  case DECO_TABLE: return .36f;
  case DECO_CHAIR: return .18f;
  case DECO_CANDELABRA: return .12f;
  case DECO_STATUE: return .24f;
  case DECO_URN: return .20f;
  default: return 0;
  }
}
float world_prop_scale(DecoKind kind) {
  switch (kind) {
  case DECO_OAK: return 2.4f;
  case DECO_PINE: return 2.7f;
  case DECO_WELL: return 1.1f;
  case DECO_GRAVE: return .62f;
  case DECO_CROSS: return 1.35f;
  case DECO_BENCH: return .60f;
  case DECO_ALTAR: return .9f;
  case DECO_MONK: return .84f;
  default: return .64f;
  }
}
static bool printable(const char *text, size_t capacity) {
  if (!*text || strlen(text) >= capacity) return false;
  for (; *text; text++) if ((unsigned char)*text < 32 || (unsigned char)*text > 126) return false;
  return true;
}
static bool dry(const Level *lv, int x, int y) { return inside(lv, x, y) && lv->grid[y * lv->w + x] == '.'; }

bool world_configure(Level *lv, const char *text, char *err, size_t size) {
  WorldLayout *w = &lv->world;
  char context[96] = {0};
  for (const char *p = text; *p;) {
    const char *end = strchr(p, '\n');
    size_t n = end ? (size_t)(end - p) : strlen(p);
    // Only own these directives. Legacy and horror parsers own their limits.
    const char *names[] = {"@outside", "@terrain", "@prop", "@monk", "@landmark", "@objective", "@destination", "@roof", "@facade"};
    int directive = -1;
    for (int i = 0; i < 9; i++) {
      size_t len = strlen(names[i]);
      if (n >= len && !strncmp(p, names[i], len) && (n == len || isspace((unsigned char)p[len]))) directive = i;
    }
    if (directive >= 0) {
      char line[512], extra;
      snprintf(context, sizeof context, "%.90s", names[directive]);
      if (n >= sizeof line) goto bad;
      memcpy(line, p, n); line[n] = 0;
      while (n && isspace((unsigned char)line[n - 1])) line[--n] = 0;
      const char *value = line + strlen(names[directive]);
      while (isspace((unsigned char)*value)) value++;
      if (!w->cells) w->cells = calloc((size_t)lv->w * lv->h, sizeof *w->cells);
      if (!w->cells) goto bad;
      int x, y, rw, rh, used = 0;
      if (directive <= 1) {
        char kind[24]; int ground = GROUND_GRASS;
        if (directive == 1) {
          if (sscanf(value, "%23s %n", kind, &used) != 1 || !used) goto bad;
          const char *grounds[] = {"stone", "grass", "earth", "cobble", "gravel", "leaves"};
          ground = 0;
          while (ground < GROUND_COUNT && strcmp(kind, grounds[ground])) ground++;
          if (ground == GROUND_COUNT) goto bad;
        }
        if (sscanf(value + used, "%d %d %d %d %c", &x, &y, &rw, &rh, &extra) != 4 ||
            !inside(lv, x, y) || rw <= 0 || rh <= 0 || rw > lv->w - x || rh > lv->h - y) goto bad;
        for (int yy = y; yy < y + rh; yy++) for (int xx = x; xx < x + rw; xx++) {
          WorldCell *c = &w->cells[yy * lv->w + xx];
          if (directive == 0) c->outside = 1;
          c->authored = 1;
          c->ground = (uint8_t)ground;
        }
      } else if (directive == 2 || directive == 3) {
        if (w->prop_count == WORLD_PROPS) goto bad;
        WorldProp prop = {.kind = DECO_MONK};
        if (directive == 2) {
          char kind[24];
          if (sscanf(value, "%23s %d %d %c", kind, &x, &y, &extra) != 3) goto bad;
          const char *props[] = {"oak", "pine", "well", "grave", "cross", "bench", "altar", "table", "chair", "candelabra", "statue", "urn"};
          const DecoKind kinds[] = {DECO_OAK,DECO_PINE,DECO_WELL,DECO_GRAVE,DECO_CROSS,DECO_BENCH,DECO_ALTAR,
                                    DECO_TABLE,DECO_CHAIR,DECO_CANDELABRA,DECO_STATUE,DECO_URN};
          int k = 0;
          while (k < 12 && strcmp(kind, props[k])) k++;
          if (k == 12) goto bad;
          prop.kind = kinds[k];
        } else {
          if (w->monk_count == WORLD_MONKS) goto bad;
          if (sscanf(value, "%d %d %n", &x, &y, &used) != 2 || !used) goto bad;
          char *name = (char *)value + used, *sep = strchr(name, '|');
          if (!sep) goto bad;
          *sep++ = 0;
          if (!printable(name, sizeof w->monks[0].name) || !printable(sep, sizeof lv->horror.notes[0].text)) goto bad;
          Monk *m = &w->monks[w->monk_count++];
          *m = (Monk){.x = x, .y = y, .note = -1};
          snprintf(m->name, sizeof m->name, "%s", name);
          snprintf(m->text, sizeof m->text, "%s", sep);
        }
        if (!dry(lv, x, y) || x <= 0 || y <= 0 || x >= lv->w - 1 || y >= lv->h - 1) goto bad;
        for (int i = 0; i < w->prop_count; i++) if (w->props[i].x == x && w->props[i].y == y) goto bad;
        for (int i = 0; i < lv->item_count; i++) if ((int)lv->items[i].x == x && (int)lv->items[i].y == y) goto bad;
        for (int i = 0; i < lv->enemy_count; i++) if ((int)lv->enemies[i].x == x && (int)lv->enemies[i].y == y) goto bad;
        for (int i = 0; i < lv->barrel_count; i++) if ((int)lv->barrels[i].x == x && (int)lv->barrels[i].y == y) goto bad;
        if ((int)lv->start_x == x && (int)lv->start_y == y) goto bad;
        prop.x = x; prop.y = y; w->props[w->prop_count++] = prop;
      } else if (directive == 4) {
        if (w->landmark_count == WORLD_LANDMARKS) goto bad;
        Landmark *l = &w->landmarks[w->landmark_count++];
        if (sscanf(value, "%d %d %f %n", &l->x, &l->y, &l->radius, &used) != 3 || !used ||
            !dry(lv, l->x, l->y) || !isfinite(l->radius) || l->radius < 1 || l->radius > 32 ||
            !printable(value + used, sizeof l->name)) goto bad;
        snprintf(l->name, sizeof l->name, "%s", value + used);
      } else if (directive==8) {
        if (w->facade_count==WORLD_FACADES) goto bad;
        WorldFacade *f=&w->facades[w->facade_count++]; char kind[20];
        if (sscanf(value,"%d %d %d %d %19s %c",&f->x,&f->y,&f->dx,&f->dy,kind,&extra)!=5 ||
            !inside(lv,f->x,f->y) || abs(f->dx)+abs(f->dy)!=1 ||
            !dry(lv,f->x+f->dx,f->y+f->dy) || !strchr("123456",lv->grid[f->y*lv->w+f->x])) goto bad;
        if (!strcmp(kind,"glass")) f->kind=HANG_GLASS_WINDOW;
        else if (!strcmp(kind,"shutter")) f->kind=HANG_SHUTTER_WINDOW;
        else if (!strcmp(kind,"crest")) f->kind=HANG_SHIELD;
        else goto bad;
      } else if (directive==7) {
        if (w->roof_count==WORLD_ROOFS) goto bad;
        WorldRoof *roof=&w->roofs[w->roof_count++];
        if (sscanf(value,"%d %d %d %d %f %f %d %d %c",&roof->x,&roof->y,&roof->w,&roof->h,
            &roof->eave,&roof->ridge,&roof->axis,&roof->slate,&extra)!=8 ||
            !inside(lv,roof->x,roof->y) || roof->w<3 || roof->h<3 || roof->w>lv->w-roof->x || roof->h>lv->h-roof->y ||
            !isfinite(roof->eave) || !isfinite(roof->ridge) || roof->eave<.5f || roof->ridge<=roof->eave || roof->ridge>5 ||
            roof->axis<0 || roof->axis>1 || roof->slate<0 || roof->slate>1) goto bad;
      } else {
        char *dest = directive == 5 ? w->objective : w->destination;
        size_t capacity = directive == 5 ? sizeof w->objective : sizeof w->destination;
        if (!printable(value, capacity)) goto bad;
        snprintf(dest, capacity, "%s", value);
      }
    }
    if (!end) break;
    p = end + 1;
  }
  return true;
bad:
  snprintf(err, size, "Invalid world directive: %s", context);
  return false;
}
