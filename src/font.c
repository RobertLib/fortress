// Procedural serif stroke font. See font.h.
//
// Every glyph is a short drawing program on a design grid of 100 units per
// em, y up, baseline at 0: cap height 69, x-height 48, ascenders 74,
// descenders -22. Curves are traced with a broad elliptical pen, wider than
// tall and tilted a little, so vertical strokes come out heavy and horizontal
// ones light; stems, bars, diagonals and serifs are flat-cut shapes. On first
// use at a given size and weight a glyph is rasterised into an 8-bit coverage
// mask and cached. Each primitive is evaluated as a signed distance to its
// edge, turned into anti-aliased coverage and merged by maximum (a union).
// Below HINT_FULL px a small auto-hinter rounds pen widths to whole pixels
// and snaps stems, bars and curve extremes to the pixel grid; each glyph then
// keeps its design side bearings rounded to whole pixels, figures share one
// width, and neighbours are nudged apart where they would touch. At every
// size the baseline, x-height, cap height, ascender and descender land on
// pixel boundaries.
//
// Drawing language (numbers separated by spaces or commas; widths of stems,
// dots and diagonals are in stems, bar thicknesses in hairlines, so the same
// program serves both weights):
//   M x y              move the pen          L x y              line
//   Q x1 y1 x y        quadratic curve       C x1 y1 x2 y2 x y  cubic curve
//   A cx cy rx ry a0 a1 [e]   elliptical arc in degrees (a1 < a0 runs
//                      clockwise) starting a new stroke; 'a' continues the
//                      current one. e > 2 squares the ellipse off.
//   O cx cy rx ry [e]  closed ellipse
//   k f [g]            pen scale: f for vertical strokes, g for horizontal
//   D x y r            dot of radius r (in stems) centred on (x, y)
//   o x y r            dot resting on y
//   | x y0 y1 [k]      vertical stem
//   = x0 x1 y [k]      horizontal bar centred on y
//   ^ x0 x1 y [k]      bar hanging from y     _ x0 x1 y [k]   bar resting on y
//   / x0 y0 x1 y1 [k0 [k1]]  straight stroke cut level at both ends, k0 and
//                      k1 thick at either end
//   F l r, H l r       bracketed foot / head serif on the last stem or
//                      stroke, reaching l units left and r right of it
//   W l                sloped lowercase head serif on the last stem
//   V x y dy w0 w1     beak serif: a wedge whose outer edge runs along x from
//                      y to y + dy, w0 wide at y and w1 at the tip (negative
//                      widths put it right of x)
//   P x y ...          convex polygon

#include "font.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

// ------------------------------------------------------------------ metrics

#define UNITS 100.0f   // design units per em
#define HINT_FULL 25.0f // below this em size glyphs are fitted to the pixel grid
#define PEN_TILT 0.14f // radians the broad axis of the pen is raised
#define UNKNOWN_ADV 40.0f

static const float BLUE_ZONES[] = {0.0f, 48.0f, 69.0f, 74.0f, -22.0f};

typedef struct Weight {
  float stem;    // vertical stroke width
  float hair;    // horizontal stroke thickness
  float serif;   // serif slab thickness
  float bracket; // serif bracket radius
  float trim;    // sidebearing taken off each side, relative to bold
} Weight;

static const Weight WEIGHTS[2] = {
  [FONT_REGULAR] = {9.0f, 4.0f, 4.0f, 3.0f, 2.2f},
  [FONT_BOLD] = {14.5f, 6.0f, 5.5f, 4.0f, 0.0f},
};

// ------------------------------------------------------------------ glyphs

typedef struct GlyphDef {
  uint32_t cp;
  float adv;
  const char *prog;
} GlyphDef;

