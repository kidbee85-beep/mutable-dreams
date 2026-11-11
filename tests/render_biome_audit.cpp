// Offline factory sound audit. No firmware code or saved Biome is changed.
#include "dsp/dreams.h"
#include <cassert>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <sys/stat.h>

int main(int argc, char **argv) {
  if (argc != 3) {
    std::fprintf(stderr, "Usage: biomes-tests OUTPUT_DIR SECONDS\n");
    return 2;
  }
  const int seconds = std::atoi(argv[2]);
  if (seconds < 1 || seconds > 16) return 2;
  mkdir(argv[1], 0755);
  char path[1024];
  std::snprintf(path, sizeof path, "%s/manifest.tsv", argv[1]);
  FILE *manifest = std::fopen(path, "wb");
  assert(manifest);
  std::fprintf(manifest, "id\tname\tpeak\trms\tquality_reductions\tnonfinite\n");
  float stereo[256];
  const unsigned frames = unsigned(MD_RATE * seconds);
  for (int id = 0; id < MD_BIOMES; ++id) {
    md_init();
    assert(md_command(MD_SCENE, 0, id));
    md_render(stereo, 128); // consume the queued scene while stopped
    assert(md_state()->biome_id == id);
    std::snprintf(path, sizeof path, "%s/%03d.f32", argv[1], id);
    FILE *out = std::fopen(path, "wb");
    assert(out);
    assert(md_command(MD_PLAY, 0, 1));
    double squares = 0;
    double peak = 0;
    float accum = 0;
    unsigned decim = 0;
    for (unsigned at = 0; at < frames; at += 128) {
      const unsigned n = frames - at < 128 ? frames - at : 128;
      md_render(stereo, n);
      for (unsigned j = 0; j < n; ++j) {
        const float left = stereo[2 * j], right = stereo[2 * j + 1];
        if (!std::isfinite(left) || !std::isfinite(right)) return 3;
        const float sample = (left + right) * 0.5f;
        squares += double(sample) * sample;
        peak = std::fmax(peak, std::fabs(sample));
        accum += sample;
        if (++decim == 4) {
          float downsampled = accum * 0.25f;
          std::fwrite(&downsampled, sizeof downsampled, 1, out);
          accum = 0;
          decim = 0;
        }
      }
    }
    std::fclose(out);
    const md_stats *s = md_status();
    std::fprintf(manifest, "%d\t%s\t%.6f\t%.6f\t%u\t%u\n",
                 id, md_biome_name(id), peak, std::sqrt(squares / frames),
                 s->quality_reductions, s->nonfinite);
    if (id % 16 == 0) std::fprintf(stderr, "Rendered %d/%d\n", id + 1, MD_BIOMES);
  }
  std::fclose(manifest);
  return 0;
}
