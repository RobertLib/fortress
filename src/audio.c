// Procedural sound effects and a small software mixer. Every effect is
// synthesized once into a mono sample buffer; the device callback mixes the
// active voices (rate, gain, optional low-pass and pan) into stereo output.

#include "audio.h"

#include "common.h"

#include <SDL3/SDL.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

#define SAMPLE_RATE 44100
#define MAX_VOICES 64
#define MIX_FRAMES 512 // frames mixed per chunk in the device callback
#define FRAME_BYTES ((int)(2 * sizeof(float)))
#define MASTER_GAIN 0.5f
#define FILTER_Q 1.0 // low-pass resonance in dB

// ------------------------------------------------------------------ synthesis

// Running state for the synths that accumulate their own phase; every buffer
// starts from a zeroed state.
typedef struct {
  double ph; // oscillator phase
  double lt; // time of the previous sample
  double f;  // drifting pulse rate
  double z;  // one-sample filter memory
} SynthState;

typedef double (*SynthFn)(double t, SynthState *s);

static double noise(void) { return frand() * 2 - 1; }
static int effects_volume=85,music_volume=45;
static double env(double t, double dur, double p) { return pow(fmax(0, 1 - t / dur), p); }
static double saw(double t, double f) { return 2 * fmod(t * f, 1) - 1; }
static double square(double t, double f) { return fmod(t * f, 1) < 0.5 ? 1 : -1; }
static double sine(double t, double f) { return sin(2 * M_PI * f * t); }

// --- weapons

static double sfx_crossbow(double t, SynthState *s) {
  // string twang + wooden thunk
  double snap = noise() * env(t, 0.03, 2) * 0.7;
  double twang = sine(t, 480 * (1 - t * 1.5)) * env(t, 0.09, 3) * 0.5;
  double thunk = sine(t, 110 * (1 - t)) * env(t, 0.22, 3) * 0.6;
  return (snap + twang + thunk) * 0.85;
}

static double sfx_arbalest(double t, SynthState *s) {
  double snap = noise() * env(t, 0.025, 2) * 0.6;
  double twang = sine(t, 520 * (1 - t * 2)) * env(t, 0.06, 3) * 0.45;
  double thunk = sine(t, 130 * (1 - t * 2)) * env(t, 0.1, 3) * 0.5;
  return (snap + twang + thunk) * 0.8;
}

static double sfx_dagger(double t, SynthState *s) {
  // whoosh: filtered-ish noise with moving amplitude
  double sweep = sin(M_PI * (t / 0.16));
  return noise() * sweep * sweep * 0.35;
}

static double sfx_empty(double t, SynthState *s) { return square(t, 900) * env(t, 0.05, 6) * 0.25; }
static double sfx_metal_hit(double t,SynthState *s) {
  return noise()*env(t,.018,2)*.25 + (sine(t,1463)+sine(t,2317)*.45+sine(t,3771)*.2)*env(t,.36,2)*.3;
}
static double sfx_flesh_hit(double t,SynthState *s) {
  s->z+=(noise()-s->z)*.32;
  return s->z*env(t,.14,2)*1.5+sine(t,86*(1-t*2))*env(t,.12,3)*.4;
}
static double sfx_reload(double t,SynthState *s) {
  double click=noise()*env(t,.025,3)*.45;
  if (t>.09) click+=noise()*env(t-.09,.035,3)*.3;
  return click+sine(t,170)*env(t,.16,3)*.14;
}
static double sfx_captain_call(double t,SynthState *s) {
  double attack=fmin(1,t/.07),fade=env(t,.9,1.2);
  double f=146.83*(1+.006*sin(t*32));
  return (sine(t,f)+sine(t,f*2)*.35+sine(t,f*3)*.14)*attack*fade*.45;
}
static double sfx_step_wood(double t,SynthState *s) {
  return noise()*env(t,.018,3)*.3+(sine(t,115)+sine(t,193)*.4)*env(t,.14,3)*.45;
}
static double sfx_step_leaves(double t,SynthState *s) {
  s->z+=(noise()-s->z)*.15;
  return s->z*env(t,.17,1.4)*.9+noise()*env(t,.045,2)*.1;
}

static double sfx_splash(double t, SynthState *s) {
  s->z += (noise() - s->z) * .18;
  double bubbles = sine(t, 420 + 180 * sin(t * 37)) * env(t, .32, 2) * .14;
  return s->z * env(t, .3, 1.8) * 2 + bubbles;
}

static double sfx_water_flow(double t, SynthState *s) {
  s->z += (noise() - s->z) * .065;
  double fade = fmin(1, t / .15) * fmin(1, (.9 - t) / .2);
  double bubble = sine(t, 580 + 150 * sin(t * 19)) * pow(fmax(0, sin(t * 31)), 8) * .035;
  return (s->z * .8 + bubble) * fade;
}

static double sfx_enemy_shot(double t, SynthState *s) {
  // enemy crossbow: deeper twang from a distance
  double snap = noise() * env(t, 0.04, 2) * 0.55;
  double twang = sine(t, 360 * (1 - t * 1.5)) * env(t, 0.1, 3) * 0.45;
  double thunk = sine(t, 90 * (1 - t)) * env(t, 0.24, 3) * 0.6;
  return (snap + twang + thunk) * 0.75;
}

// --- traps

static double sfx_trap_click(double t, SynthState *s) {
  // two dry mechanical clicks under the flagstone
  bool on = fmod(t, 0.08) < 0.014;
  return on ? (noise() * 0.4 + square(t, 1500) * 0.5) * env(t, 0.16, 1) : 0;
}

static double sfx_arrow_hit(double t, SynthState *s) {
  // arrow thocking into wood or stone
  return (noise() * 0.5 + sine(t, 190 * (1 - t * 3)) * 0.6) * env(t, 0.07, 3) * 0.7;
}

