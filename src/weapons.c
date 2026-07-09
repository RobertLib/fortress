// POV weapon views. Weapons are modelled as small assemblies of 3D boxes and
// rendered with a perspective camera sitting at the player's eye: flat-shaded
// faces sorted back-to-front, strings and bolt streaks drawn as projected 3D
// lines.
//
// Camera space: x right, y up, z forward into the screen; the eye is at the
// origin. Each part is an axis-aligned box (optionally rotated around its
// own center, optionally tapered toward one end) with a base color.

#include "assets.h"

#include <string.h>

#define FOCAL 150.0
#define VIEW_CX (WEAPON_VIEW / 2.0)
#define VIEW_CY 78.0

#define MAX_PARTS 24
#define MAX_LINES 4
#define MAX_FACES (MAX_PARTS * 6)

typedef struct {
  double x, y, z;
} Vec3;

static Vec3 v3(double x, double y, double z) {
  Vec3 v = {x, y, z};
  return v;
}

static Vec3 v3_sub(Vec3 a, Vec3 b) { return v3(a.x - b.x, a.y - b.y, a.z - b.z); }

static double v3_dot(Vec3 a, Vec3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }

static Vec3 v3_cross(Vec3 a, Vec3 b) { return v3(a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x); }

static Vec3 v3_norm(Vec3 v) {
  double l = sqrt(v.x * v.x + v.y * v.y + v.z * v.z);
  if (l == 0) l = 1;
  return v3(v.x / l, v.y / l, v.z / l);
}

static double axis_of(Vec3 v, int axis) { return axis == 0 ? v.x : axis == 1 ? v.y : v.z; }

// Rotation by r.x around x, then r.y around y, then r.z around z.
static Vec3 rot3(Vec3 v, Vec3 r) {
  double x = v.x, y = v.y, z = v.z;
  if (r.x != 0) {
    double c = cos(r.x), s = sin(r.x);
    double ny = y * c - z * s, nz = y * s + z * c;
    y = ny;
    z = nz;
  }
  if (r.y != 0) {
    double c = cos(r.y), s = sin(r.y);
    double nx = x * c + z * s, nz = -x * s + z * c;
    x = nx;
    z = nz;
  }
  if (r.z != 0) {
    double c = cos(r.z), s = sin(r.z);
    double nx = x * c - y * s, ny = x * s + y * c;
    x = nx;
    y = ny;
  }
  return v3(x, y, z);
}

static void project(Vec3 v, float *out) {
  out[0] = (float)(VIEW_CX + (FOCAL * v.x) / v.z);
  out[1] = (float)(VIEW_CY - (FOCAL * v.y) / v.z);
}

// ------------------------------------------------------------------ models

typedef struct {
  Vec3 p; // center
  Vec3 s; // width, height, depth
  const char *color;
  Vec3 r; // rotation around the center
  // taper: the cross-section at one end of the box (end +1 or -1 along
  // taper_axis) is scaled by taper_k; taper_axis < 0 for a plain box
  int taper_axis, taper_end;
  double taper_k;
} Part;

typedef struct {
  Vec3 a, b;
  const char *color;
  double width;
} Line3;

typedef struct {
  Part parts[MAX_PARTS];
  int part_count;
  Line3 lines[MAX_LINES];
  int line_count;
  // global pitch around `pivot`: tilts the whole weapon nose-down so its top
  // faces show
  double pitch;
  Vec3 pivot;
  // where the hands hold the weapon this frame: a fixed offset plus the
  // recoil or thrust direction scaled by the frame's kick
  Vec3 offset, recoil;
  double kick;
} Model;

static Vec3 at(const Model *m, double x, double y, double z) {
  return v3(x + m->offset.x + m->recoil.x * m->kick, y + m->offset.y + m->recoil.y * m->kick,
            z + m->offset.z + m->recoil.z * m->kick);
}

static Part *part(Model *m, Vec3 p, Vec3 s, const char *color, Vec3 r) {
  Part *pt = &m->parts[m->part_count++];
  pt->p = p;
  pt->s = s;
  pt->color = color;
  pt->r = r;
  pt->taper_axis = -1;
  pt->taper_end = 1;
  pt->taper_k = 1;
  return pt;
}

static void taper(Part *pt, int axis, double k, int end) {
  pt->taper_axis = axis;
  pt->taper_k = k;
  pt->taper_end = end;
}

static void line(Model *m, Vec3 a, Vec3 b, const char *color, double width) {
  Line3 *l = &m->lines[m->line_count++];
  l->a = a;
  l->b = b;
  l->color = color;
  l->width = width;
}

