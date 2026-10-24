// Text-file level format:
//   Lines starting with ";" are comments.
//   Lines starting with "@" are properties: @name, @floor, @ceil (colors),
//   @surfaces (material theme),
//   @height / @ceiling x y width height elevation (absolute tile units),
//   @start (initial facing: N/S/E/W, default E).
//   Everything else is the map grid, one character per cell:
//     1-6    walls (texture variants), # = 1
//     8      cracked, breakable masonry (interior walls only)
//     B      barred wall (solid, but can be seen and shot through)
//     .      floor
//     D      door (slides open)
//     R      barred door (slides open, can be seen and shot through)
//     X      exit door (needs all 3 keys)
//     P      player start
//     K      key            H healing potion (+25)   F bread (+10)
//     A      quiver of bolts   T treasure (score)    M repeating crossbow
//     Q/q    gunpowder barrel / ordinary barrel
//     G/g    guard (patrol/standing)
//     S/s    knight         O/o captain
//     V/v    bat swarm (flying about / roosting until disturbed)
//     C/c    war hound (roaming/staying put). A hound with a soldier within
//            4 cells is leashed to him instead: it heels to its handler,
//            barks him awake when it spots the player, and returns to his
//            side when it loses the chase.
//     Y/y    rat (scurrying about/lurking). Rats slip between the other
//            actors' legs; one that spots the player rouses the rest of its
//            pack, but they can't open doors.
//     W/w    spider (prowling/lurking under the ceiling). A lurker drops on
//            its silk thread when the player comes close and climbs back up
//            once it loses him. Spiders are venomous and can't open doors.
//     Wall torches are placed automatically on suitable stretches of masonry.
//     Tables and chairs are likewise furnished automatically in larger rooms,
//     and wardrobes and chests are set flush against flat wall stretches.
//     A fountain plays in the widest court, wherever the floor opens up
//     enough to hold its basin.
//     Iron chains are draped on the masonry and hung from the ceiling in
//     the gloomier corners, purely for atmosphere.
//     Cobwebs gather in the upper corners of walls the torchlight barely
//     reaches — some still keep their spider.
//     Portraits of the fortress's old masters hang on the cleaner walls;
//     their painted eyes follow the player through the room.
//     Niches are carved into quiet stretches of wall — the masonry recedes
//     to half depth and keeps a stone figure, an old urn or a cluster of
//     votive candles on display.
//     Standing candelabra light the stretches the torches never reach,
//     their flames flickering with a soft glow of their own.
//     A fireplace blazes against plain masonry in one of the larger halls
//     (two on the biggest floors) — a stone surround set flat into the
//     wall with logs alight in the grate, lighting the room like an
//     oversized torch.
//     ^      arrow trap: a pressure plate on the floor; the nearest wall in
//            a straight line becomes an arrow slit (texture 7) and looses a
//            volley of arrows when the plate is stepped on
//     !      open pit: a lethal hole with a narrow stone rim
//     =      shallow pool; >/< flowing east/west, }/{ south/north
//     &/%    swinging ceiling blade, east-west / north-south
//     Z      secret wall: looks like the surrounding masonry, slides open
//            when the nearest lever is pulled and then stays open
//     L      lever on a wall: press use on it to open its secret wall(s).
//            The lever shows on exactly one face of the cell — one that
//            looks out on floor the player can reach without any secrets,
//            so it can never end up buried or sealed away; the other faces
//            wear the surrounding masonry.
//     ~      mirror portal: a tall glass of swirling light set into the
//            wall. Portals pair up in reading order (1st with 2nd, 3rd with
//            4th…); step into the glass and its twin draws you out on the
//            far side. Only the player fits through — to enemies and bolts
//            the mirror is solid glass.
//   After parsing, carved niches appear in the grid as "N".

#include "level.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const int CARDINAL[4][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};

static bool in_set(const char *set, char c) { return c && strchr(set, c) != NULL; }

bool level_is_wall(char c) { return in_set("12345678B#", c); }
bool level_is_water(char c) { return in_set("=><}{", c); }
bool level_is_open(char c) { return c == '.' || c == '!' || level_is_water(c); }

float level_floor_at(const Level *lv, int x, int y) {
  return lv->heights && x >= 0 && y >= 0 && x < lv->w && y < lv->h ? lv->heights[y * lv->w + x] : 0;
}

float level_floor(const Level *lv, float x, float y) {
  return level_floor_at(lv, (int)floorf(x), (int)floorf(y));
}

float level_ceiling_at(const Level *lv, int x, int y) {
  if (world_outside(lv, x, y)) return LEVEL_MAX_HEIGHT + 1;
  if (lv->ceilings && x >= 0 && y >= 0 && x < lv->w && y < lv->h) {
    float z = lv->ceilings[y * lv->w + x];
    if (isfinite(z)) return z;
  }
  return level_floor_at(lv, x, y) + 1;
}

float level_ceiling(const Level *lv, float x, float y) {
  return level_ceiling_at(lv, (int)floorf(x), (int)floorf(y));
}

void level_water_flow(char c, int *dx, int *dy) {
  *dx = (c == '>') - (c == '<');
  *dy = (c == '}') - (c == '{');
}

static int item_kind_for(char ch) {
  switch (ch) {
  case 'K': return ITEM_KEY;
  case 'H': return ITEM_POTION;
  case 'F': return ITEM_BREAD;
  case 'A': return ITEM_BOLTS;
  case 'T': return ITEM_TREASURE;
  case 'M': return ITEM_ARBALEST;
  default: return -1;
  }
}

static int enemy_type_for(char ch) {
  switch (ch) {
  case 'G': return ENEMY_GUARD;
  case 'S': return ENEMY_KNIGHT;
  case 'O': return ENEMY_CAPTAIN;
  case 'V': return ENEMY_BAT;
  case 'C': return ENEMY_DOG;
  case 'Y': return ENEMY_RAT;
  case 'W': return ENEMY_SPIDER;
  default: return -1;
  }
}

// multiplicative hashes of cell coordinates keep every layout stable
static inline uint32_t imul(uint32_t a, uint32_t b) { return a * b; }

static double round_limit(int lo, int hi, double v) {
  double r = round_half_up(v);
  return r < lo ? lo : r > hi ? hi : r;
}

// Grid lookup the way the placement rules expect it: an index past either
// end of the grid reads as nothing at all.
static char cell_at_index(const Level *lv, long idx) {
  if (idx < 0 || idx >= (long)lv->w * lv->h) return 0;
  return lv->grid[idx];
}

// The masonry most common among a cell's plain-wall neighbours (ties go to
// the lower digit), so a disguised cell melts into its surroundings.
static char neighbour_masonry(const Level *lv, int x, int y) {
  int counts[7] = {0};
  for (int d = 0; d < 4; d++) {
    char c = cell_at_index(lv, (long)(y + CARDINAL[d][1]) * lv->w + x + CARDINAL[d][0]);
    if (in_set("123456", c)) counts[c - '0']++;
  }
  char best = '1';
  int best_count = 0;
  for (int k = 1; k <= 6; k++) {
    if (counts[k] > best_count) {
      best_count = counts[k];
      best = (char)('0' + k);
    }
  }
  return best;
}