static double sfx_pit_fall(double t, SynthState *s) {
  // A rush of air, then stones clattering deep below the opening.
  s->z += (noise() - s->z) * 0.08;
  double rush = s->z * 2.2 * sin(M_PI * fmin(1, t / 0.75));
  double impact = t > 0.5 ? (noise() * 0.35 + sine(t, 72) * 0.5) * env(t - 0.5, 0.4, 2) : 0;
  return rush + impact;
}

static double sfx_pendulum(double t, SynthState *s) {
  // Air rushing past a heavy blade, with a rusty ceiling bearing behind it.
  s->z += (noise() - s->z) * 0.12;
  double sweep = sin(M_PI * fmin(1, t / 0.45));
  double creak = (sine(t, 170 + 60 * sin(t * 13)) + sine(t, 510) * 0.3) * env(t, 0.45, 1.8);
  return s->z * sweep * sweep * 1.4 + creak * 0.12;
}

static double sfx_explosion(double t, SynthState *s) {
  double n = noise();
  s->z += (n - s->z) * 0.055;
  double crack = n * env(t, 0.06, 2) * 0.85;
  double rumble = (s->z * 3 + sine(t, 65 - t * 40) * 0.55) * env(t, 0.8, 2);
  double splinters = n * env(t, 0.35, 3) * 0.22;
  return (crack + rumble + splinters) * 0.9;
}

static double sfx_wall_hit(double t, SynthState *s) {
  double chip = noise() * env(t, 0.045, 2);
  double knock = sine(t, 340 - 400 * t) * env(t, 0.13, 3);
  return chip * 0.6 + knock * 0.4;
}

static double sfx_wall_break(double t, SynthState *s) {
  double n = noise();
  s->z += (n - s->z) * 0.08;
  double crack = n * env(t, 0.08, 2) * 0.8;
  double tumble = (s->z * 2.5 + sine(t, 85 - t * 45) * 0.3) * env(t, 0.75, 1.6);
  double chips = n * pow(fmax(0, sine(t, 23)), 6) * env(t, 0.65, 1.3) * 0.45;
  return crack + tumble + chips;
}

// --- player

static double sfx_player_pain(double t, SynthState *s) { return saw(t, 280 - 500 * t) * env(t, 0.22, 3) * 0.5; }

static double sfx_player_death(double t, SynthState *s) {
  double f = 320 * pow(0.12, t / 0.9) + 40;
  return (saw(t, f) * 0.6 + noise() * 0.15) * env(t, 0.9, 1.5) * 0.8;
}

static double sfx_step(double t, SynthState *s) { return noise() * env(t, 0.05, 4) * 0.12; }

static double sfx_venom(double t, SynthState *s) {
  // the poison throbbing in the blood: a dull, uneasy double heartbeat
  double lub = (sine(t, 62 - 30 * t) + sine(t, 124 - 60 * t) * 0.4) * env(t, 0.12, 2);
  double k = t - 0.17;
  double dub = k > 0 ? (sine(k, 52) + sine(k, 104) * 0.35) * env(k, 0.14, 2) * 0.7 : 0;
  return (lub + dub) * 0.85;
}

// --- enemies

static double sfx_alert(double t, SynthState *s) {
  // sharp two-tone shout, "halt!"
  double f = t < 0.14 ? 470 : 350;
  return (square(t, f) * 0.5 + noise() * 0.12) * env(fmod(t, 0.15), 0.14, 1.5) * 0.55;
}

static double sfx_knife_slash(double t, SynthState *s) {
  // enemy blade at close quarters: airy sweep ending in a sharp nick
  double sweep = sin(M_PI * fmin(1, t / 0.12));
  double whoosh = noise() * sweep * sweep * 0.4;
  double nick = t > 0.11 ? sine(t, 1400 * (1 - (t - 0.11) * 4)) * env(t - 0.11, 0.06, 2) * 0.5 : 0;
  return whoosh + nick;
}

static double sfx_punch(double t, SynthState *s) {
  // dull bare-knuckle body blow
  double thud = sine(t, 95 * (1 - t * 2)) * env(t, 0.12, 2) * 0.8;
  double smack = noise() * env(t, 0.03, 2) * 0.45;
  return (thud + smack) * 0.9;
}

static double sfx_enemy_pain(double t, SynthState *s) { return square(t, 420 - 700 * t) * env(t, 0.18, 3) * 0.35; }

static double sfx_enemy_death(double t, SynthState *s) {
  double f = 380 * pow(0.2, t / 0.6) + 60;
  return (saw(t, f) * 0.55 + noise() * 0.2 * env(t, 0.25, 3)) * env(t, 0.6, 1.6) * 0.7;
}

// --- bats

static double sfx_bat_alert(double t, SynthState *s) {
  // burst of shrill chittering as the swarm takes wing
  bool on = fmod(t, 0.08) < 0.038;
  double f = 2300 - 900 * (t / 0.4) + sine(t, 27) * 160;
  double flap = noise() * 0.14 * env(t, 0.4, 1);
  return (on ? sine(t, f) * env(fmod(t, 0.08), 0.05, 2) * 0.4 : 0) + flap;
}

static double sfx_bat_squeak(double t, SynthState *s) { return sine(t, 2600 - 5000 * t) * env(t, 0.13, 2) * 0.4; }

static double sfx_bat_bite(double t, SynthState *s) {
  return (sine(t, 1900 - 6500 * t) * 0.5 + noise() * 0.18) * env(t, 0.11, 2) * 0.6;
}

static double sfx_bat_death(double t, SynthState *s) {
  double f = 2200 * pow(0.22, t / 0.5);
  return (sine(t, f) * 0.5 + noise() * 0.15 * env(t, 0.22, 3)) * env(t, 0.5, 1.5) * 0.6;
}

static double sfx_flutter(double t, SynthState *s) {
  // leathery wingbeats: noise pulsed at wing frequency
  double beat = pow(fmax(0, sin(2 * M_PI * 13 * t)), 2);
  return noise() * beat * env(t, 0.24, 1) * 0.3;
}

