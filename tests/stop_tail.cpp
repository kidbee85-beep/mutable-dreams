// 2.2 development: listening comparison for idle-voice sleep. Each Biome plays PLAY_S seconds, then
// is stopped for STOP_S seconds. Writes OUT/NNN.f32 (stereo, 44.1 kHz) and OUT/manifest.tsv with the
// rendered voice quanta while playing and after the stop (the CPU proxy).
// Usage: stoptail-tests OUT PLAY_S STOP_S [ID...]
#include "dsp/dreams.h"
#include <cassert>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <sys/stat.h>
int main(int argc, char **argv) {
  if (argc < 4) return 2;
  mkdir(argv[1], 0755);
  const unsigned play = unsigned(std::atoi(argv[2])) * MD_RATE, stop = unsigned(std::atoi(argv[3])) * MD_RATE;
  char path[1024];
  std::snprintf(path, sizeof path, "%s/manifest.tsv", argv[1]);
  FILE *man = std::fopen(path, "wb");
  std::fprintf(man, "id\tname\tquanta_play\tquanta_stop\tskipped_stop\tnonfinite\n");
  float lr[256];
  const int first = argc > 4 ? 4 : 0;
  const int count = argc > 4 ? argc - 4 : MD_BIOMES;
  for (int k = 0; k < count; ++k) {
    const int id = first ? std::atoi(argv[first + k]) : k;
    md_init();
    assert(md_command(MD_SCENE, 0, id));
    md_render(lr, 128);
    std::snprintf(path, sizeof path, "%s/%03d.f32", argv[1], id);
    FILE *out = std::fopen(path, "wb");
    assert(md_command(MD_PLAY, 0, 1));
    unsigned q0 = md_status()->voice_quanta, q1 = 0, s1 = 0;
    bool stopped = false;
    for (unsigned at = 0; at < play + stop; at += 128) {
      if (!stopped && at >= play) { stopped = true; assert(md_command(MD_PLAY, 0, 0)); q1 = md_status()->voice_quanta; s1 = md_status()->skipped_quanta; }
      md_render(lr, 128);
      if (std::getenv("STOPTAIL_TRACE") && at % (MD_RATE * 5) < 128) { std::fprintf(stderr, "%d t=%5.1f q=%u peaks", id, at / (float)MD_RATE, md_status()->voice_quanta); for (int t = 0; t < 6; ++t) std::fprintf(stderr, " %.1e", md_status()->track_peak[t]); std::fprintf(stderr, "\n"); }
      std::fwrite(lr, sizeof(float), 256, out);
    }
    std::fclose(out);
    const md_stats *s = md_status();
    std::fprintf(man, "%d\t%s\t%u\t%u\t%u\t%u\n", id, md_biome_name(id), q1 - q0, s->voice_quanta - q1,
                 s->skipped_quanta - s1, s->nonfinite);
  }
  std::fclose(man);
  return 0;
}
