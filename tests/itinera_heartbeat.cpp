/* SPDX-License-Identifier: MIT OR GPL-3.0-only
 * 2.4 · a dream never starts or sits silent for long. The generative patterns are sparse and their random state is new
 * on every start (the seed comes from the engine's frame count), so a dream could begin with half a minute of nothing,
 * or go quiet for 40 s in its middle: the first audible note within 3 s, and no note-free stretch beyond 11 s.
 * Mode: tools/build_local.py itinerahb, then app/build/native/itinerahb-tests */
#include "dsp/dreams.h"
#include <cassert>
#include <cmath>
#include <cstdio>
#include <vector>
static float lr[256];
int main() {
 unsigned worst_first = 0, worst_gap = 0; int bad_preset = -1;
 for (int preset = 0; preset < MD_ITINERA_PRESETS; preset++) {
  for (int trial = 0; trial < 3; trial++) {
   md_init();
   /* a different engine frame count at the start gives a different dream seed, as a real start does */
   for (int k = 0; k < 37 + trial * 4999 + preset * 211; k++) md_render(lr, 128);
   const unsigned def = md_itinera_defaults((unsigned)preset);
   assert(md_command(MD_ITINERA_START, preset, (int)(0 + 4 * (((def - 1) / 4) % 3) + 12 * ((def - 1) / 12))));
   uint32_t last = 0; unsigned first = 0, gap = 0, max_gap = 0; bool heard = false;
   const unsigned seconds = 150;
   for (unsigned s = 0; s < seconds; s++) {
    uint32_t events = 0;
    for (unsigned k = 0; k < MD_RATE / 128; k++) {
     md_render(lr, 128);
     for (unsigned j = 0; j < 256; j++) assert(std::isfinite(lr[j]));
    }
    for (int t = 0; t < 6; t++) events += md_status()->events[t];
    if (events != last) { heard = true; gap = 0; last = events; }
    else { gap++; if (!heard) first = s + 1; if (heard && gap > max_gap) max_gap = gap; }
   }
   if (first > worst_first) worst_first = first;
   if (max_gap > worst_gap) { worst_gap = max_gap; bad_preset = preset; }
  }
 }
 printf("longest start without a note: %u s; longest note-free stretch: %u s (preset %d)\n", worst_first, worst_gap, bad_preset);
 assert(worst_first <= 3);
 puts("PASS every dream sounds within 3 s of its start");
 assert(worst_gap <= 11);
 puts("PASS no dream goes without a note for more than 11 s");
 return 0;
}