// --- dogs
// Dog voices are a phase-accumulated glottal pulse train (so pitch bends
// stay clean) shaped by fixed vocal-tract formants via ring modulation.

static double sfx_dog_bark(double t, SynthState *s) {
  double dt = t - s->lt;
  s->lt = t;
  double k = t < 0.24 ? t : t - 0.26; // two barks
  if (k < 0 || k > 0.15) return 0;
  // "wo-of": pitch jumps up fast, then droops
  double f0 = 120 + 330 * sin(M_PI * fmin(1, k / 0.13));
  s->ph += 2 * M_PI * f0 * (1 + 0.06 * noise()) * dt;
  double pulse = pow(0.5 + 0.5 * sin(s->ph), 3);
  double voice = pulse * (sine(k, 620) * 0.8 + sine(k, 1250) * 0.45) + sin(s->ph) * 0.45;
  double breath = noise() * 0.2 * env(k, 0.05, 2);
  double a = fmin(1, k / 0.012) * env(k, 0.15, 1.4);
  return (voice + breath) * a * 0.8;
}

static double sfx_dog_growl(double t, SynthState *s) {
  if (t == 0) s->f = 68; // starting pulse rate
  double dt = t - s->lt;
  s->lt = t;
  // irregular low pulse rate reads as a throaty rattle
  s->f += (66 - s->f) * 0.002 + noise() * 1.4;
  s->ph += 2 * M_PI * s->f * dt;
  double pulse = pow(0.5 + 0.5 * sin(s->ph), 2);
  double voice = pulse * (sine(t, 300) * 0.8 + sine(t, 660) * 0.3) + sin(s->ph) * 0.4;
  double breath = noise() * (0.1 + 0.25 * pulse);
  double e = sin(M_PI * fmin(1, t / 0.6));
  return (voice + breath) * e * 0.55;
}

static double sfx_dog_bite(double t, SynthState *s) {
  // jaws snapping shut
  double snap = noise() * env(t, 0.03, 2) * 0.7;
  double clamp = sine(t, 160 * (1 - t * 2)) * env(t, 0.11, 2) * 0.6;
  return (snap + clamp) * 0.85;
}

static double sfx_dog_pain(double t, SynthState *s) {
  double dt = t - s->lt;
  s->lt = t;
  // sharp "yipe!": shoots up, cracks, tumbles back down
  double f0 = 480 + 750 * sin(M_PI * pow(t / 0.3, 0.6));
  s->ph += 2 * M_PI * f0 * dt;
  double v = sin(s->ph) * 0.6 + sin(2 * s->ph) * 0.3 + sin(3 * s->ph) * 0.12;
  return v * fmin(1, t / 0.008) * env(t, 0.3, 1.5) * 0.5;
}

static double sfx_dog_death(double t, SynthState *s) {
  double dt = t - s->lt;
  s->lt = t;
  double k = t / 0.9;
  // long falling whine, the tremble widening as it fades
  double vib = sin(2 * M_PI * 6.5 * t) * (10 + 60 * k);
  s->ph += 2 * M_PI * fmax(120, 760 - 420 * k + vib) * dt;
  double v = sin(s->ph) * 0.55 + sin(2 * s->ph) * 0.18 + noise() * 0.06 * (1 - k);
  return v * fmin(1, t / 0.03) * env(t, 0.9, 1.3) * 0.5;
}

// --- rats
// Squeaks are phase-accumulated too: they bend a long way in a short time.

static double sfx_rat_alert(double t, SynthState *s) {
  double dt = t - s->lt;
  s->lt = t;
  // a volley of three shrill squeals, each a little lower, over scrabbling claws
  int n = (int)(t / 0.1);
  double k = t - n * 0.1;
  double claws = fmod(t, 0.031) < 0.004 ? noise() * 0.12 : 0;
  if (n > 2 || k > 0.075) return claws;
  s->ph += 2 * M_PI * (3600 - n * 350 + 900 * sin(M_PI * k / 0.075)) * dt;
  return sin(s->ph) * sin(M_PI * k / 0.075) * 0.32 + claws;
}

static double sfx_rat_squeak(double t, SynthState *s) {
  double dt = t - s->lt;
  s->lt = t;
  // a single "eek": rises, wavers, drops away
  double f = 3000 + 1300 * sin(M_PI * fmin(1, t / 0.11)) + sine(t, 38) * 120;
  s->ph += 2 * M_PI * f * dt;
  double v = sin(s->ph) * 0.7 + sin(2 * s->ph) * 0.15;
  return v * fmin(1, t / 0.006) * env(t, 0.12, 1.2) * 0.32;
}

static double sfx_rat_skitter(double t, SynthState *s) {
  // claws pattering over the flagstones: four footfalls to every bound
  static const double feet[] = {0, 0.021, 0.052, 0.07};
  double k = fmod(t, 0.12);
  double tick = 0;
  for (int i = 0; i < 4; i++) {
    double d = k - feet[i];
    if (d >= 0 && d < 0.008) tick += noise() * env(d, 0.008, 2);
  }
  return tick * sin(M_PI * t / 0.5) * 0.4;
}

static double sfx_rat_bite(double t, SynthState *s) {
  // a quick nip: incisors clicking shut, a chirp behind it
  double click = noise() * env(t, 0.015, 2) * 0.6;
  double chirp = sine(t, 2800 - 4000 * t) * env(t, 0.1, 2) * 0.3;
  return (click + chirp) * 0.85;
}

static double sfx_rat_death(double t, SynthState *s) {
  double dt = t - s->lt;
  s->lt = t;
  double k = t / 0.55;
  // a last thin shriek, sagging and wavering as it fades
  double f = 3800 * (1 - 0.55 * k) + sin(2 * M_PI * 22 * t) * 260 * k;
  s->ph += 2 * M_PI * f * dt;
  double v = sin(s->ph) * 0.6 + sin(2 * s->ph) * 0.12;
  return v * fmin(1, t / 0.01) * env(t, 0.55, 1.6) * 0.4;
}

