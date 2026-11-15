// 2.2 development: BIOME CHANGE listening and click check. For each Biome pair FROM:TO, plays FROM,
// chooses TO at 6.1 s and renders to 22 s, once per mode (0 NOW, 1 NEXT BAR, 2 TRACK BY TRACK).
// Writes OUT/FROM-TO-mN.f32 (stereo, 44.1 kHz). At every stage (a block in which a track's model,
// the Biome or the arrival changes) it prints the click measure: the loudest 5 ms window of the
// first difference (high-frequency energy) within 20 ms of the stage, against the median of the
// second before it, in dB; and the level of the 250 ms after the stage against the 250 ms before.
// Usage: biomechange-tests OUT FROM:TO...
#include "dsp/dreams.h"
#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <vector>
#include <sys/stat.h>
int main(int argc, char **argv) {
  if (argc < 3) return 2;
  mkdir(argv[1], 0755);
  const unsigned at = unsigned(6.1f * MD_RATE), total = 22u * MD_RATE, win = MD_RATE / 200;
  float lr[256];
  for (int k = 2; k < argc; ++k) {
    int from = 0, to = 0;
    if (std::sscanf(argv[k], "%d:%d", &from, &to) != 2) return 2;
    for (int mode = 0; mode < 3; ++mode) {
      md_init();
      md_set_biome_change(mode);
      assert(md_command(MD_SCENE, 0, from));
      md_render(lr, 128);
      assert(md_command(MD_PLAY, 0, 1));
      char path[1024];
      std::snprintf(path, sizeof path, "%s/%03d-%03d-m%d.f32", argv[1], from, to, mode);
      FILE *out = std::fopen(path, "wb");
      std::vector<float> hf, rms;
      std::vector<unsigned> stages;
      auto sig = [] { const md_patch *p = md_state(); unsigned s = p->biome_id * 7u + md_biome_arriving() * 131u; for (int i = 0; i < 6; ++i) s = s * 31u + p->track[i].model; return s; };
      unsigned was = sig();
      float racc = 0;
      float last = 0, acc = 0;
      unsigned n = 0;
      for (unsigned t = 0; t < total; t += 128) {
        if (t <= at && at < t + 128) assert(md_command(MD_SCENE, 0, to));
        md_render(lr, 128);
        if (sig() != was) { was = sig(); stages.push_back(t); }
        std::fwrite(lr, sizeof(float), 256, out);
        for (int i = 0; i < 128; ++i) {
          const float m = .5f * (lr[2 * i] + lr[2 * i + 1]), d = m - last;
          last = m;
          acc += d * d;
          racc += m * m;
          if (++n == win) { hf.push_back(acc / win); rms.push_back(racc / win); acc = racc = 0; n = 0; }
        }
      }
      std::fclose(out);
      std::printf("%d->%d mode %d: arrived %d, nonfinite %u\n", from, to, mode, md_state()->biome_id, md_status()->nonfinite);
      for (unsigned s : stages) {
        const size_t c = s / win, r = 4, m1 = MD_RATE / win, q = MD_RATE / 4 / win;
        std::vector<float> before(hf.begin() + c - m1, hf.begin() + c);
        std::nth_element(before.begin(), before.begin() + before.size() / 2, before.end());
        float peak = 0, pre = 0, post = 0;
        for (size_t i = c - r; i <= c + r && i < hf.size(); ++i) peak = std::max(peak, hf[i]);
        for (size_t i = 0; i < q; ++i) { pre += rms[c - 1 - i]; if (c + i < rms.size()) post += rms[c + i]; }
        std::printf("  stage %.2f s: click %+.1f dB, level after %+.1f dB\n", s / (float)MD_RATE,
                    10 * std::log10((peak + 1e-12f) / (before[before.size() / 2] + 1e-12f)), 10 * std::log10((post + 1e-12f) / (pre + 1e-12f)));
      }
    }
  }
  return 0;
}