static const GlyphDef GLYPHS[] = {
  {' ', 26, ""},
  {'!', 33, "/16.5 69 16.5 23 .95 .5 o16.5 0 .58"},
  {'"', 44, "/13 74 13 49 .72 .38 /31 74 31 49 .72 .38"},
  {'#', 68, "/21 0 30 69 .45 /41 0 50 69 .45 =7 62 23 1.1 =11 66 46 1.1"},
  {'$', 64, "M51.5 50 L51.5 56 C51.5 62 43 66 32 66 C21 66 14 60 14 51 C14 41 22 37 32 34 C43 30.5 51 27 51 18 "
            "C51 8 43 3 32 3 C20 3 12 7 12 12.5 L12 19 |32 -8 77 .3"},
  {'%', 86, "O20 52 10.5 14.5 O64 17 10.5 14.5 /17 0 69 69 .4"},
  {'&', 80, "M68 3 C60 3 53 6 46 13 L25 39 C19 46 16 51 16 56 C16 63 22 67 30 67 C37 67 42 63 42 57 C42 49 35 44 "
            "26 39 C16 33 9 27 9 18 C9 8 17 2 29 2 C43 2 52 13 58 30 =51 71 31"},
  {'\'', 24, "/12 74 12 49 .72 .38"},
  {'(', 38, "A47 28 31 52 121 239"},
  {')', 38, "A-9 28 31 52 59 -59"},
  {'*', 50, "k.45 1 M25 58 L25 70 M25 58 L36.4 61.7 M25 58 L32.1 48.3 M25 58 L17.9 48.3 M25 58 L13.6 61.7 "
            "D25 69 .32 D35.5 61.4 .32 D31.5 49.2 .32 D18.5 49.2 .32 D14.5 61.4 .32"},
  {'+', 64, "|32 8 52 .55 =9 55 30 1.25"},
  {',', 26, "o13 0 .58 M19 5 C19 -4 14 -10 6 -14"},
  {'-', 36, "=5 31 28.5 1.45"},
  {'.', 26, "o13 0 .58"},
  {'/', 40, "/5 -12 35 74 .5"},
  {'0', 64, "O32 34.5 20.5 32.7 2.3"},
  {'1', 64, "|33 0 69 F11 11 P40.25 69 27 69 13 57.5 15.5 55 40.25 62"},
  {'2', 64, "M19 60 C22 64.5 26.5 67 32 67 C43 67 50 60 50 50 C50 38 41 30 29 21 L16.5 6 _9.5 55 0 1.25 V55 0 17 7 5 "
            "D17.5 54 .55"},
  {'3', 64, "M19 60 C22 64.5 26 67 31 67 C42 67 48 61 48 52 C48 43 41 38 29 38 L25 38 M29 38 C44 38 51 30 51 20 "
            "C51 9 42 2 30 2 C23 2 18.5 4 15.5 8 D17 54 .55 D15.5 14 .55"},
  {'4', 64, "|44 0 69 F10 10 /44 69 10.5 21 .45 =6 59 20 1.1"},
  {'5', 64, "^20 55 69 V55 69 -14 7 5 /20 69 18 38 .75 M20 42 C25 44 29 45 35 45 C46 45 52 37 52 24 C52 10 43 2 "
            "31 2 C21 2 14 6 11 12 D17 13 .5"},
  {'6', 64, "M50 60 C46 65 40 67 35 67 C20 67 12 53 12 33 O32 22 20 20 D47 59 .5"},
  {'7', 64, "^9 56 69 V9 69 -15 -7 -5 M55 66 C45 50 35 30 31 3"},
  {'8', 64, "O32 51.5 16 15.5 O32 20.5 19 18.5"},
  {'9', 64, "O32 47 20 20 M14 9 C18 4 24 2 29 2 C44 2 52 16 52 36 D17 10 .5"},
  {':', 28, "o14 0 .58 o14 31.5 .58"},
  {';', 28, "o14 31.5 .58 o14 0 .58 M20 5 C20 -4 15 -10 7 -14"},
  {'<', 64, "k.55 1 M52 52 L12 30 L52 8"},
  {'=', 64, "=9 55 38 1.15 =9 55 22 1.15"},
  {'>', 64, "k.55 1 M12 52 L52 30 L12 8"},
  {'?', 52, "M12 55 C13 64 20 67 28 67 C38 67 45 62 45 53 C45 44 37 40 31 36 C27 33 26 30 26 23 D16 55 .5 "
            "o26 0 .56"},
  {'@', 94, "O43 29 12 14 |56 16 44 .6 M56 22 C56 14 59 12 64 12 C73 12 81 20 81 33 C81 52 67 64 47 64 C27 64 12 50 "
            "12 30 C12 10 27 -6 48 -6 C57 -6 64 -4 70 -1"},
  {'A', 78, "/13 0 35.5 69 .5 F8 8 /61.5 0 39 69 1.1 .85 F8 8 =20 54.5 22 1.1"},
  {'B', 70, "|17 0 69 F8 0 H8 0 M17 66 L37 66 a37 51.5 16 14.5 90 -90 L17 37 A38 20 20 17 90 -90 L17 3"},
  {'C', 70, "A38 34.5 27.25 32.7 52 312 V62.5 66 -19 8 4.5"},
  {'D', 76, "|17 0 69 F8 0 H8 0 M17 66 L35 66 a35 34.5 29 31.5 90 -90 L17 3"},
  {'E', 68, "|17 0 69 F8 0 H8 0 ^17 59 69 _17 63 0 =17 51 36 .95 V59 69 -18 9 6 V63 0 20 10 6 "
            "P47 29 51 27.5 51 44.5 47 43"},
  {'F', 64, "|17 0 69 F8 9 H8 0 ^17 59 69 =17 49 35 .95 V59 69 -18 9 6 P45 28 49 26.5 49 43.5 45 42"},
  {'G', 77, "A39 34.5 28.25 32.7 52 318 V63.5 66 -19 8 4.5 |63 3 31 H11 5"},
  {'H', 84, "|17 0 69 F8 7 H8 7 |67 0 69 F7 8 H7 8 =17 67 36"},
  {'I', 40, "|20 0 69 F8 8 H8 8"},
  {'J', 52, "|35 20 69 H8 8 A21 20 14 18 0 -140 D10.5 11 .6"},
  {'K', 78, "|17 0 69 F8 7 H8 7 /17 17 57 69 .5 H8 7 /36 42 59 0 .5 1.1 F7 8"},
  {'L', 64, "|17 0 69 F8 0 H8 8 _17 60 0 V60 0 20 10 6"},
  {'M', 96, "|14 0 69 .55 F8 8 H8 0 /14 69 46.5 0 1.05 .7 /46.5 0 78 69 .5 |78 0 69 F8 8 H0 8"},
  {'N', 79, "|15 0 69 .55 F8 8 H8 0 /15 69 64 0 1.05 .55 |64 0 69 .55 H8 8"},
  {'O', 78, "O39 34.5 28.25 32.7 2.3"},
  {'P', 67, "|17 0 69 F8 9 H8 0 M17 66 L37 66 a37 49.5 20 16.5 90 -90 L17 33"},
  {'Q', 78, "O39 34.5 28.25 32.7 2.3 k.9 1 M37 1 C48 -3 55 -8 66 -15"},
  {'R', 76, "|17 0 69 F8 8 H8 0 M17 66 L36 66 a36 50.5 16 15.5 90 -90 L17 35 /32 37 61 0 .9 1.05 F3 7"},
  {'S', 62, "M52.5 51 L52.5 57 C52.5 63 44 67 32 67 C20 67 12 60 12 51 C12 41 20 37 31 33.5 C43 29.5 52 26 52 17 "
            "C52 7 43 2 31 2 C18 2 10 6 10 12 L10 19"},
  {'T', 66, "|33 0 69 F9 9 ^4 62 69 V4 69 -18 -9 -6 V62 69 -18 9 6"},
  {'U', 80, "|16 26 69 H8 8 A40 26 24 24 180 300 k.55 A40 26 24 24 290 360 |64 0 69 .55 H8 8 F0 5"},
  {'V', 76, "/17 69 39.5 0 1.1 .6 H8 8 /39.5 0 63 69 .5 H8 8"},
  {'W', 102, "/15 69 33 0 1 .6 H7 6 /33 0 53 69 .5 /51 69 71 0 1 .6 H6 6 /71 0 91 69 .5 H6 7"},
  {'X', 76, "/18 69 58 0 1 1 H8 7 F7 8 /18 0 58 69 .5 .5 F8 7 H7 8"},
  {'Y', 72, "/17 69 37 34 1 .8 H8 7 /37 34 58 69 .5 H7 8 |37 0 36 F9 9"},
  {'Z', 66, "^9 60 69 _4 60 0 /51 66 14 3 1.05 V9 69 -16 -8 -5.5 V60 0 18 8 5.5"},
  {'[', 40, "|16 -16 74 .95 ^16 36 74 _16 36 -16"},
  {'\\', 40, "/5 74 35 -12 .5"},
  {']', 40, "|24 -16 74 .95 ^4 24 74 _4 24 -16"},
  {'^', 64, "k.6 1 M13 40 L32 67 L51 40"},
  {'_', 64, "_0 64 -16"},
  {'`', 50, "k.6 1 M19 71 L31 59"},
  {'a', 55, "|41 0 31 F0 6 M41 30 C41 41.5 36 45.8 27.5 45.8 C22 45.8 18.5 44.5 16.5 42 D18.5 38.5 .52 "
            "M41 26 C26 26 10.5 22 10.5 12.5 C10.5 5.5 15.5 2.2 23 2.2 C31 2.2 37 6 41 13"},
  {'b', 61, "|15 0 74 W7 M15 34 C19 42 26 45.8 33 45.8 C44 45.8 51 37 51 24 C51 10 43 2.2 32 2.2 C24 2.2 19 4 15 8"},
  {'c', 50, "A29 24 18.5 21.8 42 318 D41.5 36.5 .5"},
  {'d', 61, "|46 0 74 W7 F0 6.5 M46 36 C42 43 36 45.8 29 45.8 C18 45.8 10 37 10 24 C10 11 17 2.2 28 2.2 C36 2.2 42 6 "
            "46 12"},
  {'e', 53, "=10 45 27 |45.3 24 28 A28 24 17.5 21.8 8 325"},
  {'f', 36, "|15 0 57 F6.5 9 M15 56 C15 67 21 71.8 29 71.8 C31 71.8 32.5 71.3 33.5 70.5 D34 66.5 .52 ^4 34 48"},
  {'g', 56, "O25 32.5 11.5 12.5 M31 44.8 Q40 46.3 49 46.3 k.5 M48.5 46.3 L48.5 42.5 "
            "k.6 M18 22 C13.5 19 13 14.5 16.5 12.8 C19 11.6 22 11.3 26 11.3 L37 11.3 "
            "k1 M37 11.3 C45.5 11.3 49 6 49 -3.5 C49 -13 40 -19.2 28.5 -19.2 C15.5 -19.2 8 -14 8 -7.5 "
            "C8 -2 11 3 15.5 7"},
  {'h', 63, "|15 0 74 W7 F6.5 6.5 M15 35 C20 42 26 45.8 33 45.8 C43 45.8 49 40 49 31 |49 0 31 F6.5 6.5"},
  {'i', 32, "|16 0 48 W7 F6.5 6.5 D16 63 .58"},
  {'j', 31, "|18 -8 48 W7 M18 -6 C18 -15 14 -19.2 8 -19.2 C6 -19.2 4.5 -18.8 3.5 -18 D4 -14 .52 D18 63 .58"},
  {'k', 59, "|15 0 74 W7 F6.5 6.5 /15 12 44 48 .45 H6 6 /28 28 50 0 .45 .95 F5 6.5"},
  {'l', 31, "|16 0 74 W7 F6.5 6.5"},
  {'m', 94, "|15 0 48 W7 F6.5 6.5 M15 35 C19 42 24 45.8 30.5 45.8 C39 45.8 47 41 47 31 |47 0 31 F6.5 6.5 M47 35 "
            "C51 42 56 45.8 62.5 45.8 C71 45.8 79 41 79 31 |79 0 31 F6.5 6.5"},
  {'n', 64, "|15 0 48 W7 F6.5 6.5 M15 35 C20 42 26 45.8 33 45.8 C43 45.8 49 40 49 31 |49 0 31 F6.5 6.5"},
  {'o', 58, "O29 24 18.75 21.8 2.3"},
  {'p', 61, "|15 -22 48 W7 F6.5 6.5 M15 36 C19 43 26 45.8 33 45.8 C44 45.8 51 37 51 24 C51 10 43 2.2 32 2.2 C24 2.2 "
            "19 5 15 10"},
  {'q', 60, "|46 -22 48 F6.5 6.5 M46 36 C42 43 36 45.8 29 45.8 C18 45.8 10 37 10 24 C10 11 17 2.2 28 2.2 C36 2.2 42 "
            "6 46 12"},
  {'r', 46, "|15 0 48 W7 F6.5 10 M15 34 C20 42 26 45.8 32 45.8 C34 45.8 35.5 45.4 36.5 44.8 D37 40 .52"},
  {'s', 47, "M39.5 34 L39.5 38.5 C39.5 43 33 45.8 25 45.8 C16 45.8 11 41.5 11 35.5 C11 22 39 27 39 12.5 C39 5.5 33 "
            "2.2 24 2.2 C15 2.2 9 5 9 10 L9 14.5"},
  {'t', 36, "|15 12 52 P7.75 50 22.25 50 22.25 61 7.75 53 ^3 34 48 M15 12 C15 5 19 2.2 25 2.2 C30 2.2 33 4 35 7"},
  {'u', 63, "|15 17 48 W7 M15 18 C15 8 20 2.2 29 2.2 C37 2.2 43 6 48.5 13 |48.5 0 48 W7 F0 6.5"},
  {'v', 56, "/15 48 30 0 1 .6 H6 6 /30 0 45 48 .45 H6 6"},
  {'w', 80, "/15 48 28 0 1 .6 H6 5 /28 0 42 48 .45 /41 48 55 0 1 .6 /55 0 69 48 .45 H5 6"},
  {'x', 59, "/15 48 43 0 1 1 H6 6 F6 6 /15 0 43 48 .45 .45 F6 6 H6 6"},
  {'y', 56, "/14 48 30 2 1 .55 H6 6 /46 48 25.4 -10 .45 H6 6 M25.8 -9 C24 -16 19.5 -19.2 14 -19.2 D10 -15 .52"},
  {'z', 50, "^9 42 48 _7 43 0 /34 45 15 3 1 V9 48 -12 -6 -4 V43 0 13 6 4"},
  {'{', 44, "k.8 1 M36 74 C26 74 23 70 23 60 L23 40 C23 32 19 28 11 28 C19 28 23 24 23 16 L23 -6 "
            "C23 -14 26 -18 36 -18"},
  {'|', 36, "|18 -22 76 .55"},
  {'}', 44, "k.8 1 M8 74 C18 74 21 70 21 60 L21 40 C21 32 25 28 33 28 C25 28 21 24 21 16 L21 -6 "
            "C21 -14 18 -18 8 -18"},
  {'~', 64, "k.8 1 M10 27 C14 35 20 37 26 34 C32 31 38 27 44 27 C50 27 53 30 55 34"},
  {0x2013, 50, "=1 49 28.5 1.2"},
  {0x2014, 100, "=3 97 28.5 1.2"},
  {0x2019, 26, "D13 62.5 .55 M18.5 60 C18.5 52 14 47 6 44"},
  {0x2026, 90, "o15 0 .56 o45 0 .56 o75 0 .56"},
};