// --- spiders
// No voice at all: hisses are noise with the lows differenced away, the
// rest is chitin clicking and scraping.

static double hiss(SynthState *s) {
  double n = noise();
  double v = n - s->z;
  s->z = n;
  return v * 0.5;
}

static double sfx_spider_hiss(double t, SynthState *s) {
  // rearing up: a long rasping hiss over clattering fangs
  double rasp = hiss(s) * (0.75 + 0.25 * sine(t, 31));
  double clack = t < 0.3 && fmod(t, 0.06) < 0.005 ? noise() * 0.6 : 0;
  double a = fmin(1, t / 0.05) * env(t, 0.6, 1.3);
  return (rasp * 0.7 + clack) * a * 0.75;
}

static double sfx_spider_chitter(double t, SynthState *s) {
  // mandibles working: a dry run of clicks, quickening, then trailing off
  static const double clicks[] = {0, 0.045, 0.08, 0.105, 0.125, 0.15, 0.2};
  double v = 0;
  for (int i = 0; i < 7; i++) {
    double d = t - clicks[i];
    if (d >= 0 && d < 0.007) v += (hiss(s) + sine(d, 2600 + i * 150) * 0.5) * env(d, 0.007, 2);
  }
  return v * 0.45;
}

static double sfx_spider_skitter(double t, SynthState *s) {
  // eight legs rattling over the flagstones in two staggered sets
  static const double feet[] = {0, 0.013, 0.029, 0.041, 0.06, 0.071, 0.088, 0.1};
  double k = fmod(t, 0.11);
  double tick = 0;
  for (int i = 0; i < 8; i++) {
    double d = k - feet[i];
    if (d >= 0 && d < 0.005) tick += hiss(s) * env(d, 0.005, 2);
  }
  return tick * sin(M_PI * t / 0.45) * 0.55;
}

static double sfx_spider_bite(double t, SynthState *s) {
  // fangs clacking shut, then a wet squelch as they sink in
  double clack = t < 0.07 && fmod(t, 0.035) < 0.006 ? noise() * 0.6 : 0;
  s->z += (noise() - s->z) * 0.08; // smoothed noise: a low, wet gurgle
  double squelch = t > 0.04 ? s->z * 3 * env(t - 0.04, 0.14, 1.5) : 0;
  return (clack + squelch) * 0.8;
}

static double sfx_spider_pain(double t, SynthState *s) {
  double dt = t - s->lt;
  s->lt = t;
  // a shrill, grating screech: the hiss chopped by a wavering whistle
  s->ph += 2 * M_PI * (1900 + 500 * sine(t, 17)) * dt;
  double v = hiss(s) * (0.5 + 0.5 * sin(s->ph)) + sin(s->ph) * 0.25;
  return v * fmin(1, t / 0.01) * env(t, 0.28, 1.4) * 0.6;
}

static double sfx_spider_death(double t, SynthState *s) {
  // a long hiss sagging away, the legs crackling as they curl up
  double k = t / 0.9;
  double rasp = hiss(s) * env(t, 0.9, 1.6) * (1 - 0.5 * k);
  double crack = t > 0.15 && fmod(t * (8 + 14 * k), 1) < 0.08 ? noise() * 0.5 * (1 - k) : 0;
  return (rasp * 0.6 + crack) * fmin(1, t / 0.02) * 0.7;
}

// --- pickups

static double sfx_pickup(double t, SynthState *s) { return sine(t, 620 + 1800 * t) * env(t, 0.14, 3) * 0.4; }

static double sfx_pickup_ammo(double t, SynthState *s) { return square(t, 330 + 500 * t) * env(t, 0.12, 3) * 0.25; }

static double sfx_pickup_health(double t, SynthState *s) {
  double f = t < 0.12 ? 520 : 780;
  return sine(t, f) * env(fmod(t, 0.14), 0.13, 3) * 0.4;
}

static double sfx_pickup_weapon(double t, SynthState *s) {
  static const double steps[] = {260, 330, 392, 523};
  double f = steps[mini(3, (int)floor(t / 0.1))];
  return (square(t, f) * 0.3 + sine(t, f * 2) * 0.2) * env(fmod(t, 0.11), 0.1, 3) * 0.9;
}

static double sfx_pickup_key(double t, SynthState *s) {
  static const double steps[] = {660, 880, 1320};
  double f = steps[mini(2, (int)floor(t / 0.15))];
  return sine(t, f) * env(fmod(t, 0.16), 0.15, 3) * 0.45;
}

static double sfx_extra_life(double t, SynthState *s) {
  static const double steps[] = {523, 659, 784, 1047, 1319};
  double f = steps[mini(4, (int)floor(t / 0.16))];
  return (sine(t, f) + sine(t, f * 2) * 0.3) * env(fmod(t, 0.17), 0.16, 3) * 0.4;
}

// --- doors / exit

static double sfx_door(double t, SynthState *s) {
  double grind = noise() * 0.18;
  double hum = sine(t, 70 + 90 * t) * 0.3;
  double e = sin(M_PI * fmin(1, t / 0.5));
  return (grind + hum) * e;
}

static double sfx_lever(double t, SynthState *s) {
  // iron handle thrown: a sharp catch, then the counterweight thunk
  double click = t < 0.03 ? noise() * 0.6 + square(t, 1300) * 0.4 : 0;
  double thunk = t > 0.09 ? (sine(t - 0.09, 95) * 0.6 + noise() * 0.12) * env(t - 0.09, 0.2, 2) : 0;
  return click * env(t, 0.03, 1) + thunk;
}

static double sfx_secret(double t, SynthState *s) {
  // hidden masonry grinding aside: slow pulsing stone-on-stone rumble
  double grind = noise() * (0.5 + 0.5 * sin(2 * M_PI * 9 * t)) * 0.22;
  double rumble = sine(t, 46 + sine(t, 0.7) * 8) * 0.4;
  double e = sin(M_PI * fmin(1, t / 1.3));
  return (grind + rumble) * e;
}