// The neighbouring treads belong to a stair flight or its landing.
static bool on_steps(const Level *lv, int x, int y) {
  for (int d = 0; d < 4; d++) {
    int nx = x + CARDINAL[d][0], ny = y + CARDINAL[d][1];
    if (nx >= 0 && ny >= 0 && nx < lv->w && ny < lv->h &&
        level_floor_at(lv, x, y) != level_floor_at(lv, nx, ny)) return true;
  }
  return false;
}

static bool is_floor(const Level *lv, int x, int y) {
  return x >= 0 && y >= 0 && x < lv->w && y < lv->h && lv->grid[y * lv->w + x] == '.';
}

// Projecting furniture needs an actual bay, rather than the three exposed
// floor cells of a T-junction. Keep the centre of narrow passages walkable.
static bool furnishing_bay(const Level *lv, int x, int y, int dx, int dy) {
  for (int depth = 1; depth <= 2; depth++)
    for (int side = -1; side <= 1; side++)
      if (!is_floor(lv, x + dx * depth - dy * side, y + dy * depth + dx * side)) return false;
  return true;
}

// Does the face (dx, dy) of wall cell (x, y) belong to a stretch of wall
// with a door set in it? The stretch runs sideways along the face for as
// long as there is masonry with open floor in front of it — a gap in the
// wall or a corner ends it.
static bool wall_has_door(const Level *lv, int x, int y, int dx, int dy) {
  for (int s = -1; s <= 1; s += 2) {
    int ax = -dy * s, ay = dx * s;
    for (int t = 1;; t++) {
      int nx = x + ax * t, ny = y + ay * t;
      if (!is_floor(lv, nx + dx, ny + dy)) break;
      char c = lv->grid[ny * lv->w + nx];
      if (c == '.') break;
      if (in_set("DRX", c)) return true;
    }
  }
  return false;
}

// is any torch closer than sqrt(r2) to (x, y)?
static bool near_torch(const Level *lv, double x, double y, double r2) {
  for (int i = 0; i < lv->torch_count; i++) {
    const Torch *t = &lv->torches[i];
    if ((t->x - x) * (t->x - x) + (t->y - y) * (t->y - y) < r2) return true;
  }
  return false;
}

static bool near_deco(const Level *lv, double x, double y, double r2) {
  for (int i = 0; i < lv->decoration_count; i++) {
    const Decoration *d = &lv->decorations[i];
    if ((d->x - x) * (d->x - x) + (d->y - y) * (d->y - y) < r2) return true;
  }
  return false;
}

static void add_deco(Level *lv, double x, double y, double radius, double phase, DecoKind kind) {
  lv->decorations[lv->decoration_count++] = (Decoration){x, y, radius, phase, kind};
}

typedef struct {
  int x, y, dx, dy;
  char cell;
  uint32_t hash;
  bool used;
  int order;
} BoxCandidate;

typedef struct {
  int x, y, dx, dy;
  uint32_t hash;
  int order;
} TorchCandidate;

typedef struct {
  int x, y, open;
  uint32_t hash;
  int order;
} TableSpot;

static int cmp_torch(const void *a, const void *b) {
  const TorchCandidate *p = a, *q = b;
  if (p->hash != q->hash) return p->hash < q->hash ? -1 : 1;
  return p->order - q->order;
}

static int cmp_box(const void *a, const void *b) {
  const BoxCandidate *p = a, *q = b;
  if (p->hash != q->hash) return p->hash < q->hash ? -1 : 1;
  return p->order - q->order;
}

static int cmp_table(const void *a, const void *b) {
  const TableSpot *p = a, *q = b;
  if (p->open != q->open) return q->open - p->open;
  if (p->hash != q->hash) return p->hash < q->hash ? -1 : 1;
  return p->order - q->order;
}

// ------------------------------------------------------------------ parsing

typedef struct {
  char name[64];
  char floor[32];
  char ceil[32];
  char start[8];
  char surfaces[32];
} Props;

static void copy_trimmed(char *dst, size_t size, const char *src, size_t len) {
  while (len && isspace((unsigned char)*src)) {
    src++;
    len--;
  }
  while (len && isspace((unsigned char)src[len - 1])) len--;
  if (len >= size) len = size - 1;
  memcpy(dst, src, len);
  dst[len] = 0;
}

// "@key value" — a word, whitespace, then the value.
static void parse_prop(Props *props, const char *line, size_t len) {
  size_t i = 1;
  while (i < len && (isalnum((unsigned char)line[i]) || line[i] == '_')) i++;
  if (i == 1 || i >= len || !isspace((unsigned char)line[i])) return;
  size_t key_len = i - 1;
  const char *key = line + 1;
  while (i < len && isspace((unsigned char)line[i])) i++;
  const char *value = line + i;
  size_t value_len = len - i;
  if (key_len == 4 && !strncmp(key, "name", 4))
    copy_trimmed(props->name, sizeof props->name, value, value_len);
  else if (key_len == 5 && !strncmp(key, "floor", 5))
    copy_trimmed(props->floor, sizeof props->floor, value, value_len);
  else if (key_len == 4 && !strncmp(key, "ceil", 4))
    copy_trimmed(props->ceil, sizeof props->ceil, value, value_len);
  else if (key_len == 5 && !strncmp(key, "start", 5))
    copy_trimmed(props->start, sizeof props->start, value, value_len);
  else if (key_len == 8 && !strncmp(key, "surfaces", 8))
    copy_trimmed(props->surfaces, sizeof props->surfaces, value, value_len);
}

static bool blank(const char *s, size_t len) {
  for (size_t i = 0; i < len; i++)
    if (!isspace((unsigned char)s[i])) return false;
  return true;
}

typedef struct {
  const char *s;
  size_t len;
} Row;

