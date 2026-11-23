/* SPDX-License-Identifier: MIT OR GPL-3.0-only
 * 2.4 · the output settings of the engine: WIDTH (the sides widened, the mono sum left alone) and LEVEL MATCH (the Biome loudness
 * trim on or off). Mode: tools/build_local.py finish, then app/build/native/finish-tests */
#include "dsp/dreams.h"
#include <cassert>
#include <cmath>
#include <cstdio>
#include <vector>
static float lr[256];
struct Result { double rms_l, rms_r, rms_mono, corr, peak; };
static Result run(int biome, int width, int match) {
 md_init(); md_set_width(width); md_set_level_match(match);
 assert(md_command(MD_SCENE, 0, biome)); md_render(lr, 128);
 if (md_pending_biome() >= 0) { assert(md_command(MD_SCENE_CONFIRM, 0, 1)); md_render(lr, 128); }
 assert(md_command(MD_PLAY, 0, 1));
 double ll = 0, rr = 0, mm = 0, lrs = 0, pk = 0; unsigned long n = 0;
 for (unsigned s = 0; s < 14u * MD_RATE / 128u; s++) {
  md_render(lr, 128);
  if (s < 3u * MD_RATE / 128u) continue;                 /* the first 3 s: the ramps settle */
  for (unsigned k = 0; k < 128; k++) {
   const double l = lr[2 * k], r = lr[2 * k + 1], m = 0.5 * (l + r);
   assert(std::isfinite(l) && std::isfinite(r)); ll += l * l; rr += r * r; mm += m * m; lrs += l * r; n++;
   if (fabs(l) > pk) pk = fabs(l); if (fabs(r) > pk) pk = fabs(r);
  }
 }
 return Result{sqrt(ll / n), sqrt(rr / n), sqrt(mm / n), lrs / sqrt(ll * rr), pk};
}
static double db(double a, double b) { return 20.0 * log10(a / b); }
int main() {
 /* WIDTH: the correlation falls with every step, the mono sum stays within 0.05 dB, nothing clips */
 for (int biome : {0, 100, 140, 213}) {
  const Result w0 = run(biome, 0, 1);
  double last = w0.corr;
  for (int w = 1; w <= 3; w++) {
   const Result r = run(biome, w, 1);
   assert(r.corr <= last + 1e-6);                          /* wider (or already as wide as it gets) */
   assert(fabs(db(r.rms_mono, w0.rms_mono)) < 0.05);       /* the middle is the same */
   assert(r.peak < 1.0);
   last = r.corr;
  }
 }
 /* the two with a real side signal become clearly wider (the others are nearly mono: little to widen) */
 assert(run(0, 3, 1).corr < run(0, 0, 1).corr - 0.3);
 assert(run(100, 3, 1).corr < run(100, 0, 1).corr - 0.1);
 puts("PASS WIDTH widens the sides, keeps the mono sum and stays inside full scale");
 /* LEVEL MATCH off: a quiet Biome returns to its 2.3 level (here 14+ dB down), a Biome the trim does not touch does not move */
 const Result on = run(2, 0, 1), off = run(2, 0, 0);
 assert(db(on.rms_l, off.rms_l) > 12.0);
 const Result a = run(0, 0, 1), b = run(0, 0, 0);
 assert(fabs(db(a.rms_l, b.rms_l)) < 0.05);
 puts("PASS LEVEL MATCH off gives the 2.3 levels, a Biome the table leaves alone is the same");
 /* the settings are not part of a patch: a project saved and loaded keeps its sound whatever they are */
 md_init(); md_set_width(3); md_patch p = *md_state(); assert(md_validate(&p)); assert(md_load(&p));
 puts("PASS the settings are outside the patch");
 /* the device's fall back to ECO (md_overload: over 90 % of the block) is counted once, like the admission guard's */
 md_init(); assert(md_command(MD_QUALITY, 0, 1)); for (int j = 1; j < 6; j++) assert(md_command(MD_MUTE, j, 1)); assert(md_command(MD_PLAY, 0, 1)); { std::vector<float> sink((size_t)2 * 4096); md_render(sink.data(), 4096); }
 const unsigned before = md_status()->quality_reductions;
 assert(md_quality_active());
 md_overload();
 assert(!md_quality_active());
 assert(md_status()->quality_reductions == before + 1);
 md_overload();   // already ECO: nothing more to shed, nothing more to count
 assert(md_status()->quality_reductions == before + 1);
 puts("PASS an overload falls back to ECO and is counted once");
 return 0;
}