static double sfx_portal(double t, SynthState *s) {
  // glassy vortex: a hum that sweeps up and folds back on itself,
  // dusted with a faint high chime
  double f = 220 + 760 * sin((M_PI * t) / 0.8);
  double swirl = sine(t, f + sine(t, 9) * 40) * 0.4;
  double chime = sine(t, 1319) * env(fmod(t, 0.2), 0.18, 2) * 0.1;
  double e = sin(M_PI * fmin(1, t / 0.8));
  return (swirl + chime + noise() * 0.04) * e * 0.85;
}

static double sfx_locked(double t, SynthState *s) {
  bool on = fmod(t, 0.16) < 0.11;
  return on ? square(t, 130) * 0.35 : 0;
}

static double sfx_exit_open(double t, SynthState *s) {
  static const double steps[] = {392, 523, 659, 784};
  double f = steps[mini(3, (int)floor(t / 0.22))];
  return (square(t, f) * 0.25 + sine(t, f) * 0.35) * env(fmod(t, 0.23), 0.22, 3) * 0.9;
}

// --- jingles

static double sfx_level_done(double t, SynthState *s) {
  static const double steps[] = {523, 659, 784, 1047, 784, 1047};
  double f = steps[mini((int)SDL_arraysize(steps) - 1, (int)floor(t / 0.22))];
  return (sine(t, f) * 0.4 + square(t, f / 2) * 0.15) * env(fmod(t, 0.23), 0.22, 1.5);
}

static double sfx_game_over(double t, SynthState *s) {
  static const double steps[] = {440, 415, 392, 311};
  double f = steps[mini((int)SDL_arraysize(steps) - 1, (int)floor(t / 0.4))];
  return (saw(t, f) * 0.3 + sine(t, f / 2) * 0.3) * env(fmod(t, 0.41), 0.4, 1.2);
}

static double sfx_victory(double t, SynthState *s) {
  static const double steps[] = {523, 523, 523, 659, 784, 659, 784, 1047, 1047};
  double f = steps[mini((int)SDL_arraysize(steps) - 1, (int)floor(t / 0.28))];
  return (sine(t, f) * 0.4 + square(t, f) * 0.12 + sine(t, f * 2) * 0.15) * env(fmod(t, 0.29), 0.28, 1.3);
}

static double sfx_blip(double t, SynthState *s) { return square(t, 700) * env(t, 0.06, 4) * 0.25; }

static double sfx_start(double t, SynthState *s) {
  static const double steps[] = {330, 440, 660};
  double f = steps[mini(2, (int)floor(t / 0.16))];
  return square(t, f) * env(fmod(t, 0.17), 0.16, 3) * 0.3;
}

// Sparse, distant hauntings. Soft attacks avoid clicks and leave real combat
// warnings distinguishable from the fortress's less explicable noises.
static double sfx_horror_wind(double t, SynthState *s) {
  s->z += (noise() - s->z) * .008;
  double fade = sq(sin(M_PI * t / 4));
  return (s->z * 1.6 + sine(t, 47) * .06 + sine(t, 49.3) * .035) * fade;
}

static double sfx_horror_knock(double t, SynthState *s) {
  double beat = t < .37 ? t : t < .92 ? t - .37 : t - .92;
  double strike = (sine(beat, 112) * .5 + sine(beat, 203) * .18 + noise() * .13) * env(beat, .24, 3);
  return strike * fmin(1, beat / .003);
}

static double sfx_horror_scrape(double t, SynthState *s) {
  s->z += (noise() - s->z) * .055;
  double scrape = sine(t, 310 + 50 * sin(t * 17)) * .1 + s->z * .75;
  return scrape * sq(sin(M_PI * t / 2.2)) * (.45 + .55 * sq(sin(t * 23)));
}

static double sfx_warden_step(double t, SynthState *s) {
  s->z += (noise() - s->z) * .045;
  double impact = sine(t, 61) * .7 + sine(t, 106) * .22 + s->z * 1.5;
  return impact * env(t, .65, 2.5) * fmin(1, t / .004);
}

static double sfx_horror_steps(double t, SynthState *s) {
  double beat = fmod(t, .83);
  return sfx_warden_step(beat, s) * (.5 - .08 * floor(t / .83));
}

static double sfx_horror_breath(double t, SynthState *s) {
  s->z += (noise() - s->z) * .09;
  double fade = sq(sin(M_PI * t / 2.7));
  return (s->z * .7 + sine(t, 92 + 3 * sin(t * 5)) * .045) * fade * (.7 + .3 * sin(t * 11));
}

static double sfx_horror_bell(double t, SynthState *s) {
  double bell = sine(t, 74) * .36 * exp(-t * .8) + sine(t, 113.7) * .2 * exp(-t) +
                sine(t, 179.1) * .13 * exp(-t * 1.4) + sine(t, 306.3) * .07 * exp(-t * 2);
  return bell * fmin(1, t / .008) * fmin(1, (5 - t) / .3);
}

static double sfx_warden_chain(double t, SynthState *s) {
  s->z += (noise() - s->z) * .15;
  double links = sine(t, 740) * .16 + sine(t, 1083) * .09 + s->z * .4;
  return links * (.2 + .8 * pow(fmax(0, sin(t * 41)), 4)) * env(t, 1.6, 1.3) * fmin(1, t / .015);
}

static double sfx_parchment(double t, SynthState *s) {
  s->z += (noise() - s->z) * .25;
  return s->z * .35 * sq(sin(M_PI * t / .3));
}

typedef struct {
  double duration; // seconds
  SynthFn fn;
} SynthDef;