// Rotate a finished model around the vertical axis through `pivot`, so the
// weapon is seen slightly from the side: pure head-on views read as flat.
static Vec3 yaw_point(Vec3 v, double cy, double sy, Vec3 pivot) {
  double x = v.x - pivot.x, z = v.z - pivot.z;
  return v3(pivot.x + x * cy + z * sy, v.y, pivot.z - x * sy + z * cy);
}

static void yaw_model(Model *m, double yaw, Vec3 pivot) {
  double cy = cos(yaw), sy = sin(yaw);
  for (int i = 0; i < m->part_count; i++) {
    m->parts[i].p = yaw_point(m->parts[i].p, cy, sy, pivot);
    m->parts[i].r.y += yaw;
  }
  for (int i = 0; i < m->line_count; i++) {
    m->lines[i].a = yaw_point(m->lines[i].a, cy, sy, pivot);
    m->lines[i].b = yaw_point(m->lines[i].b, cy, sy, pivot);
  }
}

#define SKIN "#dba377"
#define SKIN_DARK "#c08b5c"
#define LIMB "#523823"
#define GOLD "#c9a24a"
#define STEEL "#c4ced4"
#define STRING "#c9bd9c"
#define LEATHER "#4a3220"
#define STREAK "#f2ecd8"

#define AXIS_X 0
#define AXIS_Z 2