// Kerning pairs in design units.
typedef struct KernPair {
  char a, b;
  signed char k;
} KernPair;

static const KernPair KERNS[] = {
  {'A', 'T', -6}, {'A', 'V', -8}, {'A', 'W', -6}, {'A', 'Y', -8}, {'A', 'O', -2}, {'A', 'C', -2}, {'A', 'G', -2},
  {'A', 'Q', -2}, {'A', 'U', -3}, {'T', 'A', -6}, {'V', 'A', -8}, {'W', 'A', -6}, {'Y', 'A', -8}, {'L', 'T', -7},
  {'L', 'V', -8}, {'L', 'W', -6}, {'L', 'Y', -8}, {'F', 'A', -5}, {'P', 'A', -6}, {'T', 'O', -2}, {'O', 'T', -2},
  {'Y', 'O', -3}, {'O', 'Y', -3}, {'V', 'O', -2}, {'O', 'V', -2}, {'R', 'T', -2}, {'R', 'Y', -4}, {'R', 'V', -3},
  {'R', 'W', -2}, {'T', ',', -8}, {'T', '.', -8}, {'V', ',', -8}, {'V', '.', -8}, {'W', ',', -6}, {'W', '.', -6},
  {'Y', ',', -8}, {'Y', '.', -8}, {'P', ',', -8}, {'P', '.', -8}, {'F', ',', -6}, {'F', '.', -6}, {'T', 'o', -6},
  {'T', 'e', -6}, {'T', 'a', -6}, {'T', 'r', -4}, {'T', 'y', -4}, {'T', 'u', -4}, {'Y', 'o', -6}, {'V', 'o', -4},
  {'W', 'o', -3}, {'r', '.', -5}, {'r', ',', -5}, {'y', '.', -4}, {'v', '.', -4}, {'w', '.', -3},
};

static const GlyphDef *glyph_def(uint32_t cp) {
  static const GlyphDef *ascii[128];
  static bool indexed = false;
  if (!indexed) {
    for (size_t i = 0; i < sizeof GLYPHS / sizeof GLYPHS[0]; i++) {
      if (GLYPHS[i].cp < 128) ascii[GLYPHS[i].cp] = &GLYPHS[i];
    }
    indexed = true;
  }
  if (cp < 128) return ascii[cp];
  for (size_t i = 0; i < sizeof GLYPHS / sizeof GLYPHS[0]; i++) {
    if (GLYPHS[i].cp == cp) return &GLYPHS[i];
  }
  return NULL;
}

static float kern_units(uint32_t a, uint32_t b) {
  if (a >= 128 || b >= 128) return 0;
  for (size_t i = 0; i < sizeof KERNS / sizeof KERNS[0]; i++) {
    if ((uint32_t)KERNS[i].a == a && (uint32_t)KERNS[i].b == b) return KERNS[i].k;
  }
  return 0;
}

// Decodes one UTF-8 sequence; malformed input yields U+FFFD and moves on.
static uint32_t utf8_next(const char **ps) {
  const unsigned char *s = (const unsigned char *)*ps;
  uint32_t c = s[0];
  int extra = 0;
  if (c < 0x80) {
    extra = 0;
  } else if ((c & 0xe0) == 0xc0) {
    c &= 0x1f;
    extra = 1;
  } else if ((c & 0xf0) == 0xe0) {
    c &= 0x0f;
    extra = 2;
  } else if ((c & 0xf8) == 0xf0) {
    c &= 0x07;
    extra = 3;
  } else {
    *ps += 1;
    return 0xfffd;
  }
  for (int i = 1; i <= extra; i++) {
    if ((s[i] & 0xc0) != 0x80) {
      *ps += i;
      return 0xfffd;
    }
    c = (c << 6) | (s[i] & 0x3f);
  }
  *ps += 1 + extra;
  return c;
}

// ------------------------------------------------------------------ sizing

static int size_key(float size) { return (int)lroundf(size * 8.0f); }

static bool grid_fitted(int sq) { return (float)sq / 8.0f < HINT_FULL; }

static float kern_px(uint32_t a, uint32_t b, int sq) {
  float k = kern_units(a, b);
  if (k == 0) return 0;
  float px = k * (float)sq / 8.0f / UNITS;
  return grid_fitted(sq) ? roundf(px) : px;
}

// ------------------------------------------------------------------ primitives

typedef enum { PRIM_SEG, PRIM_POLY, PRIM_FILLET } PrimKind;

#define POLY_MAX 8

typedef struct Prim {
  PrimKind kind;
  int n;                    // polygon vertices
  float p[POLY_MAX * 2];    // seg: a, b; poly: vertices; fillet: corner, directions, radii
  float m[4];               // seg: pixel offsets to unit pen space
  float gmax;               // seg: largest pen-space distance change per pixel
  float ex;                 // seg: horizontal half-extent of the pen
  float bx0, by0, bx1, by1; // pixel bounds including the anti-aliasing fringe
} Prim;

static Prim *prims;
static int prim_count, prim_cap;

static Prim *prim_new(PrimKind kind) {
  if (prim_count == prim_cap) {
    prim_cap = prim_cap ? prim_cap * 2 : 256;
    prims = realloc(prims, (size_t)prim_cap * sizeof(Prim));
  }
  Prim *p = &prims[prim_count++];
  memset(p, 0, sizeof *p);
  p->kind = kind;
  return p;
}

// Coverage buffer: pixel (i, j) spans [ox + i, ox + i + 1) x [oy + j, oy + j + 1).
typedef struct Cover {
  int ox, oy, w, h;
  float *cov;
} Cover;

static float *cover_buf;
static size_t cover_cap;

static void prim_span(const Cover *cv, const Prim *p, int *i0, int *j0, int *i1, int *j1) {
  *i0 = (int)floorf(p->bx0) - cv->ox;
  *j0 = (int)floorf(p->by0) - cv->oy;
  *i1 = (int)ceilf(p->bx1) - cv->ox;
  *j1 = (int)ceilf(p->by1) - cv->oy;
  if (*i0 < 0) *i0 = 0;
  if (*j0 < 0) *j0 = 0;
  if (*i1 > cv->w) *i1 = cv->w;
  if (*j1 > cv->h) *j1 = cv->h;
}

static inline void cover(float *c, float v) {
  if (v > *c) *c = v > 1.0f ? 1.0f : v;
}