static const SynthDef SYNTHS[SND_COUNT] = {
  [SND_CROSSBOW] = {0.3, sfx_crossbow},
  [SND_ARBALEST] = {0.13, sfx_arbalest},
  [SND_DAGGER] = {0.16, sfx_dagger},
  [SND_EMPTY] = {0.06, sfx_empty},
  [SND_ENEMY_SHOT] = {0.26, sfx_enemy_shot},
  [SND_TRAP_CLICK] = {0.16, sfx_trap_click},
  [SND_PIT_FALL] = {0.95, sfx_pit_fall},
  [SND_PENDULUM] = {0.45, sfx_pendulum},
  [SND_ARROW_HIT] = {0.09, sfx_arrow_hit},
  [SND_EXPLOSION] = {0.85, sfx_explosion},
  [SND_WALL_HIT] = {0.15, sfx_wall_hit},
  [SND_WALL_BREAK] = {0.8, sfx_wall_break},
  [SND_PLAYER_PAIN] = {0.22, sfx_player_pain},
  [SND_PLAYER_DEATH] = {0.9, sfx_player_death},
  [SND_STEP] = {0.07, sfx_step},
  [SND_VENOM] = {0.32, sfx_venom},
  [SND_ALERT] = {0.3, sfx_alert},
  [SND_KNIFE_SLASH] = {0.18, sfx_knife_slash},
  [SND_PUNCH] = {0.14, sfx_punch},
  [SND_ENEMY_PAIN] = {0.18, sfx_enemy_pain},
  [SND_ENEMY_DEATH] = {0.6, sfx_enemy_death},
  [SND_BAT_ALERT] = {0.4, sfx_bat_alert},
  [SND_BAT_SQUEAK] = {0.14, sfx_bat_squeak},
  [SND_BAT_BITE] = {0.12, sfx_bat_bite},
  [SND_BAT_DEATH] = {0.5, sfx_bat_death},
  [SND_FLUTTER] = {0.24, sfx_flutter},
  [SND_DOG_BARK] = {0.5, sfx_dog_bark},
  [SND_DOG_GROWL] = {0.6, sfx_dog_growl},
  [SND_DOG_BITE] = {0.13, sfx_dog_bite},
  [SND_DOG_PAIN] = {0.3, sfx_dog_pain},
  [SND_DOG_DEATH] = {0.9, sfx_dog_death},
  [SND_RAT_ALERT] = {0.3, sfx_rat_alert},
  [SND_RAT_SQUEAK] = {0.12, sfx_rat_squeak},
  [SND_RAT_SKITTER] = {0.5, sfx_rat_skitter},
  [SND_RAT_BITE] = {0.1, sfx_rat_bite},
  [SND_RAT_DEATH] = {0.55, sfx_rat_death},
  [SND_SPIDER_HISS] = {0.6, sfx_spider_hiss},
  [SND_SPIDER_CHITTER] = {0.21, sfx_spider_chitter},
  [SND_SPIDER_SKITTER] = {0.45, sfx_spider_skitter},
  [SND_SPIDER_BITE] = {0.2, sfx_spider_bite},
  [SND_SPIDER_PAIN] = {0.28, sfx_spider_pain},
  [SND_SPIDER_DEATH] = {0.9, sfx_spider_death},
  [SND_PICKUP] = {0.14, sfx_pickup},
  [SND_PICKUP_AMMO] = {0.12, sfx_pickup_ammo},
  [SND_PICKUP_HEALTH] = {0.3, sfx_pickup_health},
  [SND_PICKUP_WEAPON] = {0.4, sfx_pickup_weapon},
  [SND_PICKUP_KEY] = {0.45, sfx_pickup_key},
  [SND_EXTRA_LIFE] = {0.8, sfx_extra_life},
  [SND_DOOR] = {0.5, sfx_door},
  [SND_LEVER] = {0.3, sfx_lever},
  [SND_SECRET] = {1.3, sfx_secret},
  [SND_PORTAL] = {0.8, sfx_portal},
  [SND_LOCKED] = {0.32, sfx_locked},
  [SND_EXIT_OPEN] = {0.9, sfx_exit_open},
  [SND_LEVEL_DONE] = {1.4, sfx_level_done},
  [SND_GAME_OVER] = {1.6, sfx_game_over},
  [SND_VICTORY] = {2.6, sfx_victory},
  [SND_BLIP] = {0.07, sfx_blip},
  [SND_START] = {0.5, sfx_start},
  [SND_SPLASH] = {0.32, sfx_splash},
  [SND_WATER_FLOW] = {0.9, sfx_water_flow},
  [SND_HORROR_WIND] = {4, sfx_horror_wind},
  [SND_HORROR_KNOCK] = {1.5, sfx_horror_knock},
  [SND_HORROR_SCRAPE] = {2.2, sfx_horror_scrape},
  [SND_HORROR_STEPS] = {2.49, sfx_horror_steps},
  [SND_HORROR_BREATH] = {2.7, sfx_horror_breath},
  [SND_HORROR_BELL] = {5, sfx_horror_bell},
  [SND_WARDEN_STEP] = {.65, sfx_warden_step},
  [SND_WARDEN_CHAIN] = {1.6, sfx_warden_chain},
  [SND_PARCHMENT] = {.3, sfx_parchment},
  [SND_METAL_HIT]={.36,sfx_metal_hit}, [SND_FLESH_HIT]={.16,sfx_flesh_hit},
  [SND_RELOAD]={.18,sfx_reload}, [SND_CAPTAIN_CALL]={.9,sfx_captain_call},
  [SND_STEP_WOOD]={.16,sfx_step_wood}, [SND_STEP_LEAVES]={.18,sfx_step_leaves},
};

static float *buffers[SND_COUNT];
static int lengths[SND_COUNT];

static void synthesize_all(void) {
  for (int id = 0; id < SND_COUNT; id++) {
    const SynthDef *def = &SYNTHS[id];
    int len = maxi(1, (int)floor(def->duration * SAMPLE_RATE));
    float *d = malloc((size_t)len * sizeof(float));
    if (!d) continue;
    SynthState st = {0};
    for (int i = 0; i < len; i++) d[i] = (float)def->fn((double)i / SAMPLE_RATE, &st);
    buffers[id] = d;
    lengths[id] = len;
  }
}