static void weapon_model(Model *m, WeaponKind kind, int frame) {
  const Vec3 still = v3(0, 0, 0);
  memset(m, 0, sizeof(*m));

  if (kind == WEAPON_DAGGER) {
    // held point-forward: the blade juts out of the fist toward the
    // crosshair, and the stab thrusts the whole arm deeper into the scene
    m->kick = frame == 1 ? 1 : frame == 2 ? 0.45 : 0;
    m->recoil = v3(0, 0.02, 0.24);
    part(m, at(m, 0.1, -0.38, 0.44), v3(0.14, 0.3, 0.13), SKIN, v3(0.35, 0, 0.12)); // forearm
    part(m, at(m, 0.11, -0.48, 0.4), v3(0.16, 0.14, 0.15), LEATHER, v3(0.35, 0, 0.12)); // bracer
    part(m, at(m, 0.06, -0.24, 0.5), v3(0.16, 0.13, 0.16), SKIN, v3(0.05, 0.1, 0)); // fist
    part(m, at(m, -0.02, -0.21, 0.455), v3(0.05, 0.05, 0.07), SKIN_DARK, still); // thumb
    part(m, at(m, 0.06, -0.21, 0.415), v3(0.05, 0.06, 0.035), GOLD, still); // pommel behind the fist
    part(m, at(m, 0.06, -0.2, 0.575), v3(0.2, 0.05, 0.035), GOLD, v3(-0.35, 0, 0)); // crossguard
    taper(part(m, at(m, 0.06, -0.135, 0.745), v3(0.045, 0.02, 0.36), STEEL, v3(-0.35, 0, 0)), AXIS_Z, 0.15,
          1); // blade
    yaw_model(m, -0.15, v3(0.06, 0, 0.5));
    m->pitch = 0.06;
    m->pivot = v3(0, -0.2, 0.5);
  } else if (kind == WEAPON_CROSSBOW) {
    // sits low; recoils up and back
    m->kick = frame == 1 ? 1 : frame == 2 ? 0.5 : 0;
    m->offset = v3(0.03, -0.06, 0);
    m->recoil = v3(0, 0.05, -0.04);
    part(m, at(m, 0.02, -0.35, 0.46), v3(0.14, 0.3, 0.13), SKIN, v3(0.32, 0, 0)); // forearm
    part(m, at(m, 0.03, -0.46, 0.42), v3(0.16, 0.14, 0.15), LEATHER, v3(0.32, 0, 0)); // bracer
    part(m, at(m, 0, -0.21, 0.52), v3(0.16, 0.12, 0.15), SKIN, still); // fist on the grip
    part(m, at(m, 0, -0.12, 0.4), v3(0.09, 0.13, 0.14), "#4a3018", still); // butt at the cheek
    taper(part(m, at(m, 0, -0.1, 0.6), v3(0.08, 0.08, 0.42), "#6a4426", still), AXIS_Z, 0.75, 1); // tiller
    // wooden limbs swept back toward the shooter, steel lath on their face
    taper(part(m, at(m, -0.19, -0.045, 0.72), v3(0.38, 0.06, 0.065), LIMB, v3(0, -0.4, -0.18)), AXIS_X, 0.5, -1);
    taper(part(m, at(m, 0.19, -0.045, 0.72), v3(0.38, 0.06, 0.065), LIMB, v3(0, 0.4, 0.18)), AXIS_X, 0.5, 1);
    Vec3 tip_l = at(m, -0.36, -0.011, 0.646);
    Vec3 tip_r = at(m, 0.36, -0.011, 0.646);
    if (frame == 0) {
      // string cocked back to the nut, bolt lying on the tiller
      Vec3 nut = at(m, 0, -0.065, 0.5);
      line(m, tip_l, nut, STRING, 2);
      line(m, tip_r, nut, STRING, 2);
      taper(part(m, at(m, 0, -0.03, 0.62), v3(0.024, 0.024, 0.36), "#5d3f1c", still), AXIS_Z, 0.5, 1); // shaft
      part(m, at(m, 0, -0.03, 0.82), v3(0.045, 0.045, 0.08), STEEL, still); // head
      part(m, at(m, 0, -0.025, 0.49), v3(0.018, 0.065, 0.08), "#a03030", still); // fletching
      part(m, at(m, 0, -0.025, 0.49), v3(0.065, 0.018, 0.08), "#a03030", still);
    } else {
      // loosed: string resting against the front of the lath
      Vec3 front = at(m, 0, -0.055, 0.68);
      line(m, tip_l, front, STRING, 2);
      line(m, tip_r, front, STRING, 2);
      if (frame == 1) line(m, v3(0, 0.03, 1.2), v3(0, 0.1, 2.4), STREAK, 2); // bolt racing away
    }
    yaw_model(m, 0.08, v3(0.03, 0, 0.55));
    m->pitch = 0.16;
    m->pivot = v3(0, -0.12, 0.5);
  } else {
    // repeating crossbow, sits low
    m->kick = frame == 1 ? 1 : frame == 2 ? 0.5 : 0;
    m->offset = v3(0.02, -0.06, 0);
    m->recoil = v3(0, 0.04, -0.04);
    // both arms braced
    part(m, at(m, -0.21, -0.38, 0.45), v3(0.11, 0.28, 0.1), SKIN, v3(0.28, 0, -0.18));
    part(m, at(m, 0.21, -0.38, 0.45), v3(0.11, 0.28, 0.1), SKIN, v3(0.28, 0, 0.18));
    part(m, at(m, -0.24, -0.47, 0.42), v3(0.13, 0.12, 0.12), LEATHER, v3(0.28, 0, -0.18)); // bracers
    part(m, at(m, 0.24, -0.47, 0.42), v3(0.13, 0.12, 0.12), LEATHER, v3(0.28, 0, 0.18));
    part(m, at(m, -0.16, -0.24, 0.51), v3(0.13, 0.1, 0.13), SKIN, still); // fists
    part(m, at(m, 0.16, -0.24, 0.51), v3(0.13, 0.1, 0.13), SKIN, still);
    part(m, at(m, 0, -0.13, 0.38), v3(0.11, 0.15, 0.16), "#4a3018", still); // butt
    taper(part(m, at(m, 0, -0.09, 0.6), v3(0.11, 0.1, 0.46), "#6a4426", still), AXIS_Z, 0.8, 1); // tiller
    // magazine of the repeating mechanism riding on top, iron-strapped
    part(m, at(m, 0, 0.015, 0.58), v3(0.12, 0.1, 0.22), "#7a5230", still);
    part(m, at(m, 0, 0.015, 0.51), v3(0.13, 0.11, 0.02), "#2e2012", still);
    part(m, at(m, 0, 0.015, 0.65), v3(0.13, 0.11, 0.02), "#2e2012", still);
    // heavy limbs with steel laths
    taper(part(m, at(m, -0.2, -0.04, 0.74), v3(0.4, 0.065, 0.075), LIMB, v3(0, -0.45, -0.2)), AXIS_X, 0.5, -1);
    taper(part(m, at(m, 0.2, -0.04, 0.74), v3(0.4, 0.065, 0.075), LIMB, v3(0, 0.45, 0.2)), AXIS_X, 0.5, 1);
    Vec3 tip_l = at(m, -0.375, 0.0, 0.666);
    Vec3 tip_r = at(m, 0.375, 0.0, 0.666);
    Vec3 hook = at(m, 0, -0.07, 0.52);
    line(m, tip_l, hook, STRING, 2);
    line(m, tip_r, hook, STRING, 2);
    if (frame == 1) line(m, v3(0, 0.04, 1.2), v3(0, 0.12, 2.4), STREAK, 3); // bolt racing away
    yaw_model(m, 0.08, v3(0.02, 0, 0.55));
    m->pitch = 0.16;
    m->pivot = v3(0, -0.12, 0.5);
  }
}

// ------------------------------------------------------------------ rendering

typedef struct {
  double z; // depth of the face's center
  float pts[8]; // projected quad
  const char *color;
  double light; // brightness factor
} Face;