bool level_parse(Level *lv, const char *text, char *err, size_t err_size) {
  memset(lv, 0, sizeof *lv);
  Props props = {"FLOOR", "#707070", "#383838", "E", "guardhouse"};

  // split into lines, collecting the map rows
  size_t text_len = strlen(text);
  int row_cap = 16, h = 0;
  Row *rows = malloc(sizeof(Row) * row_cap);
  int w = 0;
  for (const char *p = text; p < text + text_len || p == text;) {
    const char *nl = memchr(p, '\n', (size_t)(text + text_len - p));
    size_t len = nl ? (size_t)(nl - p) : (size_t)(text + text_len - p);
    size_t raw_len = len;
    if (raw_len && p[raw_len - 1] == '\r') raw_len--;
    if (raw_len && p[0] == ';') {
      // comment
    } else if (raw_len && p[0] == '@') {
      parse_prop(&props, p, raw_len);
    } else if (!blank(p, raw_len)) {
      if (h == row_cap) {
        row_cap *= 2;
        rows = realloc(rows, sizeof(Row) * row_cap);
      }
      rows[h].s = p;
      rows[h].len = raw_len;
      if ((int)raw_len > w) w = (int)raw_len;
      h++;
    }
    if (!nl) break;
    p = nl + 1;
  }
  if (h == 0 || w == 0) {
    snprintf(err, err_size, "Level has no map");
    free(rows);
    return false;
  }

  static const char *const themes[SURFACE_THEME_COUNT] = {"guardhouse", "cellars", "barracks", "vault", "keep"};
  int theme = 0;
  while (theme < SURFACE_THEME_COUNT && strcmp(props.surfaces, themes[theme])) theme++;
  if (theme == SURFACE_THEME_COUNT) {
    snprintf(err, err_size, "Unknown surface theme: %s", props.surfaces);
    free(rows);
    return false;
  }
  lv->surface_theme = (SurfaceTheme)theme;

  const int cells = w * h;
  lv->w = w;
  lv->h = h;
  lv->grid = malloc((size_t)cells + 1);
  memset(lv->grid, '1', (size_t)cells);
  lv->grid[cells] = 0;
  lv->heights = calloc((size_t)cells, sizeof(float));
  // Rectangles can overlap: later @height directives override earlier ones.
  // Read bounded lines separately so malformed values cannot consume map rows.
  for (const char *line = text; *line;) {
    const char *end = strchr(line, '\n');
    size_t len = end ? (size_t)(end - line) : strlen(line);
    if (len >= 7 && !strncmp(line, "@height", 7) && (len == 7 || isspace((unsigned char)line[7]))) {
      char value[256], extra;
      int x, y, rw, rh;
      float z;
      copy_trimmed(value, sizeof value, line + 7, len - 7);
      if (sscanf(value, "%d %d %d %d %f %c", &x, &y, &rw, &rh, &z, &extra) != 5 ||
          x < 0 || y < 0 || rw <= 0 || rh <= 0 || x >= w || y >= h || rw > w - x || rh > h - y ||
          !isfinite(z) || fabsf(z) > LEVEL_MAX_HEIGHT || fabsf(z / LEVEL_STEP - roundf(z / LEVEL_STEP)) > .001f) {
        snprintf(err, err_size, "Invalid @height: expected x y width height elevation (steps of 0.125, -4 to 4)");
        free(rows);
        level_free(lv);
        return false;
      }
      for (int yy = y; yy < y + rh; yy++)
        for (int xx = x; xx < x + rw; xx++) lv->heights[yy * w + xx] = z;
    }
    if (!end) break;
    line = end + 1;
  }
  for (int i = 0; i < cells; i++)
    if (lv->heights[i] != 0) lv->has_heights = true;
  // Ceilings are independent of floors. Unspecified cells retain the legacy
  // one-unit clearance, including when a custom map edits its floor later.
  lv->ceilings = malloc((size_t)cells * sizeof(float));
  for (int i = 0; i < cells; i++) lv->ceilings[i] = NAN;
  for (const char *line = text; *line;) {
    const char *end = strchr(line, '\n');
    size_t len = end ? (size_t)(end - line) : strlen(line);
    if (len >= 8 && !strncmp(line, "@ceiling", 8) && (len == 8 || isspace((unsigned char)line[8]))) {
      char value[256], extra;
      int x, y, rw, rh;
      float z;
      copy_trimmed(value, sizeof value, line + 8, len - 8);
      if (sscanf(value, "%d %d %d %d %f %c", &x, &y, &rw, &rh, &z, &extra) != 5 ||
          x < 0 || y < 0 || rw <= 0 || rh <= 0 || x >= w || y >= h || rw > w - x || rh > h - y ||
          !isfinite(z) || fabsf(z) > LEVEL_MAX_HEIGHT + 1 || fabsf(z / LEVEL_STEP - roundf(z / LEVEL_STEP)) > .001f) {
        snprintf(err, err_size, "Invalid @ceiling: expected x y width height elevation (steps of 0.125, -5 to 5)");
        free(rows);
        level_free(lv);
        return false;
      }
      for (int yy = y; yy < y + rh; yy++)
        for (int xx = x; xx < x + rw; xx++) lv->ceilings[yy * w + xx] = z;
      lv->has_ceilings = true;
    }
    if (!end) break;
    line = end + 1;
  }
  // A full unit of headroom keeps every existing actor and trap usable.
  for (int i = 0; i < cells; i++) {
    if (isfinite(lv->ceilings[i]) && lv->ceilings[i] < lv->heights[i] + 1) {
      snprintf(err, err_size, "Invalid @ceiling: less than one unit above floor at %d,%d", i % w, i / w);
      free(rows);
      level_free(lv);
      return false;
    }
  }
  char *grid = lv->grid;

  lv->items = malloc(sizeof(Item) * cells);
  lv->enemies = malloc(sizeof(EnemySpawn) * cells);
  lv->barrels = malloc(sizeof(BarrelSpawn) * cells);
  lv->doors = malloc(sizeof(DoorSpawn) * cells);
  lv->levers = malloc(sizeof(Lever) * cells);
  lv->portals = malloc(sizeof(Portal) * cells);
  lv->traps = malloc(sizeof(TrapSpawn) * cells);
  lv->pendulums = malloc(sizeof(PendulumSpawn) * cells);
  int *plates = malloc(sizeof(int) * cells * 2);
  int *secrets = malloc(sizeof(int) * cells * 2);
  int plate_count = 0, secret_cells = 0;
  bool has_player = false;
  float player_x = 0, player_y = 0;

  for (int y = 0; y < h; y++) {
    for (int x = 0; x < w; x++) {
      char ch = x < (int)rows[y].len ? rows[y].s[x] : '1';
      if (ch == ' ' || ch == '#') ch = '1'; // padding counts as wall
      // Keep the map sealed even if breakable masonry is put on its edge.
      if (ch == '8' && (x == 0 || y == 0 || x == w - 1 || y == h - 1)) ch = '1';
      char cell = '.';
      int kind;
      if (level_is_wall(ch)) {
        cell = ch;
      } else if (ch == 'D' || ch == 'R') {
        cell = ch;
        lv->doors[lv->door_count++] = (DoorSpawn){x, y, ch, 0};
      } else if (ch == 'X' || ch == '!') {
        cell = ch;
        if (ch == '!') lv->pit_count++;
      } else if (level_is_water(ch)) {
        cell = ch;
        lv->water_count++;
      } else if (ch == 'Z') {
        cell = 'Z';
        secrets[secret_cells * 2] = x;
        secrets[secret_cells * 2 + 1] = y;
        secret_cells++;
      } else if (ch == 'L') {
        cell = 'L';
        Lever *l = &lv->levers[lv->lever_count++];
        memset(l, 0, sizeof *l);
        l->x = x;
        l->y = y;
      } else if (ch == '~') {
        cell = '~';
        lv->portals[lv->portal_count++] = (Portal){x, y, 0, 0, -1};
      } else if (ch == 'P') {
        has_player = true;
        player_x = x + 0.5f;
        player_y = y + 0.5f;
      } else if (ch == '^') {
        plates[plate_count * 2] = x;
        plates[plate_count * 2 + 1] = y;
        plate_count++;
      } else if (ch == '&' || ch == '%') {
        lv->pendulums[lv->pendulum_count++] = (PendulumSpawn){x, y, ch == '&', ch == '%'};
      } else if (ch == 'Q' || ch == 'q') {
        lv->barrels[lv->barrel_count++] = (BarrelSpawn){x + 0.5f, y + 0.5f, ch == 'Q'};
      } else if ((kind = item_kind_for(ch)) >= 0) {
        lv->items[lv->item_count++] = (Item){x + 0.5f, y + 0.5f, (ItemKind)kind};
      } else if ((kind = enemy_type_for((char)toupper((unsigned char)ch))) >= 0) {
        lv->enemies[lv->enemy_count++] =
            (EnemySpawn){x + 0.5f, y + 0.5f, (EnemyType)kind, ch == toupper((unsigned char)ch), -1};
      }
      grid[y * w + x] = cell;
    }
  }
  free(rows);

  lv->start_x = player_x; lv->start_y = player_y;
  if (!world_configure(lv, text, err, err_size)) {
    free(plates); free(secrets); level_free(lv); return false;
  }
  if (lv->world.cells) lv->has_ceilings = true;

  // Every war hound is leashed to the nearest man-at-arms within earshot
  // (4 cells): it heels to him, springs when he fights and barks him awake
  // in turn. A hound with no soldier nearby just guards its own ground.
  for (int k = 0; k < lv->enemy_count; k++) {
    EnemySpawn *dog = &lv->enemies[k];
    if (dog->type != ENEMY_DOG) continue;
    int best = -1;
    float best_d = 0;
    for (int i = 0; i < lv->enemy_count; i++) {
      const EnemySpawn *e = &lv->enemies[i];
      if (e->type == ENEMY_DOG || e->type == ENEMY_BAT || e->type == ENEMY_RAT || e->type == ENEMY_SPIDER) continue;
      float d = sq(e->x - dog->x) + sq(e->y - dog->y);
      if (d <= 16 && (best < 0 || d < best_d)) {
        best = i;
        best_d = d;
      }
    }
    dog->master = best;
  }

  // Resolve each trap's shooter: the nearest wall in a cardinal direction
  // with a clear run of floor becomes an arrow slit (texture "7"). Slits at
  // least 2 cells out are preferred so the volley is visible in flight.
  for (int t = 0; t < plate_count; t++) {
    int tx = plates[t * 2], ty = plates[t * 2 + 1];
    bool found = false;
    int best_rank = 0, bcx = 0, bcy = 0, bdx = 0, bdy = 0;
    for (int dir = 0; dir < 4; dir++) {
      int dx = CARDINAL[dir][0], dy = CARDINAL[dir][1];
      for (int d = 1; d <= 8; d++) {
        int cx = tx + dx * d, cy = ty + dy * d;
        if (cx < 0 || cy < 0 || cx >= w || cy >= h) break;
        char cell = grid[cy * w + cx];
        if (level_is_open(cell)) continue;
        if (level_is_wall(cell) && cell != '8') {
          int rank = (d >= 2 ? 0 : 100) + d;
          if (!found || rank < best_rank) {
            found = true;
            best_rank = rank;
            bcx = cx;
            bcy = cy;
            bdx = dx;
            bdy = dy;
          }
        }
        break; // doors and the gate can't hide a slit
      }
    }
    if (!found) continue; // walled-in plate: nowhere to shoot from, drop it
    grid[bcy * w + bcx] = '7';
    lv->traps[lv->trap_count++] =
        (TrapSpawn){tx, ty, (float)-bdx, (float)-bdy, bcx + 0.5f - bdx * 0.51f, bcy + 0.5f - bdy * 0.51f};
  }

  // Secret walls become sliding doors in disguise: each takes the texture
  // most common among its plain-wall neighbours so it melts into the
  // masonry, and is wired to the nearest lever, which will open it.
  for (int s = 0; s < secret_cells; s++) {
    int sx = secrets[s * 2], sy = secrets[s * 2 + 1];
    lv->doors[lv->door_count++] = (DoorSpawn){sx, sy, 'Z', neighbour_masonry(lv, sx, sy)};
    Lever *best = NULL;
    int best_d = 0;
    for (int i = 0; i < lv->lever_count; i++) {
      Lever *l = &lv->levers[i];
      int d = (l->x - sx) * (l->x - sx) + (l->y - sy) * (l->y - sy);
      if (!best || d < best_d) {
        best = l;
        best_d = d;
      }
    }
    if (!best)
      fprintf(stderr, "Secret wall at %d,%d has no lever — it will never open\n", sx, sy);
    else if (best->door_count < LEVER_MAX_DOORS)
      best->doors[best->door_count++] = sy * w + sx;
  }

  // Give each lever its one face. Only floor the player can actually walk to
  // qualifies (flood fill with secrets still closed), so the handle can never
  // face into a wall, a sealed stash or an unreachable pocket. With several
  // reachable faces the one nearest the lever's secret wall wins — that is
  // where the player will be hunting. The other faces of the cell wear the
  // neighbours' masonry, like the secret walls do.
  uint8_t *reachable = calloc((size_t)cells, 1);
  if (has_player && (lv->lever_count || lv->portal_count)) {
    int *stack = malloc(sizeof(int) * cells);
    int top = 0;
    int start = (int)floorf(player_y) * w + (int)floorf(player_x);
    stack[top++] = start;
    reachable[start] = 1;
    while (top) {
      int idx = stack[--top];
      int ix = idx % w, iy = idx / w;
      for (int d = 0; d < 4; d++) {
        int nx = ix + CARDINAL[d][0], ny = iy + CARDINAL[d][1];
        if (nx < 0 || ny < 0 || nx >= w || ny >= h) continue;
        int n = ny * w + nx;
        if (reachable[n]) continue;
        char c = grid[n];
        if (c != '.' && !level_is_water(c) && c != 'D' && c != 'R') continue;
        if (fabsf(level_floor_at(lv, nx, ny) - level_floor_at(lv, ix, iy)) > LEVEL_STEP + .001f) continue;
        reachable[n] = 1;
        stack[top++] = n;
      }
    }
    free(stack);
  }
  for (int i = 0; i < lv->lever_count; i++) {
    Lever *l = &lv->levers[i];
    l->tex = neighbour_masonry(lv, l->x, l->y);
    int door_x = l->door_count ? l->doors[0] % w : l->x;
    int door_y = l->door_count ? l->doors[0] / w : l->y;
    bool found = false;
    int best_d = 0;
    for (int d = 0; d < 4; d++) {
      int nx = l->x + CARDINAL[d][0], ny = l->y + CARDINAL[d][1];
      if (nx < 0 || ny < 0 || nx >= w || ny >= h) continue;
      if ((grid[ny * w + nx] != '.' && !level_is_water(grid[ny * w + nx])) || !reachable[ny * w + nx]) continue;
      int dd = (nx - door_x) * (nx - door_x) + (ny - door_y) * (ny - door_y);
      if (!found || dd < best_d) {
        found = true;
        best_d = dd;
        l->dx = CARDINAL[d][0];
        l->dy = CARDINAL[d][1];
      }
    }
    if (!found) {
      fprintf(stderr, "Lever at %d,%d faces no reachable floor — it cannot be pulled\n", l->x, l->y);
      l->dx = 1;
      l->dy = 0;
    }
  }

  // Mirror portals pair up in reading order: first with second, third with
  // fourth. Each picks the floor cell it spills arrivals onto — reachable
  // floor is preferred, and floor that runs on for a second cell beats a
  // dead stop against masonry, so the traveller never steps out nose to a
  // wall when the mirror can help it. Unreachable floor is still fair game:
  // a vault served only by its mirror is the whole point of one. A mirror
  // with no twin, or a pair with a walled-in end, stays solid glass.
  for (int i = 0; i < lv->portal_count; i++) {
    Portal *pt = &lv->portals[i];
    int best_score = -1;
    for (int d = 0; d < 4; d++) {
      int dx = CARDINAL[d][0], dy = CARDINAL[d][1];
      int nx = pt->x + dx, ny = pt->y + dy;
      if (nx < 0 || ny < 0 || nx >= w || ny >= h) continue;
      char c = grid[ny * w + nx];
      if (c != '.' && !level_is_water(c)) continue;
      char beyond = cell_at_index(lv, (long)(ny + dy) * w + nx + dx);
      int runs_on = beyond == '.' || level_is_water(beyond);
      int score = (reachable[ny * w + nx] ? 2 : 0) + runs_on;
      if (score > best_score) {
        best_score = score;
        pt->dx = dx;
        pt->dy = dy;
      }
    }
  }
  for (int i = 0; i < lv->portal_count; i++) {
    Portal *pt = &lv->portals[i];
    int twin = i ^ 1;
    if (twin >= lv->portal_count) {
      pt->pair = -1;
      fprintf(stderr, "Portal at %d,%d has no twin — it stays solid glass\n", pt->x, pt->y);
    } else if (!lv->portals[twin].dx && !lv->portals[twin].dy) {
      pt->pair = -1;
      fprintf(stderr, "Portal at %d,%d is walled in — the pair stays solid glass\n", lv->portals[twin].x,
              lv->portals[twin].y);
    } else {
      pt->pair = twin;
    }
  }

  // Scatter a modest number of torches along walls that border walkable
  // space. Sorting by a coordinate hash makes the layout stable while the
  // spacing pass keeps corridors from turning into rows of identical lights.
  // The banner of "6" hangs where a torch would go, so it never takes one.
  TorchCandidate *torch_cands = malloc(sizeof(TorchCandidate) * cells * 4);
  int torch_cand_count = 0;
  for (int y = 1; y < h - 1; y++) {
    for (int x = 1; x < w - 1; x++) {
      char cell = grid[y * w + x];
      if (!level_is_wall(cell) || cell == '6' || cell == '7' || cell == '8' || cell == 'B') continue;
      for (int d = 0; d < 4; d++) {
        int dx = CARDINAL[d][0], dy = CARDINAL[d][1];
        if (grid[(y + dy) * w + x + dx] != '.') continue;
        uint32_t hash = imul((uint32_t)(x + 17), 73856093u) ^ imul((uint32_t)(y + 31), 19349663u) ^
                        imul((uint32_t)(d + 7), 83492791u);
        torch_cands[torch_cand_count] = (TorchCandidate){x, y, dx, dy, hash, torch_cand_count};
        torch_cand_count++;
      }
    }
  }
  qsort(torch_cands, (size_t)torch_cand_count, sizeof(TorchCandidate), cmp_torch);
  int torch_limit = (int)round_limit(7, 14, (double)cells / 70);
  lv->torches = malloc(sizeof(Torch) * (torch_limit + 1));
  for (int i = 0; i < torch_cand_count && lv->torch_count < torch_limit; i++) {
    const TorchCandidate *c = &torch_cands[i];
    double tx = c->x + 0.5 + c->dx * 0.52;
    double ty = c->y + 0.5 + c->dy * 0.52;
    bool near = false;
    for (int t = 0; t < lv->torch_count && !near; t++)
      near = (lv->torches[t].x - tx) * (lv->torches[t].x - tx) + (lv->torches[t].y - ty) * (lv->torches[t].y - ty) < 18;
    if (near) continue;
    lv->torches[lv->torch_count++] = (Torch){tx, ty, (double)(c->hash % 1000) / 1000 * M_PI * 2};
  }
  free(torch_cands);

  // Furnish the larger rooms: a table wherever the floor opens up wide, with
  // a chair or two pulled to its sides. A spot qualifies when its 3x3 core is
  // clear of walls, spawns and pickups and most of the surrounding 5x5 is
  // open floor — corridors and crossings never pass that test. Openness-first
  // ordering fills the biggest halls; the hash keeps ties stable.
  uint8_t *spawn = calloc((size_t)cells, 1);
  for (int y = 0; y < h; y++) for (int x = 0; x < w; x++)
    if (world_reserved(lv, x, y)) spawn[y * w + x] = 1;
  if (has_player) spawn[(int)floorf(player_y) * w + (int)floorf(player_x)] = 1;
  for (int i = 0; i < lv->item_count; i++)
    spawn[(int)floorf(lv->items[i].y) * w + (int)floorf(lv->items[i].x)] = 1;
  for (int i = 0; i < lv->enemy_count; i++)
    spawn[(int)floorf(lv->enemies[i].y) * w + (int)floorf(lv->enemies[i].x)] = 1;
  for (int i = 0; i < lv->barrel_count; i++)
    spawn[(int)floorf(lv->barrels[i].y) * w + (int)floorf(lv->barrels[i].x)] = 1;
  for (int t = 0; t < plate_count; t++) spawn[plates[t * 2 + 1] * w + plates[t * 2]] = 1;
  // Leave the whole swing and its approaches clear of automatic furniture.
  for (int i = 0; i < lv->pendulum_count; i++) {
    const PendulumSpawn *p = &lv->pendulums[i];
    for (int y = maxi(0, p->y - 1); y <= mini(h - 1, p->y + 1); y++)
      for (int x = maxi(0, p->x - 1); x <= mini(w - 1, p->x + 1); x++) spawn[y * w + x] = 1;
  }
  // keep the cell a portal delivers onto clear of furniture and chests
  for (int i = 0; i < lv->portal_count; i++) {
    const Portal *pt = &lv->portals[i];
    if (pt->dx || pt->dy) spawn[(pt->y + pt->dy) * w + pt->x + pt->dx] = 1;
  }
  free(plates);
  free(secrets);
  free(reachable);

  TableSpot *spots = malloc(sizeof(TableSpot) * cells);
  int spot_count = 0;
  for (int y = 2; y < h - 2; y++) {
    for (int x = 2; x < w - 2; x++) {
      bool core_ok = true;
      for (int dy = -1; dy <= 1 && core_ok; dy++) {
        for (int dx = -1; dx <= 1; dx++) {
          // walls must clear the whole 3x3; spawns and pickups only matter on
          // the table cell itself and the orthogonal cells chairs may take
          if (!is_floor(lv, x + dx, y + dy) || (dx * dy == 0 && spawn[(y + dy) * w + x + dx])) {
            core_ok = false;
            break;
          }
        }
      }
      if (!core_ok) continue;
      int open = 0;
      for (int dy = -2; dy <= 2; dy++)
        for (int dx = -2; dx <= 2; dx++)
          if (is_floor(lv, x + dx, y + dy)) open++;
      if (open < 16) continue;
      uint32_t hash = imul((uint32_t)(x + 41), 2654435761u) ^ imul((uint32_t)(y + 89), 97370749u);
      spots[spot_count] = (TableSpot){x, y, open, hash, spot_count};
      spot_count++;
    }
  }
  qsort(spots, (size_t)spot_count, sizeof(TableSpot), cmp_table);

  const int deco_cap = cells + 64 + WORLD_PROPS;
  lv->decorations = malloc(sizeof(Decoration) * deco_cap);
  Decoration *decos = lv->decorations;

  // A stone fountain graces the widest court. It takes the very best spots
  // before the furniture is brought in, demands nearly all of its 5x5 to be
  // open floor — so cramped keeps simply go without — and the tables then
  // keep a respectful distance from its basin.
  int fountain_limit = cells >= 900 ? 2 : 1;
  int fountains = 0;
  for (int i = 0; i < spot_count && fountains < fountain_limit; i++) {
    const TableSpot *s = &spots[i];
    if (s->open < 19) continue;
    double cx = s->x + 0.5, cy = s->y + 0.5;
    if (near_deco(lv, cx, cy, 100)) continue;
    add_deco(lv, cx, cy, 0.42, 0, DECO_FOUNTAIN);
    fountains++;
  }

  int table_limit = (int)round_limit(2, 6, (double)cells / 110);
  int tables = 0;
  for (int i = 0; i < spot_count && tables < table_limit; i++) {
    const TableSpot *s = &spots[i];
    double cx = s->x + 0.5, cy = s->y + 0.5;
    bool crowded = false;
    for (int d = 0; d < lv->decoration_count && !crowded; d++) {
      double q = (decos[d].x - cx) * (decos[d].x - cx) + (decos[d].y - cy) * (decos[d].y - cy);
      double r = decos[d].kind == DECO_TABLE ? 22 : decos[d].kind == DECO_FOUNTAIN ? 8 : 0;
      crowded = q < r;
    }
    if (crowded) continue;
    add_deco(lv, cx, cy, 0.36, 0, DECO_TABLE);
    tables++;
    // chairs sit in the (guaranteed clear) neighbour cells, pulled to the table
    int chairs = 1 + (int)((s->hash >> 3) & 1);
    for (int k = 0; k < chairs; k++) {
      const int *dir = CARDINAL[(s->hash + (uint32_t)k) % 4];
      add_deco(lv, cx + dir[0] * 0.85, cy + dir[1] * 0.85, 0.18, 0, DECO_CHAIR);
    }
  }
  free(spots);

  // Wall boxes (wardrobes and chests) lean against flat wall stretches. The
  // wall cell keeps its own texture — the renderer draws the box in front of
  // it — so masonry tiling is never disturbed. A spot needs a plain wall
  // flanked by more plain wall (so the box sits flush, with no door/bars/slit
  // at its edges), open floor two cells deep in front (never plugs a
  // corridor), and clearance from torches and furniture. A wall with a door
  // in it stays bare: the hearth, armour, candelabra, hangings, chains and
  // niches below all pick from these same candidates.
  BoxCandidate *cands = malloc(sizeof(BoxCandidate) * cells * 4);
  int cand_count = 0;
  for (int y = 1; y < h - 1; y++) {
    for (int x = 1; x < w - 1; x++) {
      if (!in_set("123456", grid[y * w + x])) continue;
      for (int d = 0; d < 4; d++) {
        int dx = CARDINAL[d][0], dy = CARDINAL[d][1];
        int fx = x + dx, fy = y + dy;
        if (world_reserved(lv, fx, fy)) continue;
        if (grid[fy * w + fx] != '.' || spawn[fy * w + fx]) continue;
        if (!is_floor(lv, fx + dx, fy + dy)) continue;
        char flank_a = grid[(y + dx) * w + (x - dy)];
        char flank_b = grid[(y - dx) * w + (x + dy)];
        if (!in_set("1234567", flank_a) || !in_set("1234567", flank_b)) continue;
        if (wall_has_door(lv, x, y, dx, dy)) continue;
        uint32_t hash = imul((uint32_t)(x + 59), 374761393u) ^ imul((uint32_t)(y + 23), 668265263u) ^
                        imul((uint32_t)(d + 3), 40503u);
        cands[cand_count] = (BoxCandidate){x, y, dx, dy, grid[y * w + x], hash, false, cand_count};
        cand_count++;
      }
    }
  }
  qsort(cands, (size_t)cand_count, sizeof(BoxCandidate), cmp_box);
  free(spawn);

  lv->wall_boxes = malloc(sizeof(WallBox) * (cand_count + 1));
  lv->hangings = malloc(sizeof(Hanging) * (cand_count + 1));
  WallBox *boxes = lv->wall_boxes;
  Hanging *hangings = lv->hangings;

  // A fireplace blazes in one hall (two on the biggest floors): a stone
  // surround set into plain masonry with logs alight in the grate. Its
  // wall attachment anchors the solid surround and animated firebox.
  // It burns like an oversized torch, so it claims a stretch
  // the torches left dark, and it would rather have flanking floor on both
  // sides, so it reads as the heart of a room instead of a plug in a
  // corridor. Placed before the wardrobes and chests: the hearth is
  // the centrepiece and picks its wall first. A collision circle
  // half-buried in the wall keeps anyone from standing in the flames.
  int fireplace_limit = cells >= 900 ? 2 : 1;
  int fireplaces = 0;
  // Legacy custom maps keep their stable furnishing layout and save signature.
  // The new campaign opts into independent ceilings and the stricter bays.
  for (int pass = 0; pass < (lv->has_ceilings ? 1 : 2); pass++) {
    if (fireplaces > 0) break;
    for (int i = 0; i < cand_count && fireplaces < fireplace_limit; i++) {
      BoxCandidate *c = &cands[i];
      if (c->used || !in_set("1235", c->cell)) continue;
      if (lv->has_ceilings) {
        if (!furnishing_bay(lv, c->x, c->y, c->dx, c->dy)) continue;
      } else if (pass == 0) {
        int fx = c->x + c->dx, fy = c->y + c->dy;
        if (!is_floor(lv, fx - c->dy, fy + c->dx) || !is_floor(lv, fx + c->dy, fy - c->dx)) continue;
      }
      double cx = c->x + 0.5 + c->dx * 0.56, cy = c->y + 0.5 + c->dy * 0.56;
      if (near_torch(lv, cx, cy, 4)) continue;
      bool crowded = false;
      for (int d = 0; d < lv->decoration_count && !crowded; d++) {
        double q = (decos[d].x - cx) * (decos[d].x - cx) + (decos[d].y - cy) * (decos[d].y - cy);
        crowded = q < (decos[d].kind == DECO_FIREPLACE ? 100 : 2.56);
      }
      if (crowded) continue;
      c->used = true;
      hangings[lv->hanging_count++] = (Hanging){c->x, c->y, c->dx, c->dy, HANG_FIREPLACE, c->hash};
      // the decoration carries no sprite of its own — just the collision
      // circle and the light the fire throws (position, phase)
      add_deco(lv, cx, cy, 0.26, (double)((c->hash >> 8) % 1000) / 1000 * M_PI * 2, DECO_FIREPLACE);
      fireplaces++;
    }
  }

  // wardrobes and chests
  struct {
    DecoKind deco;
    BoxKind box;
    int limit;
    double radius;
    const char *backing;
  } box_kinds[2] = {
      // backing: wall textures the box may stand against — a tall wardrobe
      // would awkwardly cover the window of "4" or the banner of "6", a low
      // chest only clashes with the banner
      {DECO_WARDROBE, BOX_WARDROBE, (int)round_limit(2, 5, (double)cells / 130), 0.28, "1235"},
      {DECO_CHEST, BOX_CHEST, (int)round_limit(2, 4, (double)cells / 160), 0.28, "12345"},
  };
  for (int b = 0; b < 2; b++) {
    int placed = 0;
    for (int i = 0; i < cand_count && placed < box_kinds[b].limit; i++) {
      BoxCandidate *c = &cands[i];
      if (c->used || !in_set(box_kinds[b].backing, c->cell)) continue;
      // collision circle sits on the wall face itself — half of it buried in
      // the wall — so it barely pokes past the box front and leaves corridors
      // walkable
      double cx = c->x + 0.5 + c->dx * 0.5, cy = c->y + 0.5 + c->dy * 0.5;
      if (near_torch(lv, cx, cy, 1.7)) continue;
      if (near_deco(lv, cx, cy, 1.45)) continue;
      bool near = false;
      for (int k = 0; k < lv->wall_box_count && !near; k++) {
        double q = (boxes[k].x + 0.5 - cx) * (boxes[k].x + 0.5 - cx) + (boxes[k].y + 0.5 - cy) * (boxes[k].y + 0.5 - cy);
        near = q < (boxes[k].kind == box_kinds[b].box ? 9 : 2.56);
      }
      if (near) continue;
      c->used = true;
      boxes[lv->wall_box_count++] = (WallBox){c->x, c->y, c->dx, c->dy, box_kinds[b].box};
      add_deco(lv, cx, cy, box_kinds[b].radius, 0, box_kinds[b].deco);
      placed++;
    }
  }

  // A suit of armour stands against the wall, rendered as a billboard like
  // the furniture, so it keeps its presence from every angle. Pure set
  // dressing: no collision circle, so it can never block a corridor.
  int armor_limit = (int)round_limit(1, 3, (double)cells / 220);
  int armors = 0;
  for (int i = 0; i < cand_count && armors < armor_limit; i++) {
    BoxCandidate *c = &cands[i];
    if (c->used || !in_set("1235", c->cell)) continue;
    double cx = c->x + 0.5 + c->dx * 0.7, cy = c->y + 0.5 + c->dy * 0.7;
    if (near_torch(lv, cx, cy, 1.7)) continue;
    if (near_deco(lv, cx, cy, 2.56)) continue;
    c->used = true;
    add_deco(lv, cx, cy, 0, 0, DECO_ARMOR);
    armors++;
  }

  // Standing candelabra keep the stretches the torches never reach — the
  // wide berth demanded of torches below steers them into the gloom, where
  // their small flames actually read. A sliver of collision keeps the player
  // from walking through burning candles without ever pinching a corridor.
  int candelabra_limit = (int)round_limit(1, 4, (double)cells / 200);
  int candelabras = 0;
  for (int i = 0; i < cand_count && candelabras < candelabra_limit; i++) {
    BoxCandidate *c = &cands[i];
    if (c->used) continue;
    if (lv->has_ceilings && !furnishing_bay(lv, c->x, c->y, c->dx, c->dy)) continue;
    double cx = c->x + 0.5 + c->dx * 0.68, cy = c->y + 0.5 + c->dy * 0.68;
    if (near_torch(lv, cx, cy, 4)) continue;
    if (near_deco(lv, cx, cy, 2.56)) continue;
    c->used = true;
    add_deco(lv, cx, cy, 0.14, 0, DECO_CANDELABRA);
    candelabras++;
  }

  // Crossed swords, shields, portraits, chains, cracks and cobwebs hang
  // flat on the masonry.
  struct {
    HangKind kind;
    int limit;
    const char *backing;
  } hang_kinds[6] = {
      // portraits of the fortress's old masters claim the cleanest masonry
      // first — never the mossy or dungeon walls, where oils would rot. Their
      // painted eyes follow whoever passes; the renderer sees to that. The
      // hash carried in `seed` picks which of the sitters hangs where.
      {HANG_PORTRAIT, (int)round_limit(1, 3, (double)cells / 210), "135"},
      // trophies suit dressed stone; chains also hang from the rougher
      // dungeon masonry, where a shield would look out of place
      {HANG_SHIELD, (int)round_limit(1, 3, (double)cells / 200), "1235"},
      {HANG_SWORD, (int)round_limit(1, 3, (double)cells / 200), "1235"},
      {HANG_CHAIN, (int)round_limit(1, 4, (double)cells / 170), "1245"},
      // the fortress is old and its masonry is giving way: settling cracks
      // wander down the plainer stonework, some spalled open with the rubble
      // of the fallen facing at the skirting. Dressed feature walls keep
      // their faces
      {HANG_CRACK, (int)round_limit(2, 6, (double)cells / 110), "1245"},
      // cobwebs gather in the corners no chatelaine has dusted in years —
      // any masonry suits them, and the torch check steers them into the
      // gloom
      {HANG_COBWEB, (int)round_limit(2, 7, (double)cells / 90), "123456"},
  };
  for (int k = 0; k < 6; k++) {
    int placed = 0;
    for (int i = 0; i < cand_count && placed < hang_kinds[k].limit; i++) {
      BoxCandidate *c = &cands[i];
      if (c->used || !in_set(hang_kinds[k].backing, c->cell)) continue;
      double cx = c->x + 0.5 + c->dx * 0.6, cy = c->y + 0.5 + c->dy * 0.6;
      if (near_torch(lv, cx, cy, 1.7)) continue;
      if (near_deco(lv, cx, cy, 1.45)) continue;
      bool near = false;
      for (int j = 0; j < lv->hanging_count && !near; j++) {
        const Hanging *hh = &hangings[j];
        double q = (hh->x + 0.5 - cx) * (hh->x + 0.5 - cx) + (hh->y + 0.5 - cy) * (hh->y + 0.5 - cy);
        near = q < (hh->kind == hang_kinds[k].kind ? 9 : 2.56);
      }
      if (near) continue;
      c->used = true;
      hangings[lv->hanging_count++] = (Hanging){c->x, c->y, c->dx, c->dy, hang_kinds[k].kind, c->hash};
      placed++;
    }
  }
  // consecutive seeds walk the sitters round-robin, so no floor hangs the
  // same face twice before all of them have had their place
  uint32_t sitter = 0;
  for (int j = 0; j < lv->hanging_count; j++) {
    if (hangings[j].kind == HANG_PORTRAIT) {
      sitter = hangings[j].seed;
      break;
    }
  }
  for (int j = 0; j < lv->hanging_count; j++)
    if (hangings[j].kind == HANG_PORTRAIT) hangings[j].seed = sitter++;

  // More chains hang from the ceiling, kept to cells in front of walls so
  // they never dangle in the middle of a hall. Pure set dressing like the
  // armour: no collision circle, and the hook ends around eye level so
  // walking beneath them reads naturally.
  int chain_limit = (int)round_limit(1, 4, (double)cells / 190);
  int chains = 0;
  for (int i = 0; i < cand_count && chains < chain_limit; i++) {
    BoxCandidate *c = &cands[i];
    if (c->used) continue;
    double cx = c->x + 0.5 + c->dx, cy = c->y + 0.5 + c->dy;
    if (near_torch(lv, cx, cy, 1)) continue;
    bool crowded = false;
    for (int d = 0; d < lv->decoration_count && !crowded; d++) {
      double q = (decos[d].x - cx) * (decos[d].x - cx) + (decos[d].y - cy) * (decos[d].y - cy);
      crowded = q < (decos[d].kind == DECO_CHAINS ? 16 : 1.45);
    }
    if (crowded) continue;
    c->used = true;
    add_deco(lv, cx, cy, 0, 0, DECO_CHAINS);
    chains++;
  }

  // Niches: a wall cell carved back to half its depth ("N"), with a statue
  // or an urn on display in the recess. The renderer draws the sunken face
  // door-style and the flanking walls provide real jambs; the piece inside
  // is an ordinary billboard that the z-buffer clips to the opening. Only
  // cells carrying nothing else on any face give up their masonry, so a
  // torch, box or hanging can never end up floating in front of a recess.
  // Any masonry will do for the backing: the carved face replaces the
  // cell's own texture entirely.
  int niche_limit = (int)round_limit(1, 4, (double)cells / 180);
  int niche_x[8], niche_y[8], niches = 0;
  for (int i = 0; i < cand_count && niches < niche_limit; i++) {
    BoxCandidate *c = &cands[i];
    if (c->used) continue;
    if (grid[c->y * w + c->x] != c->cell) continue; // another face already carved it
    bool taken = false;
    for (int k = 0; k < lv->wall_box_count && !taken; k++) taken = boxes[k].x == c->x && boxes[k].y == c->y;
    for (int k = 0; k < lv->hanging_count && !taken; k++) taken = hangings[k].x == c->x && hangings[k].y == c->y;
    if (taken) continue;
    if (near_torch(lv, c->x + 0.5, c->y + 0.5, 0.9)) continue;
    double cx = c->x + 0.5 + c->dx * 0.5, cy = c->y + 0.5 + c->dy * 0.5; // centre of the opening
    if (near_deco(lv, cx, cy, 1.45)) continue;
    bool close = false;
    for (int k = 0; k < niches && !close; k++)
      close = (niche_x[k] - c->x) * (niche_x[k] - c->x) + (niche_y[k] - c->y) * (niche_y[k] - c->y) < 9;
    if (close) continue;
    c->used = true;
    grid[c->y * w + c->x] = 'N';
    niche_x[niches] = c->x;
    niche_y[niches] = c->y;
    niches++;
    static const DecoKind pieces[3] = {DECO_STATUE, DECO_URN, DECO_CANDLES};
    // in the recess, in front of the sunken face
    add_deco(lv, c->x + 0.5 + c->dx * 0.25, c->y + 0.5 + c->dy * 0.25, 0, 0, pieces[(c->hash >> 4) % 3]);
  }
  free(cands);

  if (!has_player) {
    snprintf(err, err_size, "Level has no player start (P)");
    level_free(lv);
    return false;
  }

  snprintf(lv->name, sizeof lv->name, "%s", props.name);
  lv->floor_color = color_parse(props.floor);
  lv->ceil_color = color_parse(props.ceil);
  lv->start_x = player_x;
  lv->start_y = player_y;
  lv->start_dx = 1;
  lv->start_dy = 0;
  if (!strcmp(props.start, "N")) {
    lv->start_dx = 0;
    lv->start_dy = -1;
  } else if (!strcmp(props.start, "S")) {
    lv->start_dx = 0;
    lv->start_dy = 1;
  } else if (!strcmp(props.start, "W")) {
    lv->start_dx = -1;
    lv->start_dy = 0;
  }

  for (int i = 0; i < lv->lever_count; i++)
    if (lv->levers[i].door_count > 0) lv->secret_count++;
  for (int i = 0; i < lv->item_count; i++) {
    if (lv->items[i].kind == ITEM_KEY) lv->key_count++;
    if (lv->items[i].kind == ITEM_TREASURE) lv->treasure_count++;
  }
  // Preserve the stable dressing layout, then remove anything standing on
  // stair treads. Moving rejected pieces elsewhere could block an old route.
  int kept = 0;
  for (int i = 0; i < lv->decoration_count; i++) {
    Decoration d = decos[i];
    bool stairs = false;
    for (int dy = -1; dy <= 1; dy++)
      for (int dx = -1; dx <= 1; dx++)
        if (on_steps(lv, (int)floor(d.x + dx * d.radius), (int)floor(d.y + dy * d.radius))) stairs = true;
    if (d.radius && stairs) continue;
    decos[kept++] = d;
  }
  lv->decoration_count = kept;
  kept = 0;
  for (int i = 0; i < lv->wall_box_count; i++) {
    WallBox box = lv->wall_boxes[i];
    if (on_steps(lv, box.x + box.dx, box.y + box.dy) || on_steps(lv, box.x, box.y)) continue;
    lv->wall_boxes[kept++] = box;
  }
  lv->wall_box_count = kept;
  if (!horror_configure(lv, text, err, err_size)) {
    level_free(lv);
    return false;
  }
  if (lv->world.monk_count && (!lv->horror.chapter || lv->horror.note_count + lv->world.monk_count > HORROR_NOTES)) {
    snprintf(err, err_size, "Monks require a horror chapter and available dialogue slots");
    level_free(lv); return false;
  }
  for (int i = 0; i < lv->world.monk_count; i++) {
    Monk *m = &lv->world.monks[i];
    m->note = lv->horror.note_count++;
    HorrorNote *note = &lv->horror.notes[m->note];
    note->x = m->x; note->y = m->y;
    snprintf(note->text, sizeof note->text, "%s", m->text);
  }
  for (int i = 0; i < lv->world.prop_count; i++) {
    WorldProp *p = &lv->world.props[i];
    add_deco(lv, p->x + .5, p->y + .5, world_prop_radius(p->kind), 0, p->kind);
  }
  // hearths feed the wall/floor light model exactly like torches do
  lv->fire_lights = malloc(sizeof(Torch) * (fireplaces + 1));
  for (int d = 0; d < lv->decoration_count; d++) {
    if (decos[d].kind == DECO_FIREPLACE)
      lv->fire_lights[lv->fire_light_count++] = (Torch){decos[d].x, decos[d].y, decos[d].phase};
  }
  return true;
}

void level_free(Level *lv) {
  free(lv->world.cells);
  free(lv->grid);
  free(lv->heights);
  free(lv->ceilings);
  free(lv->items);
  free(lv->enemies);
  free(lv->barrels);
  free(lv->doors);
  free(lv->traps);
  free(lv->pendulums);
  free(lv->levers);
  free(lv->portals);
  free(lv->torches);
  free(lv->fire_lights);
  free(lv->decorations);
  free(lv->wall_boxes);
  free(lv->hangings);
  memset(lv, 0, sizeof *lv);
}