// Segment swept by an elliptical pen. The pen matrix maps it to a unit
// circle, where the distance to the swept shape is simply |q - spine| - 1;
// dividing by the gradient turns that into an approximate pixel distance.
static void raster_seg(const Cover *cv, const Prim *pr) {
  const float *m = pr->m;
  float ax = pr->p[0], ay = pr->p[1];
  float dx = pr->p[2] - ax, dy = pr->p[3] - ay;
  float ux = m[0] * dx + m[1] * dy, uy = m[2] * dx + m[3] * dy;
  float uu = ux * ux + uy * uy;
  float inv_uu = uu > 1e-12f ? 1.0f / uu : 0.0f;
  float fnx = 1.0f, fny = 0.0f; // normal used on the spine itself
  if (uu > 1e-12f) {
    float l = sqrtf(uu);
    fnx = -uy / l;
    fny = ux / l;
  }
  float lim = 1.0f + 0.5f * pr->gmax;
  float lim2 = lim * lim;
  int i0, j0, i1, j1;
  prim_span(cv, pr, &i0, &j0, &i1, &j1);
  for (int j = j0; j < j1; j++) {
    float cy = (float)(cv->oy + j) + 0.5f - ay;
    float *row = cv->cov + (size_t)j * cv->w;
    for (int i = i0; i < i1; i++) {
      float cx = (float)(cv->ox + i) + 0.5f - ax;
      float qx = m[0] * cx + m[1] * cy, qy = m[2] * cx + m[3] * cy;
      float t = (qx * ux + qy * uy) * inv_uu;
      t = t < 0.0f ? 0.0f : t > 1.0f ? 1.0f : t;
      float rx = qx - t * ux, ry = qy - t * uy;
      float d2 = rx * rx + ry * ry;
      if (d2 >= lim2) continue;
      float d = sqrtf(d2), nx = fnx, ny = fny;
      if (d > 1e-4f) {
        nx = rx / d;
        ny = ry / d;
      }
      float gx = m[0] * nx + m[2] * ny, gy = m[1] * nx + m[3] * ny;
      cover(&row[i], 0.5f - (d - 1.0f) / sqrtf(gx * gx + gy * gy));
    }
  }
}

// Convex polygon: the signed distance is the largest distance to any edge
// line, which keeps corners sharp.
static void raster_poly(const Cover *cv, const Prim *pr) {
  int n = pr->n;
  float nx[POLY_MAX], ny[POLY_MAX], nc[POLY_MAX];
  float area = 0;
  for (int k = 0; k < n; k++) {
    int l = (k + 1) % n;
    area += pr->p[k * 2] * pr->p[l * 2 + 1] - pr->p[l * 2] * pr->p[k * 2 + 1];
  }
  float sign = area > 0 ? 1.0f : -1.0f;
  int e = 0;
  for (int k = 0; k < n; k++) {
    int l = (k + 1) % n;
    float ex = pr->p[l * 2] - pr->p[k * 2], ey = pr->p[l * 2 + 1] - pr->p[k * 2 + 1];
    float len = sqrtf(ex * ex + ey * ey);
    if (len < 1e-6f) continue;
    // outward normal of a counterclockwise (in y-down pixels: clockwise) ring
    nx[e] = ey / len * sign;
    ny[e] = -ex / len * sign;
    nc[e] = nx[e] * pr->p[k * 2] + ny[e] * pr->p[k * 2 + 1];
    e++;
  }
  if (e < 3) return;
  int i0, j0, i1, j1;
  prim_span(cv, pr, &i0, &j0, &i1, &j1);
  for (int j = j0; j < j1; j++) {
    float cy = (float)(cv->oy + j) + 0.5f;
    float *row = cv->cov + (size_t)j * cv->w;
    for (int i = i0; i < i1; i++) {
      float cx = (float)(cv->ox + i) + 0.5f;
      float sd = -1e9f;
      for (int k = 0; k < e; k++) {
        float v = nx[k] * cx + ny[k] * cy - nc[k];
        if (v > sd) sd = v;
      }
      cover(&row[i], 0.5f - sd);
    }
  }
}

// Serif bracket: the concave fill between a stem and a slab, a box with an
// ellipse taken out of it. The box reaches back into the stem and slab a
// little so the seams stay solid.
static void raster_fillet(const Cover *cv, const Prim *pr) {
  float kx = pr->p[0], ky = pr->p[1], sx = pr->p[2], sy = pr->p[3], rx = pr->p[4], ry = pr->p[5];
  float shear = pr->p[6]; // the wall leans with the stem edge: dx per pixel down
  float ov = 1.0f;
  float xa = kx - sx * ov, xb = kx + sx * rx, ya = ky - sy * ov, yb = ky + sy * ry;
  float bcx = (xa + xb) * 0.5f, bhx = fabsf(xb - xa) * 0.5f;
  float bcy = (ya + yb) * 0.5f, bhy = fabsf(yb - ya) * 0.5f;
  float ecx = kx + sx * rx, ecy = ky + sy * ry;
  int i0, j0, i1, j1;
  prim_span(cv, pr, &i0, &j0, &i1, &j1);
  for (int j = j0; j < j1; j++) {
    float cy = (float)(cv->oy + j) + 0.5f;
    float *row = cv->cov + (size_t)j * cv->w;
    float lean = shear * (cy - ky);
    for (int i = i0; i < i1; i++) {
      float cx = (float)(cv->ox + i) + 0.5f - lean;
      float sd = fmaxf(fabsf(cx - bcx) - bhx, fabsf(cy - bcy) - bhy);
      float ex = (cx - ecx) / rx, ey = (cy - ecy) / ry;
      float g = sqrtf(ex * ex + ey * ey);
      if (g > 1e-6f) {
        float gx = ex / rx, gy = ey / ry;
        float de = (g - 1.0f) * g / sqrtf(gx * gx + gy * gy);
        if (-de > sd) sd = -de;
      } else {
        sd = 1e9f;
      }
      cover(&row[i], 0.5f - sd);
    }
  }
}

// ------------------------------------------------------------------ hinting

#define MAX_ANCHORS 48

// Piecewise-linear map from design units to pixels through snapped anchors.
typedef struct Axis {
  int n;
  float d[MAX_ANCHORS], p[MAX_ANCHORS];
} Axis;

static void axis_add(Axis *a, float d, float p) {
  if (a->n < MAX_ANCHORS) {
    a->d[a->n] = d;
    a->p[a->n] = p;
    a->n++;
  }
}

// Sorts the anchors and drops any that would fold or pinch the map.
static void axis_finish(Axis *a) {
  for (int i = 1; i < a->n; i++) {
    float d = a->d[i], p = a->p[i];
    int j = i - 1;
    while (j >= 0 && a->d[j] > d) {
      a->d[j + 1] = a->d[j];
      a->p[j + 1] = a->p[j];
      j--;
    }
    a->d[j + 1] = d;
    a->p[j + 1] = p;
  }
  int n = 0;
  for (int i = 0; i < a->n; i++) {
    if (n > 0 && (a->d[i] - a->d[n - 1] < 0.3f || a->p[i] <= a->p[n - 1])) continue;
    a->d[n] = a->d[i];
    a->p[n] = a->p[i];
    n++;
  }
  a->n = n;
}

static float axis_map(const Axis *a, float v, float s) {
  int n = a->n;
  if (n == 0) return v * s;
  if (v <= a->d[0]) return a->p[0] + (v - a->d[0]) * s;
  if (v >= a->d[n - 1]) return a->p[n - 1] + (v - a->d[n - 1]) * s;
  int i = 1;
  while (a->d[i] < v) i++;
  float t = (v - a->d[i - 1]) / (a->d[i] - a->d[i - 1]);
  return a->p[i - 1] + t * (a->p[i] - a->p[i - 1]);
}

// ------------------------------------------------------------------ programs

typedef struct Stem {
  bool ok;
  int prim;              // index of the stem's shape, or -1
  float dy0, dy1;        // design heights of the bottom and top ends
  float bx0, bx1, by;    // pixel left and right edges at the bottom end
  float tx0, tx1, ty;    // and at the top end
} Stem;

typedef struct Build {
  float s;      // pixels per design unit
  int hint;     // 1: key heights only, 2: full grid fitting
  bool collect; // first pass: gather anchors instead of drawing
  const Weight *wt;
  float stem_px, hair_px, serif_px, bracket_px;
  float kx, ky;             // pen scale
  float pm[4], pgmax;       // pen matrix for the current scale
  float pen_wv, pen_wh;     // its vertical and horizontal stroke widths
  bool open;
  float cx, cy;             // current point, design units
  Stem st;
  Axis ax, ay;
} Build;

static float px_x(const Build *b, float x) { return axis_map(&b->ax, x - b->wt->trim, b->s); }
static float px_y(const Build *b, float y) { return -axis_map(&b->ay, y, b->s); }

// Stroke widths in pixels; grid fitting rounds them to whole pixels.
static float stem_w(const Build *b, float k) {
  float w = k * b->wt->stem * b->s;
  if (b->hint < 2) return w;
  return fmaxf(1.0f, roundf(w < 1.7f ? w - 0.15f : w + 0.1f)); // the tiniest text stays open
}

static float hair_w(const Build *b, float k) {
  float w = k * b->wt->hair * b->s;
  return b->hint == 2 ? fmaxf(1.0f, roundf(w + 0.2f)) : w;
}

static float reach_px(const Build *b, float units) {
  if (units <= 0) return 0;
  float v = units * b->s;
  return b->hint == 2 ? roundf(v + 0.1f) : v;
}