static void free_buffers(void) {
  for (int id = 0; id < SND_COUNT; id++) {
    free(buffers[id]);
    buffers[id] = NULL;
    lengths[id] = 0;
  }
}

// ------------------------------------------------------------------ mixer

typedef struct {
  const float *data; // NULL while the slot is free
  int len;
  double pos;  // fractional read position in samples
  double rate; // read-position advance per output frame
  float gain;
  float left, right; // pan gains
  bool filtered;
  double b0, b1, b2, a1, a2; // low-pass coefficients, normalized by a0
  double x1, x2, y1, y2;     // low-pass history
} Voice;

static SDL_AudioStream *stream; // NULL while audio is unavailable
static Voice voices[MAX_VOICES]; // guarded by the stream lock
static bool enabled = true;
static OcclusionFn occlusion;
static void *occlusion_ctx;

// Original chapter motifs, composed as two soft stems. Preparation uses its own
// deterministic noise source and never consumes the combat random sequence.
#define MUSIC_RATE 22050
#define MUSIC_LENGTH (MUSIC_RATE*128/5)
static float *music_stems[LEVEL_COUNT][2];
static int music_chapter;
static double music_position;
static float music_danger,music_target_gain,music_gain,music_blend;
static void prepare_music(void) {
  static const double roots[]={55,49,65.406391,43.653529,73.416192};
  static const int motifs[5][16]={{0,7,3,10,7,3,2,7,0,3,7,12,10,7,3,2},
    {0,7,12,10,3,7,2,0,0,12,7,3,10,7,2,-5},
    {0,3,7,10,7,12,10,7,0,7,3,2,7,10,3,0},
    {0,1,7,3,0,6,3,1,0,7,1,3,6,3,1,-5},
    {0,7,12,15,10,7,3,2,0,3,7,12,15,10,7,0}};
  for (int chapter=0;chapter<LEVEL_COUNT;chapter++) {
    music_stems[chapter][0]=malloc(MUSIC_LENGTH*sizeof(float));
    music_stems[chapter][1]=malloc(MUSIC_LENGTH*sizeof(float));
    if (!music_stems[chapter][0] || !music_stems[chapter][1]) continue;
    double freq[16]; for (int n=0;n<16;n++) freq[n]=roots[chapter]*4*pow(2,motifs[chapter][n]/12.0);
    Lcg random=lcg(0x724f13u+chapter); double filtered=0;
    for (int i=0;i<MUSIC_LENGTH;i++) {
      double t=(double)i/MUSIC_RATE;
      int note=(int)(t/1.6)%16; double age=fmod(t,1.6),envelope=(1-exp(-age*6))*exp(-age*2.3);
      double string=(sine(t,freq[note])*.48+sine(t,freq[note]*2)*.18+sine(t,freq[note]*3)*.08)*envelope;
      double breath=.7+.3*sin(2*M_PI*t/12.8);
      double drone=(sine(t,roots[chapter])*.10+sine(t,roots[chapter]*1.5)*.055)*breath;
      double fade=fmin(1,fmin(t/1.2,(25.6-t)/1.2));
      filtered+=((lcg_next(&random)*2-1)-filtered)*.12;
      double beat=fmod(t,.8),drum=sine(beat,65*exp(-beat*4))*exp(-beat*18)*.13;
      double rattle=filtered*exp(-fmod(t+.4,.8)*25)*.045;
      music_stems[chapter][0][i]=(float)((drone+string*.16)*fade);
      music_stems[chapter][1][i]=(float)((drone+string*.55+drum+rattle)*fade);
    }
  }
}
void audio_set_mix(int effects,int music) {
  if (stream) SDL_LockAudioStream(stream);
  effects_volume=clampi(effects,0,100); music_volume=clampi(music,0,100);
  if (stream) SDL_UnlockAudioStream(stream);
}
void audio_set_scene(int chapter,float danger,bool shelter,bool active) {
  if (stream) SDL_LockAudioStream(stream);
  chapter=clampi(chapter,0,LEVEL_COUNT-1);
  if (music_chapter!=chapter) { music_chapter=chapter; music_position=0; music_gain=0; }
  music_danger=clampf(danger,0,1);
  music_target_gain=active && !shelter?.7f:0;
  if (stream) SDL_UnlockAudioStream(stream);
}

// RBJ cookbook low-pass with the resonance given in dB. Cutoffs at or above
// Nyquist pass everything, so they leave the voice unfiltered.
static void voice_set_lowpass(Voice *v, double cutoff) {
  if (cutoff <= 0 || cutoff >= SAMPLE_RATE / 2.0) return;
  double w0 = 2 * M_PI * cutoff / SAMPLE_RATE;
  double alpha = sin(w0) / (2 * pow(10, FILTER_Q / 20));
  double cw = cos(w0);
  double a0 = 1 + alpha;
  v->b0 = (1 - cw) / 2 / a0;
  v->b1 = (1 - cw) / a0;
  v->b2 = v->b0;
  v->a1 = -2 * cw / a0;
  v->a2 = (1 - alpha) / a0;
  v->filtered = true;
}

// Adds up to `frames` stereo frames of one voice into `out`, freeing the
// slot once the read position runs past the end of the buffer.
static void mix_voice(Voice *v, float *out, int frames) {
  for (int i = 0; i < frames; i++) {
    int idx = (int)v->pos;
    float frac = (float)(v->pos - idx);
    float s0 = v->data[idx];
    float s1 = idx + 1 < v->len ? v->data[idx + 1] : 0.0f;
    float x = (s0 + (s1 - s0) * frac) * v->gain;
    if (v->filtered) {
      double y = v->b0 * x + v->b1 * v->x1 + v->b2 * v->x2 - v->a1 * v->y1 - v->a2 * v->y2;
      v->x2 = v->x1;
      v->x1 = x;
      v->y2 = v->y1;
      v->y1 = y;
      x = (float)y;
    }
    out[i * 2] += x * v->left;
    out[i * 2 + 1] += x * v->right;
    v->pos += v->rate;
    if (v->pos >= v->len) {
      v->data = NULL;
      return;
    }
  }
}

