// Gameplay randomness (xorshift64*).

#include "common.h"

static uint64_t rng_state = 0x9E3779B97F4A7C15ull;

void random_seed(uint64_t seed) { rng_state = seed ? seed : 0x9E3779B97F4A7C15ull; }

uint64_t random_state(void) { return rng_state; }

double frand(void) {
  rng_state ^= rng_state >> 12;
  rng_state ^= rng_state << 25;
  rng_state ^= rng_state >> 27;
  return (double)((rng_state * 0x2545F4914F6CDD1Dull) >> 11) / 9007199254740992.0;
}