static float snap(const Build *b, float v) { return b->hint == 2 ? roundf(v) : v; }

// Anchors: a stroke of pixel width w centred on design coordinate d gets its
// edges onto pixel boundaries; an edge anchor just rounds.
static void anchor_x(Build *b, float x, float w) {
  if (!b->collect || b->hint < 2) return;
  float d = x - b->wt->trim;
  axis_add(&b->ax, d, roundf(d * b->s - w * 0.5f) + w * 0.5f);
}

static void anchor_y(Build *b, float y, float w) {
  if (!b->collect || b->hint < 2) return;
  axis_add(&b->ay, y, roundf(y * b->s - w * 0.5f) + w * 0.5f);
}

static void anchor_y_edge(Build *b, float y) {
  if (!b->collect || b->hint < 2) return;
  axis_add(&b->ay, y, roundf(y * b->s));
}

static void bounds_from_points(Prim *p, const float *xy, int n, float pad) {
  p->bx0 = p->bx1 = xy[0];
  p->by0 = p->by1 = xy[1];
  for (int i = 1; i < n; i++) {
    p->bx0 = fminf(p->bx0, xy[i * 2]);
    p->bx1 = fmaxf(p->bx1, xy[i * 2]);
    p->by0 = fminf(p->by0, xy[i * 2 + 1]);
    p->by1 = fmaxf(p->by1, xy[i * 2 + 1]);
  }
  p->bx0 -= pad;
  p->by0 -= pad;
  p->bx1 += pad;
  p->by1 += pad;
}

static int emit_poly(Build *b, const float *xy, int n) {
  if (b->collect || n < 3 || n > POLY_MAX) return -1;
  Prim *p = prim_new(PRIM_POLY);
  p->n = n;
  memcpy(p->p, xy, (size_t)n * 2 * sizeof(float));
  bounds_from_points(p, xy, n, 1.0f);
  return prim_count - 1;
}

static int emit_rect(Build *b, float x0, float y0, float x1, float y1) {
  float xy[8] = {x0, y0, x1, y0, x1, y1, x0, y1};
  return emit_poly(b, xy, 4);
}

static void emit_fillet(Build *b, float kx, float ky, float sx, float sy, float rx, float ry, float shear) {
  if (b->collect || rx < 0.3f || ry < 0.3f) return;
  Prim *p = prim_new(PRIM_FILLET);
  float v[7] = {kx, ky, sx, sy, rx, ry, shear};
  memcpy(p->p, v, sizeof v);
  float xy[4] = {kx - sx, ky - sy, kx + sx * rx, ky + sy * ry};
  bounds_from_points(p, xy, 2, 1.0f + fabsf(shear) * (ry + 1.0f));
}

// Pen matrix for an ellipse whose vertical strokes come out wv wide and
// horizontal strokes wh thick, with its broad axis raised by tilt.
static void pen_matrix(float wv, float wh, float tilt, float m[4], float *gmax) {
  float ex = wv * 0.5f, ey = wh * 0.5f;
  float lim = atanf(ey / ex) * 0.85f; // keep the minor axis real
  if (tilt > lim) tilt = lim;
  float c = cosf(tilt), s = sinf(tilt);
  float c2 = c * c, s2 = s * s, den = c2 - s2;
  float a = sqrtf((ex * ex * c2 - ey * ey * s2) / den);
  float bb = sqrtf(fmaxf(1e-6f, (ey * ey * c2 - ex * ex * s2) / den));
  // broad axis (c, -s) in y-down pixels, narrow axis (s, c)
  m[0] = c / a;
  m[1] = -s / a;
  m[2] = s / bb;
  m[3] = c / bb;
  *gmax = 1.0f / fminf(a, bb);
}

static void pen_update(Build *b) {
  b->pen_wv = b->hint == 2 ? stem_w(b, b->kx) : b->kx * b->stem_px;
  b->pen_wh = b->hint == 2 ? hair_w(b, b->ky) : b->ky * b->hair_px;
  if (b->pen_wh > b->pen_wv) b->pen_wh = b->pen_wv;
  pen_matrix(b->pen_wv, b->pen_wh, b->hint == 2 ? 0.0f : PEN_TILT, b->pm, &b->pgmax);
}

static void emit_seg_px(Build *b, float x0, float y0, float x1, float y1, const float m[4], float gmax,
                        float reach) {
  if (b->collect) return;
  Prim *p = prim_new(PRIM_SEG);
  p->p[0] = x0;
  p->p[1] = y0;
  p->p[2] = x1;
  p->p[3] = y1;
  memcpy(p->m, m, 4 * sizeof(float));
  p->gmax = gmax;
  p->ex = reach;
  float xy[4] = {x0, y0, x1, y1};
  bounds_from_points(p, xy, 2, reach + 1.0f);
}

// Pen stroke between design points.
static void pen_line(Build *b, float x0, float y0, float x1, float y1) {
  emit_seg_px(b, px_x(b, x0), px_y(b, y0), px_x(b, x1), px_y(b, y1), b->pm, b->pgmax, b->pen_wv * 0.5f);
}

// A curve that meets a stem at a vertical tangent runs on a little along it,
// so the tilted pen leaves no nick beside the stem's cut end. (dx, dy) is the
// direction leaving the curve at (x, y).
static void pen_tuck(Build *b, float x, float y, float dx, float dy) {
  if (fabsf(dx) > 0.02f * fabsf(dy) || dy == 0) return;
  pen_line(b, x, y, x, y + (dy > 0 ? 2.5f : -2.5f));
}

static void emit_dot(Build *b, float x, float y, float r, bool resting) {
  if (b->collect) return;
  float rp = r * stem_w(b, 1.0f);
  float cx = px_x(b, x), cy = px_y(b, y) - (resting ? rp : 0.0f);
  if (b->hint == 2) {
    float dd = fmaxf(1.0f, roundf(rp * 2.0f));
    cx = roundf(cx - dd * 0.5f) + dd * 0.5f;
    cy = roundf(cy - dd * 0.5f) + dd * 0.5f;
    rp = dd * 0.5f + 0.2f;
  }
  float m[4] = {1.0f / rp, 0, 0, 1.0f / rp};
  emit_seg_px(b, cx, cy, cx, cy, m, 1.0f / rp, rp);
}

// Point on a (super)ellipse at angle t in radians.
static void arc_point(const float *a, float t, float *x, float *y) {
  float c = cosf(t), s = sinf(t);
  if (a[6] != 2.0f) {
    float k = 2.0f / a[6];
    c = copysignf(powf(fabsf(c), k), c);
    s = copysignf(powf(fabsf(s), k), s);
  }
  *x = a[0] + a[2] * c;
  *y = a[1] + a[3] * s;
}

// a: cx cy rx ry a0 a1 e
static void pen_arc(Build *b, const float *a, bool join) {
  float t0 = a[4] * 3.14159265f / 180.0f, t1 = a[5] * 3.14159265f / 180.0f;
  float sx, sy;
  arc_point(a, t0, &sx, &sy);
  if (b->collect) {
    // extremes of the arc become anchors
    float lo = fminf(a[4], a[5]), hi = fmaxf(a[4], a[5]);
    for (int q = (int)ceilf(lo / 90.0f - 1e-4f); q * 90.0f <= hi + 1e-3f; q++) {
      int r = ((q % 4) + 4) % 4;
      if (r == 0) anchor_x(b, a[0] + a[2], b->pen_wv);
      if (r == 2) anchor_x(b, a[0] - a[2], b->pen_wv);
      if (r == 1) anchor_y(b, a[1] + a[3], b->pen_wh);
      if (r == 3) anchor_y(b, a[1] - a[3], b->pen_wh);
    }
  } else {
    if (join && b->open) pen_line(b, b->cx, b->cy, sx, sy);
    bool closed = fabsf(a[5] - a[4]) >= 359.9f;
    float dir = a[5] > a[4] ? 1.0f : -1.0f;
    if (!closed) pen_tuck(b, sx, sy, -sinf(t0) * a[2] * -dir, cosf(t0) * a[3] * -dir);
    float rad = fmaxf(a[2], a[3]) * b->s + 1.0f;
    float step = 2.0f * acosf(fmaxf(0.0f, 1.0f - 0.08f / rad));
    if (step < 0.04f) step = 0.04f;
    if (a[6] != 2.0f) step *= 0.6f;
    int n = (int)ceilf(fabsf(t1 - t0) / step);
    if (n < 2) n = 2;
    float px = sx, py = sy;
    for (int i = 1; i <= n; i++) {
      float x, y;
      arc_point(a, t0 + (t1 - t0) * (float)i / (float)n, &x, &y);
      pen_line(b, px, py, x, y);
      px = x;
      py = y;
    }
    if (!closed) pen_tuck(b, px, py, -sinf(t1) * a[2] * dir, cosf(t1) * a[3] * dir);
  }
  arc_point(a, t1, &b->cx, &b->cy);
  b->open = true;
}

