// 2.2 development (#9): offline level / brightness / separation audit of the dark saw
// Biomes against existing ambient Biomes. Six-track mix plus each track solo.
// Brightness = share of energy above ~1.2 kHz (one-pole split). No hardware.
#include "dsp/dreams.h"
#include <cassert>
#include <cmath>
#include <cstdio>
#include <cstdlib>

struct Measure { double rms, peak, bright; unsigned eco; };

static Measure render(int id, int solo, unsigned seconds) {
  md_init();
  assert(md_command(MD_SCENE, 0, id));
  float stereo[256];
  md_render(stereo, 128);
  if (solo >= 0)
    for (int t = 0; t < MD_TRACKS; ++t)
      if (t != solo) assert(md_command(MD_MUTE, t, 1));
  assert(md_command(MD_PLAY, 0, 1));
  const float a = 1.0f - std::exp(-2.0f * 3.14159265f * 1200.0f / MD_RATE);
  float low = 0;
  double all = 0, high = 0, peak = 0;
  const unsigned frames = MD_RATE * seconds;
  for (unsigned at = 0; at < frames; at += 128) {
    md_render(stereo, 128);
    for (unsigned j = 0; j < 128; ++j) {
      const float l = stereo[2 * j], r = stereo[2 * j + 1];
      if (!std::isfinite(l) || !std::isfinite(r)) std::exit(3);
      const float m = 0.5f * (l + r);
      low += (m - low) * a;
      const float h = m - low;
      all += double(m) * m;
      high += double(h) * h;
      peak = std::fmax(peak, std::fmax(std::fabs(l), std::fabs(r)));
    }
  }
  return {std::sqrt(all / frames), peak, all > 0 ? high / all : 0, md_status()->quality_reductions};
}

int main(int argc, char **argv) {
  const unsigned seconds = argc > 1 ? unsigned(std::atoi(argv[1])) : 20;
  const int refs[] = {0, 9, 216, 223, 266, 269, 270, 271};
  std::printf("id\tname\tmix_rms\tmix_peak\tbright\teco\ttrack_rms_share(6)\ttrack_bright(6)\n");
  for (int pass = 0; pass < 2; ++pass) {
    const int count = pass ? MD_BIOMES - MD_DARK_FIRST_BIOME : int(sizeof refs / sizeof *refs);
    for (int k = 0; k < count; ++k) {
      const int id = pass ? MD_DARK_FIRST_BIOME + k : refs[k];
      const Measure mix = render(id, -1, seconds);
      double power[MD_TRACKS], bright[MD_TRACKS], sum = 0;
      for (int t = 0; t < MD_TRACKS; ++t) {
        const Measure s = render(id, t, seconds);
        power[t] = s.rms * s.rms;
        bright[t] = s.bright;
        sum += power[t];
      }
      std::printf("%d\t%s\t%.4f\t%.4f\t%.3f\t%u\t", id, md_biome_name(id), mix.rms, mix.peak, mix.bright, mix.eco);
      for (int t = 0; t < MD_TRACKS; ++t) std::printf("%s%.2f", t ? "," : "", sum > 0 ? power[t] / sum : 0);
      std::printf("\t");
      for (int t = 0; t < MD_TRACKS; ++t) std::printf("%s%.2f", t ? "," : "", bright[t]);
      std::printf("\n");
    }
  }
  return 0;
}