// Device callback; SDL holds the stream lock while it runs.
static void SDLCALL mix_audio(void *userdata, SDL_AudioStream *out, int additional_amount, int total_amount) {
  float mix[MIX_FRAMES * 2];
  int frames = (additional_amount + FRAME_BYTES - 1) / FRAME_BYTES;
  while (frames > 0) {
    int n = mini(frames, MIX_FRAMES);
    memset(mix, 0, (size_t)n * FRAME_BYTES);
    for (int i = 0; i < MAX_VOICES; i++) {
      if (voices[i].data) mix_voice(&voices[i], mix, n);
    }
    for (int i=0;i<n;i++) {
      music_gain+=(music_target_gain-music_gain)*(1.0f/SAMPLE_RATE);
      music_blend+=(music_danger-music_blend)*(2.0f/SAMPLE_RATE);
      int index=(int)music_position;
      float *quiet=music_stems[music_chapter][0],*fight=music_stems[music_chapter][1];
      if (quiet && fight) {
        float music=(quiet[index]*(1-music_blend)+fight[index]*music_blend)*music_gain*music_volume/100.0f;
        mix[i*2]+=music; mix[i*2+1]+=music;
      }
      music_position+=MUSIC_RATE/(double)SAMPLE_RATE;
      if (music_position>=MUSIC_LENGTH) music_position-=MUSIC_LENGTH;
    }
    for (int i = 0; i < n * 2; i++) mix[i] = enabled?clampf(mix[i] * MASTER_GAIN, -1.0f, 1.0f):0;
    SDL_PutAudioStreamData(out, mix, n * FRAME_BYTES);
    frames -= n;
  }
}

// Free slot if there is one, else the voice closest to finishing.
static Voice *claim_voice(void) {
  Voice *best = NULL;
  double best_left = 0;
  for (int i = 0; i < MAX_VOICES; i++) {
    Voice *v = &voices[i];
    if (!v->data) return v;
    double left = (v->len - v->pos) / v->rate;
    if (!best || left < best_left) {
      best = v;
      best_left = left;
    }
  }
  return best;
}

// ------------------------------------------------------------------ api

bool audio_init(void) {
  if (stream) return true;
  SDL_AudioSpec spec = {SDL_AUDIO_F32, 2, SAMPLE_RATE};
  stream = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, mix_audio, NULL);
  if (!stream) {
    SDL_Log("audio unavailable: %s", SDL_GetError());
    return false;
  }
  // the device opens paused, so nothing reads the buffers until resumed
  memset(voices, 0, sizeof(voices));
  synthesize_all();
  prepare_music();
  if (!SDL_ResumeAudioStreamDevice(stream)) {
    SDL_Log("audio unavailable: %s", SDL_GetError());
    audio_shutdown();
    return false;
  }
  return true;
}

void audio_shutdown(void) {
  if (stream) {
    SDL_DestroyAudioStream(stream);
    stream = NULL;
  }
  memset(voices, 0, sizeof(voices));
  free_buffers();
  for (int c=0;c<LEVEL_COUNT;c++) for (int stem=0;stem<2;stem++) { free(music_stems[c][stem]); music_stems[c][stem]=NULL; }
  music_position=0; music_gain=0;
}

void audio_set_enabled(bool on) {
  if (stream) SDL_LockAudioStream(stream);
  enabled=on;
  if (stream) SDL_UnlockAudioStream(stream);
}

bool audio_enabled(void) { return enabled; }

void audio_set_occlusion(OcclusionFn fn, void *ctx) {
  occlusion = fn;
  occlusion_ctx = ctx;
}

void audio_play(SoundId id, float volume, float rate) { audio_play_ex(id, volume, rate, 0, 0); }

void audio_play_ex(SoundId id, float volume, float rate, float pan, float cutoff) {
  volume*=effects_volume/100.0f;
  if (!stream || !enabled || (unsigned)id >= SND_COUNT || !buffers[id]) return;
  if (!(volume > 0) || !(rate > 0)) return;
  Voice v = {.data = buffers[id], .len = lengths[id], .rate = rate, .gain = volume, .left = 1, .right = 1};
  if (cutoff > 0) voice_set_lowpass(&v, cutoff);
  if (pan != 0) {
    // equal-power pan of the mono signal
    float x = (clampf(pan, -1.0f, 1.0f) + 1) / 2;
    v.left = cosf(x * (float)M_PI / 2);
    v.right = sinf(x * (float)M_PI / 2);
  }
  SDL_LockAudioStream(stream);
  *claim_voice() = v;
  SDL_UnlockAudioStream(stream);
}

void audio_play_at(SoundId id, float x, float y, const Listener *listener, float volume, float rate) {
  if (!stream || !enabled) return;
  if (!listener) {
    audio_play(id, volume, rate);
    return;
  }
  float dx = x - listener->x;
  float dy = y - listener->y;
  float dist = hypotf(dx, dy);
  volume *= fminf(1, 3.5f / (1 + dist * 0.6f));
  // walls between the source and the listener deaden the sound
  float walls = occlusion ? occlusion(occlusion_ctx, listener->x, listener->y, x, y) : 0;
  float cutoff = 0;
  if (walls > 0) {
    volume *= powf(0.35f, walls);
    cutoff = 1100 / walls;
  }
  if (volume < 0.02f) return;
  // project onto the listener's right vector for stereo pan
  float right_x = -listener->dir_y;
  float right_y = listener->dir_x;
  float pan = dist > 0.001f ? ((dx * right_x + dy * right_y) / dist) * 0.7f : 0;
  audio_play_ex(id, volume, rate, pan, cutoff);
}