static void pen_cubic(Build *b, float x1, float y1, float x2, float y2, float x3, float y3) {
  float x0 = b->cx, y0 = b->cy;
  if (b->collect) {
    // endpoints with vertical or level tangents are local extremes
    if (fabsf(x1 - x0) < 0.01f && fabsf(y1 - y0) > 0.01f) anchor_x(b, x0, b->pen_wv);
    if (fabsf(x2 - x3) < 0.01f && fabsf(y2 - y3) > 0.01f) anchor_x(b, x3, b->pen_wv);
    if (fabsf(y1 - y0) < 0.01f && fabsf(x1 - x0) > 0.01f) anchor_y(b, y0, b->pen_wh);
    if (fabsf(y2 - y3) < 0.01f && fabsf(x2 - x3) > 0.01f) anchor_y(b, y3, b->pen_wh);
  } else {
    float len = hypotf(x1 - x0, y1 - y0) + hypotf(x2 - x1, y2 - y1) + hypotf(x3 - x2, y3 - y2);
    int n = (int)ceilf(sqrtf(len * b->s) * 1.8f);
    if (n < 2) n = 2;
    if (n > 48) n = 48;
    pen_tuck(b, x0, y0, x0 - x1, y0 - y1);
    float px = x0, py = y0;
    for (int i = 1; i <= n; i++) {
      float t = (float)i / (float)n, u = 1.0f - t;
      float w0 = u * u * u, w1 = 3 * u * u * t, w2 = 3 * u * t * t, w3 = t * t * t;
      float x = w0 * x0 + w1 * x1 + w2 * x2 + w3 * x3;
      float y = w0 * y0 + w1 * y1 + w2 * y2 + w3 * y3;
      pen_line(b, px, py, x, y);
      px = x;
      py = y;
    }
    pen_tuck(b, x3, y3, x3 - x2, y3 - y2);
  }
  b->cx = x3;
  b->cy = y3;
}

static void set_stem(Build *b, int prim, float dy0, float dy1, float bx0, float bx1, float by, float tx0, float tx1,
                     float ty) {
  Stem st = {true, prim, dy0, dy1, bx0, bx1, by, tx0, tx1, ty};
  b->st = st;
}

// Left or right edge of the last stem at pixel height y.
static float stem_edge(const Stem *st, bool right, float y) {
  float t = st->ty != st->by ? (y - st->by) / (st->ty - st->by) : 0.0f;
  float xb = right ? st->bx1 : st->bx0, xt = right ? st->tx1 : st->tx0;
  return xb + (xt - xb) * t;
}

// Slab serif across the bottom (foot) or top of the last stem, bracketed
// into it on each side it reaches out.
static void serif(Build *b, float l, float r, bool foot) {
  const Stem *st = &b->st;
  if (!st->ok) return;
  if (b->collect) {
    anchor_y_edge(b, foot ? st->dy0 : st->dy1);
    return;
  }
  float lp = reach_px(b, l), rp = reach_px(b, r), ts = b->serif_px;
  float y = foot ? st->by : st->ty;
  float inner = foot ? y - ts : y + ts; // slab edge facing the stem
  // reach is measured from the stem edges halfway through the slab, which
  // centres serifs under slanted strokes
  float mid = (y + inner) * 0.5f;
  float x0 = stem_edge(st, false, mid) - lp, x1 = stem_edge(st, true, mid) + rp;
  emit_rect(b, x0, fminf(y, inner), x1, fmaxf(y, inner));
  float br = b->bracket_px, dir = foot ? -1.0f : 1.0f;
  if (br <= 0) return;
  float dy = st->ty - st->by;
  float lean0 = dy != 0 ? (st->tx0 - st->bx0) / dy : 0.0f, lean1 = dy != 0 ? (st->tx1 - st->bx1) / dy : 0.0f;
  float k0 = stem_edge(st, false, inner), k1 = stem_edge(st, true, inner);
  // brackets never reach past the end of the slab
  if (lp > 0) emit_fillet(b, k0, inner, -1, dir, fminf((k0 - x0) * 0.8f, br), br * 1.3f, lean0);
  if (rp > 0) emit_fillet(b, k1, inner, 1, dir, fminf((x1 - k1) * 0.8f, br), br * 1.3f, lean1);
}

// Lowercase head serif: a flag sloping down to the left from the top of the
// last stem, which it replaces.
static void wedge(Build *b, float l) {
  const Stem *st = &b->st;
  if (!st->ok) return;
  if (b->collect) {
    anchor_y_edge(b, st->dy1);
    return;
  }
  float lp = reach_px(b, l), th = b->hint == 2 ? b->serif_px : b->serif_px * 1.15f;
  float xr = st->tx1, xl = st->tx0, tip = xl - lp, y = st->ty;
  float drop = b->hint == 2 ? 0.0f : 0.3f * (xr - tip);
  float xy[8] = {xr, y, tip, y + drop, tip, y + drop + th, xr, y + th};
  emit_poly(b, xy, 4);
  if (st->prim >= 0) {
    Prim *p = &prims[st->prim];
    // lower the stem's top edge to the flag's underside
    for (int k = 0; k < p->n; k++) {
      if (p->p[k * 2 + 1] < y + th - 0.25f) p->p[k * 2 + 1] = y + th - 0.25f;
    }
  }
  float under = y + th + drop * (xr - xl) / (xr - tip);
  if (b->bracket_px > 0) emit_fillet(b, xl, under, -1, 1, fminf(lp, b->bracket_px), b->bracket_px * 1.4f, 0.0f);
}

#define MAX_ARGS 32

// Reads the next command and its numbers; returns NULL at the end.
static const char *next_cmd(const char *p, char *cmd, float *args, int *n) {
  while (*p == ' ' || *p == ',') p++;
  if (!*p) return NULL;
  *cmd = *p++;
  *n = 0;
  for (;;) {
    while (*p == ' ' || *p == ',') p++;
    bool neg = *p == '-';
    const char *q = neg ? p + 1 : p;
    if (!((*q >= '0' && *q <= '9') || *q == '.')) break;
    float v = 0, scale = 1;
    bool frac = false;
    while ((*q >= '0' && *q <= '9') || (*q == '.' && !frac)) {
      if (*q == '.') {
        frac = true;
      } else if (frac) {
        scale *= 0.1f;
        v += (float)(*q - '0') * scale;
      } else {
        v = v * 10.0f + (float)(*q - '0');
      }
      q++;
    }
    if (*n < MAX_ARGS) args[(*n)++] = neg ? -v : v;
    p = q;
  }
  return p;
}

static float arg(const float *a, int n, int i, float def) { return i < n ? a[i] : def; }