// quad corner indices of a box whose vertex i has bits (x:4, y:2, z:1)
static const int BOX_FACES[6][4] = {
  {0, 1, 3, 2}, {4, 5, 7, 6}, // -x +x
  {0, 1, 5, 4}, {2, 3, 7, 6}, // -y +y
  {0, 2, 6, 4}, {1, 3, 7, 5}, // -z +z
};

// the global pitch around the model's pivot
static Vec3 world(const Model *m, Vec3 v) {
  double pc = cos(m->pitch), ps = sin(m->pitch);
  double y = v.y - m->pivot.y, z = v.z - m->pivot.z;
  return v3(v.x, m->pivot.y + y * pc - z * ps, m->pivot.z + y * ps + z * pc);
}

// Appends the faces of a part that look toward the eye.
static int collect_faces(const Model *m, const Part *pt, Face *out, int count) {
  Vec3 light = v3_norm(v3(-0.4, 0.75, -0.5)); // torch above the left shoulder
  double hw = pt->s.x / 2, hh = pt->s.y / 2, hd = pt->s.z / 2;
  Vec3 verts[8];
  for (int i = 0; i < 8; i++) {
    Vec3 v = v3(i & 4 ? hw : -hw, i & 2 ? hh : -hh, i & 1 ? hd : -hd);
    if (pt->taper_axis >= 0) {
      double along = axis_of(v, pt->taper_axis);
      int sign = along > 0 ? 1 : along < 0 ? -1 : 0;
      if (sign == pt->taper_end) {
        if (pt->taper_axis != 0) v.x *= pt->taper_k;
        if (pt->taper_axis != 1) v.y *= pt->taper_k;
        if (pt->taper_axis != 2) v.z *= pt->taper_k;
      }
    }
    v = rot3(v, pt->r);
    verts[i] = world(m, v3(v.x + pt->p.x, v.y + pt->p.y, v.z + pt->p.z));
  }
  Vec3 pc = world(m, pt->p);
  for (int f = 0; f < 6; f++) {
    Vec3 q[4];
    for (int k = 0; k < 4; k++) q[k] = verts[BOX_FACES[f][k]];
    Vec3 cen = v3((q[0].x + q[1].x + q[2].x + q[3].x) / 4, (q[0].y + q[1].y + q[2].y + q[3].y) / 4,
                  (q[0].z + q[1].z + q[2].z + q[3].z) / 4);
    Vec3 n = v3_norm(v3_cross(v3_sub(q[1], q[0]), v3_sub(q[2], q[0])));
    if (v3_dot(n, v3_sub(cen, pc)) < 0) n = v3(-n.x, -n.y, -n.z); // outward
    if (v3_dot(n, cen) >= 0) continue; // facing away from the eye
    Face *face = &out[count++];
    face->z = cen.z;
    for (int k = 0; k < 4; k++) project(q[k], face->pts + k * 2);
    face->color = pt->color;
    face->light = 0.38 + 0.62 * fmax(0, v3_dot(n, light));
  }
  return count;
}

// Base color scaled by the face's light, each channel floored.
static Color lit(const char *hex, double f) {
  Color c = color_parse(hex);
  c.r = (float)fmin(255.0, floor(c.r * f));
  c.g = (float)fmin(255.0, floor(c.g * f));
  c.b = (float)fmin(255.0, floor(c.b * f));
  return c;
}

static void render_model(Image *img, const Model *m) {
  Face faces[MAX_FACES];
  int count = 0;
  for (int i = 0; i < m->part_count; i++) count = collect_faces(m, &m->parts[i], faces, count);
  // painter's algorithm: far first; a stable insertion sort keeps faces of
  // equal depth in model order
  for (int i = 1; i < count; i++) {
    Face f = faces[i];
    int j = i;
    for (; j > 0 && faces[j - 1].z < f.z; j--) faces[j] = faces[j - 1];
    faces[j] = f;
  }
  for (int i = 0; i < count; i++) {
    gfx_fill_poly(img, faces[i].pts, 4, lit(faces[i].color, faces[i].light));
    // darker edges define the form
    gfx_stroke_poly(img, faces[i].pts, 4, 1.0f, lit(faces[i].color, faces[i].light * 0.8));
  }
  for (int i = 0; i < m->line_count; i++) {
    const Line3 *l = &m->lines[i];
    float a[2], b[2];
    project(world(m, l->a), a);
    project(world(m, l->b), b);
    gfx_line(img, a[0], a[1], b[0], b[1], (float)l->width, color_parse(l->color));
  }
}

Image *weapon_view_build(WeaponKind kind, int frame) {
  Model m;
  weapon_model(&m, kind, frame);
  Image *img = image_new(WEAPON_VIEW, WEAPON_VIEW);
  render_model(img, &m);
  return img;
}