static void run(Build *b, const char *prog) {
  char c;
  float a[MAX_ARGS];
  int n;
  b->kx = b->ky = 1.0f;
  b->open = false;
  b->st.ok = false;
  pen_update(b);
  while ((prog = next_cmd(prog, &c, a, &n))) {
    switch (c) {
    case 'M':
      if (n < 2) break;
      b->cx = a[0];
      b->cy = a[1];
      b->open = true;
      break;
    case 'L':
      if (n < 2) break;
      if (b->collect) {
        if (fabsf(a[0] - b->cx) < 0.01f) anchor_x(b, a[0], b->pen_wv);
        if (fabsf(a[1] - b->cy) < 0.01f) anchor_y(b, a[1], b->pen_wh);
      } else {
        pen_line(b, b->cx, b->cy, a[0], a[1]);
      }
      b->cx = a[0];
      b->cy = a[1];
      break;
    case 'Q':
      if (n < 4) break;
      pen_cubic(b, b->cx + (a[0] - b->cx) * (2.0f / 3.0f), b->cy + (a[1] - b->cy) * (2.0f / 3.0f),
                a[2] + (a[0] - a[2]) * (2.0f / 3.0f), a[3] + (a[1] - a[3]) * (2.0f / 3.0f), a[2], a[3]);
      break;
    case 'C':
      if (n < 6) break;
      pen_cubic(b, a[0], a[1], a[2], a[3], a[4], a[5]);
      break;
    case 'A':
    case 'a': {
      if (n < 6) break;
      float arc[7] = {a[0], a[1], a[2], a[3], a[4], a[5], arg(a, n, 6, 2.0f)};
      pen_arc(b, arc, c == 'a');
      break;
    }
    case 'O': {
      if (n < 4) break;
      float arc[7] = {a[0], a[1], a[2], a[3], 0.0f, 360.0f, arg(a, n, 4, 2.0f)};
      pen_arc(b, arc, false);
      b->open = false;
      break;
    }
    case 'k':
      if (n < 1) break;
      b->kx = a[0];
      b->ky = arg(a, n, 1, 1.0f);
      pen_update(b);
      break;
    case 'D':
    case 'o':
      if (n < 3) break;
      emit_dot(b, a[0], a[1], a[2], c == 'o');
      break;
    case '|': {
      if (n < 3) break;
      float w = stem_w(b, arg(a, n, 3, 1.0f));
      anchor_x(b, a[0], w);
      float xc = px_x(b, a[0]), yb = px_y(b, a[1]), yt = px_y(b, a[2]);
      int id = emit_rect(b, xc - w * 0.5f, yt, xc + w * 0.5f, yb);
      set_stem(b, id, a[1], a[2], xc - w * 0.5f, xc + w * 0.5f, yb, xc - w * 0.5f, xc + w * 0.5f, yt);
      break;
    }
    case '=':
    case '^':
    case '_': {
      if (n < 3) break;
      float t = hair_w(b, arg(a, n, 3, 1.0f));
      if (c == '=') anchor_y(b, a[2], t);
      else anchor_y_edge(b, a[2]);
      float x0 = snap(b, px_x(b, a[0])), x1 = snap(b, px_x(b, a[1])), y = px_y(b, a[2]);
      float top = c == '=' ? y - t * 0.5f : c == '^' ? y : y - t;
      emit_rect(b, x0, top, x1, top + t);
      break;
    }
    case '/': {
      if (n < 4) break;
      float k0 = arg(a, n, 4, 1.0f), k1 = arg(a, n, 5, k0);
      float x0 = px_x(b, a[0]), y0 = px_y(b, a[1]), x1 = px_x(b, a[2]), y1 = px_y(b, a[3]);
      float len = hypotf(x1 - x0, y1 - y0);
      if (len < 1e-4f) break;
      float sn = fmaxf(0.2f, fabsf(y1 - y0) / len); // level cuts widen with the slant
      float h0 = stem_w(b, k0) * 0.5f / sn, h1 = stem_w(b, k1) * 0.5f / sn;
      float xy[8] = {x0 - h0, y0, x0 + h0, y0, x1 + h1, y1, x1 - h1, y1};
      int id = emit_poly(b, xy, 4);
      if (a[1] <= a[3]) set_stem(b, id, a[1], a[3], x0 - h0, x0 + h0, y0, x1 - h1, x1 + h1, y1);
      else set_stem(b, id, a[3], a[1], x1 - h1, x1 + h1, y1, x0 - h0, x0 + h0, y0);
      b->st.prim = -1; // only upright stems are trimmed by wedge serifs
      break;
    }
    case 'F':
    case 'H':
      serif(b, arg(a, n, 0, 0.0f), arg(a, n, 1, 0.0f), c == 'F');
      break;
    case 'W':
      wedge(b, arg(a, n, 0, 0.0f));
      break;
    case 'V': {
      if (n < 5) break;
      if (b->collect) break;
      float ratio = b->wt->stem / WEIGHTS[FONT_BOLD].stem;
      float xo = snap(b, px_x(b, a[0])), yb = px_y(b, a[1]), yt = px_y(b, a[1] + a[2]);
      float w0 = a[3] * ratio * b->s, w1 = a[4] * ratio * b->s;
      if (b->hint == 2) {
        w0 = copysignf(fmaxf(1.0f, roundf(fabsf(w0))), w0);
        w1 = copysignf(fmaxf(1.0f, roundf(fabsf(w1))), w1);
      }
      float xy[8] = {xo, yb, xo, yt, xo - w1, yt, xo - w0, yb};
      emit_poly(b, xy, 4);
      break;
    }
    case 'P': {
      if (n < 6 || b->collect) break;
      float xy[POLY_MAX * 2];
      int k = n / 2 > POLY_MAX ? POLY_MAX : n / 2;
      for (int i = 0; i < k; i++) {
        xy[i * 2] = px_x(b, a[i * 2]);
        xy[i * 2 + 1] = px_y(b, a[i * 2 + 1]);
      }
      emit_poly(b, xy, k);
      break;
    }
    default:
      break;
    }
  }
}

// ------------------------------------------------------------------ glyph cache

typedef struct Glyph {
  uint32_t cp;
  int sq; // em size in 1/8 px
  FontWeight weight;
  int ox, oy, w, h; // mask offset from the pen position on the baseline
  float adv;        // advance in pixels
  uint8_t *mask;
  int8_t *edge;     // grid-fitted glyphs: first and last solid column of each row, or -1
} Glyph;

static Glyph **cache;
static int cache_cap, cache_count;

static uint32_t cache_hash(uint32_t cp, int sq, FontWeight wt) {
  uint32_t h = cp * 2654435761u ^ (uint32_t)sq * 40503u ^ (uint32_t)wt * 0x9e3779b9u;
  return h ^ (h >> 15);
}

static void build_init(Build *b, float s, int hint, FontWeight wt) {
  memset(b, 0, sizeof *b);
  b->s = s;
  b->hint = hint;
  b->wt = &WEIGHTS[wt];
  b->stem_px = stem_w(b, 1.0f);
  b->hair_px = hair_w(b, 1.0f);
  b->serif_px = hint == 2 ? fmaxf(1.0f, roundf(b->wt->serif * s)) : b->wt->serif * s;
  b->bracket_px = hint == 2 ? 0.0f : b->wt->bracket * s;
}

// Horizontal ink extents of a glyph in design units, used to keep its side
// bearings when it is fitted to the pixel grid.
static void design_extents(const GlyphDef *def, FontWeight wt, float *l, float *r) {
  enum { N = sizeof GLYPHS / sizeof GLYPHS[0] };
  static float ext[2][N][2];
  static bool known[2][N];
  size_t i = (size_t)(def - GLYPHS);
  if (!known[wt][i]) {
    Build b;
    build_init(&b, 1.0f, 1, wt);
    prim_count = 0;
    run(&b, def->prog);
    float x0 = 1e9f, x1 = -1e9f;
    for (int k = 0; k < prim_count; k++) {
      const Prim *p = &prims[k];
      if (p->kind == PRIM_SEG) {
        x0 = fminf(x0, fminf(p->p[0], p->p[2]) - p->ex);
        x1 = fmaxf(x1, fmaxf(p->p[0], p->p[2]) + p->ex);
      } else if (p->kind == PRIM_POLY) {
        for (int v = 0; v < p->n; v++) {
          x0 = fminf(x0, p->p[v * 2]);
          x1 = fmaxf(x1, p->p[v * 2]);
        }
      }
    }
    ext[wt][i][0] = x0;
    ext[wt][i][1] = x1;
    known[wt][i] = true;
  }
  *l = ext[wt][i][0];
  *r = ext[wt][i][1];
}

static Glyph *glyph_build(uint32_t cp, int sq, FontWeight wt) {
  Glyph *g = calloc(1, sizeof(Glyph));
  g->cp = cp;
  g->sq = sq;
  g->weight = wt;
  const GlyphDef *def = glyph_def(cp);
  float s = (float)sq / 8.0f / UNITS;
  bool fitted = grid_fitted(sq);
  if (!def || !def->prog[0]) {
    // spaces and unknown characters; small word spaces round up to stay visible
    float adv = def ? def->adv : UNKNOWN_ADV;
    g->adv = fitted ? ceilf(adv * s + 0.5f) : adv * s;
    return g;
  }
  float adv = def->adv - 2.0f * WEIGHTS[wt].trim;
  g->adv = fitted ? roundf(adv * s) : adv * s;
  float ink_l = 0, ink_r = 0;
  if (fitted) design_extents(def, wt, &ink_l, &ink_r);

  Build b;
  build_init(&b, s, fitted ? 2 : 1, wt);

  // pass 1: anchors; the key heights snap at every size
  for (size_t i = 0; i < sizeof BLUE_ZONES / sizeof BLUE_ZONES[0]; i++) {
    axis_add(&b.ay, BLUE_ZONES[i], roundf(BLUE_ZONES[i] * s));
  }
  b.collect = true;
  if (b.hint == 2) run(&b, def->prog);
  axis_finish(&b.ax);
  axis_finish(&b.ay);

  // pass 2: shapes
  b.collect = false;
  prim_count = 0;
  run(&b, def->prog);
  if (prim_count == 0) return g;

  float x0 = prims[0].bx0, y0 = prims[0].by0, x1 = prims[0].bx1, y1 = prims[0].by1;
  for (int i = 1; i < prim_count; i++) {
    x0 = fminf(x0, prims[i].bx0);
    y0 = fminf(y0, prims[i].by0);
    x1 = fmaxf(x1, prims[i].bx1);
    y1 = fmaxf(y1, prims[i].by1);
  }
  Cover cv;
  cv.ox = (int)floorf(x0);
  cv.oy = (int)floorf(y0);
  cv.w = (int)ceilf(x1) - cv.ox;
  cv.h = (int)ceilf(y1) - cv.oy;
  size_t area = (size_t)cv.w * (size_t)cv.h;
  if (area > cover_cap) {
    cover_cap = area;
    cover_buf = realloc(cover_buf, area * sizeof(float));
  }
  cv.cov = cover_buf;
  memset(cv.cov, 0, area * sizeof(float));
  for (int i = 0; i < prim_count; i++) {
    const Prim *p = &prims[i];
    if (p->kind == PRIM_SEG) raster_seg(&cv, p);
    else if (p->kind == PRIM_POLY) raster_poly(&cv, p);
    else raster_fillet(&cv, p);
  }

  // crop to the inked area
  int i0 = cv.w, j0 = cv.h, i1 = -1, j1 = -1;
  for (int j = 0; j < cv.h; j++) {
    for (int i = 0; i < cv.w; i++) {
      if (cv.cov[(size_t)j * cv.w + i] >= 0.5f / 255.0f) {
        if (i < i0) i0 = i;
        if (i > i1) i1 = i;
        if (j < j0) j0 = j;
        if (j > j1) j1 = j;
      }
    }
  }
  if (i1 < 0) return g;

  int shift = 0;
  if (fitted) {
    // Grid-fitted advance: the solid ink keeps the design side bearings,
    // each rounded to whole pixels, and letters never touch. Figures keep
    // one common width and are centred in it.
    int c0 = cv.w, c1 = -1;
    for (int i = 0; i < cv.w; i++) {
      for (int j = 0; j < cv.h; j++) {
        if (cv.cov[(size_t)j * cv.w + i] >= 0.3f) {
          if (i < c0) c0 = i;
          c1 = i;
          break;
        }
      }
    }
    if (c1 >= 0) {
      int left = cv.ox + c0, width = c1 - c0 + 1;
      if (cp >= '0' && cp <= '9') {
        shift = (int)floorf((g->adv - (float)width) * 0.5f + 0.5f) - left;
      } else {
        float lp = roundf(ink_l * s), rp = roundf((adv - ink_r) * s);
        shift = (int)lp - left;
        g->adv = lp + (float)width + rp;
      }
    }
  }
  g->ox = cv.ox + i0 + shift;
  g->oy = cv.oy + j0;
  g->w = i1 - i0 + 1;
  g->h = j1 - j0 + 1;
  g->mask = malloc((size_t)g->w * (size_t)g->h);
  for (int j = 0; j < g->h; j++) {
    const float *src = cv.cov + (size_t)(j0 + j) * cv.w + i0;
    uint8_t *dst = g->mask + (size_t)j * g->w;
    for (int i = 0; i < g->w; i++) dst[i] = (uint8_t)(src[i] * 255.0f + 0.5f);
  }
  if (fitted && g->w < 127) {
    g->edge = malloc((size_t)g->h * 2);
    for (int j = 0; j < g->h; j++) {
      const uint8_t *row = g->mask + (size_t)j * g->w;
      int a = -1, z = -1;
      for (int i = 0; i < g->w; i++) {
        if (row[i] >= 77) {
          if (a < 0) a = i;
          z = i;
        }
      }
      g->edge[j * 2] = (int8_t)a;
      g->edge[j * 2 + 1] = (int8_t)z;
    }
  }
  return g;
}

static void cache_insert(Glyph *g) {
  uint32_t mask = (uint32_t)cache_cap - 1;
  uint32_t h = cache_hash(g->cp, g->sq, g->weight) & mask;
  while (cache[h]) h = (h + 1) & mask;
  cache[h] = g;
}

static Glyph *glyph_get(uint32_t cp, int sq, FontWeight wt) {
  if (cache_cap) {
    uint32_t mask = (uint32_t)cache_cap - 1;
    for (uint32_t h = cache_hash(cp, sq, wt) & mask; cache[h]; h = (h + 1) & mask) {
      Glyph *g = cache[h];
      if (g->cp == cp && g->sq == sq && g->weight == wt) return g;
    }
  }
  if ((cache_count + 1) * 4 > cache_cap * 3) {
    Glyph **old = cache;
    int old_cap = cache_cap;
    cache_cap = cache_cap ? cache_cap * 2 : 1024;
    cache = calloc((size_t)cache_cap, sizeof(Glyph *));
    for (int i = 0; i < old_cap; i++) {
      if (old[i]) cache_insert(old[i]);
    }
    free(old);
  }
  Glyph *g = glyph_build(cp, sq, wt);
  cache_insert(g);
  cache_count++;
  return g;
}

// ------------------------------------------------------------------ drawing

static void blit_mask(Image *dst, const Glyph *g, int x, int y, Color c) {
  if (!g->mask) return;
  int x0 = x + g->ox, y0 = y + g->oy;
  int i0 = dst->clip_x0 - x0, i1 = dst->clip_x1 - x0;
  int j0 = dst->clip_y0 - y0, j1 = dst->clip_y1 - y0;
  if (i0 < 0) i0 = 0;
  if (j0 < 0) j0 = 0;
  if (i1 > g->w) i1 = g->w;
  if (j1 > g->h) j1 = g->h;
  float k = c.a / 255.0f;
  for (int j = j0; j < j1; j++) {
    const uint8_t *src = g->mask + (size_t)j * g->w;
    uint32_t *row = dst->px + (size_t)(y0 + j) * dst->w + x0;
    for (int i = i0; i < i1; i++) {
      if (src[i]) px_blend(row + i, c.r, c.g, c.b, (float)src[i] * k);
    }
  }
}

// Small grid-fitted text: how far glyph b, about to be drawn dx pixels after
// glyph a, must move right to leave at least one clear pixel column between
// them on every row. Figures are exempt so numbers keep a fixed pitch.
static int clearance(const Glyph *a, const Glyph *b, int dx) {
  if (!a || !a->edge || !b->edge) return 0;
  if (a->cp >= '0' && a->cp <= '9' && b->cp >= '0' && b->cp <= '9') return 0;
  int need = 0;
  int y0 = a->oy > b->oy ? a->oy : b->oy;
  int y1 = a->oy + a->h < b->oy + b->h ? a->oy + a->h : b->oy + b->h;
  for (int y = y0; y < y1; y++) {
    int ar = a->edge[(y - a->oy) * 2 + 1], bl = b->edge[(y - b->oy) * 2];
    if (ar < 0 || bl < 0) continue;
    int gap = (dx + b->ox + bl) - (a->ox + ar) - 1;
    if (1 - gap > need) need = 1 - gap;
  }
  return need;
}

// Walks a string glyph by glyph: returns the advance to the next pen
// position and sets *at to where this glyph is drawn.
typedef struct Layout {
  int sq;
  FontWeight weight;
  float pen;
  uint32_t prev;
  const Glyph *last;
  float last_x;
} Layout;

static const Glyph *layout_next(Layout *lo, uint32_t cp, float *at) {
  const Glyph *g = glyph_get(cp, lo->sq, lo->weight);
  lo->pen += kern_px(lo->prev, cp, lo->sq);
  float x = floorf(lo->pen + 0.5f);
  if (g->mask && lo->last) {
    int more = clearance(lo->last, g, (int)(x - lo->last_x));
    lo->pen += (float)more;
    x += (float)more;
  }
  *at = x;
  if (g->mask) {
    lo->last = g;
    lo->last_x = x;
  } else if (g->adv > 0) {
    lo->last = NULL;
  }
  lo->pen += g->adv;
  lo->prev = cp;
  return g;
}

float font_measure(const char *text, float size, FontWeight weight) {
  int sq = size_key(size);
  if (!text || sq <= 0) return 0;
  Layout lo = {sq, weight, 0, 0, NULL, 0};
  float at;
  while (*text) layout_next(&lo, utf8_next(&text), &at);
  return lo.pen;
}

void font_draw(Image *dst, const char *text, float x, float y, float size, FontWeight weight, TextAlign align,
               Color color) {
  int sq = size_key(size);
  if (!dst || !text || sq <= 0 || color.a <= 0) return;
  if (align != ALIGN_LEFT) {
    float w = font_measure(text, size, weight);
    x -= align == ALIGN_CENTER ? w * 0.5f : w;
  }
  float origin = floorf(x + 0.5f);
  int base = (int)floorf(y + 0.5f);
  Layout lo = {sq, weight, 0, 0, NULL, 0};
  while (*text) {
    float at;
    const Glyph *g = layout_next(&lo, utf8_next(&text), &at);
    blit_mask(dst, g, (int)(origin + at), base, color);
  }
}

void font_shutdown(void) {
  for (int i = 0; i < cache_cap; i++) {
    if (cache[i]) {
      free(cache[i]->mask);
      free(cache[i]->edge);
      free(cache[i]);
    }
  }
  free(cache);
  cache = NULL;
  cache_cap = cache_count = 0;
  free(prims);
  prims = NULL;
  prim_count = prim_cap = 0;
  free(cover_buf);
  cover_buf = NULL;
  cover_cap = 0;
}
