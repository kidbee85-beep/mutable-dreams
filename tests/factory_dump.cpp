// 2.2 development: every factory Biome as loaded (md_patch bytes, card and names), for byte-exact
// comparison of preset-table encodings. Usage: factorydump-tests OUT.bin
#include "dsp/dreams.h"
#include <cassert>
#include <cstdio>
#include <cstring>
int main(int argc, char **argv) {
  if (argc != 2) return 2;
  FILE *out = std::fopen(argv[1], "wb");
  assert(out);
  float stereo[256];
  for (int id = 0; id < MD_BIOMES; ++id) {
    md_init();
    assert(md_command(MD_SCENE, 0, id));
    md_render(stereo, 128);
    std::fwrite(md_state(), sizeof(md_patch), 1, out);
    md_biome_card card;
    std::memset(&card, 0, sizeof card);
    md_biome_card_info(id, &card);
    std::fwrite(&card, sizeof card, 1, out);
    std::fprintf(out, "%s|%s|%d\n", md_biome_name(id), md_biome_family(id), md_biome_at((unsigned)id));
  }
  for (int id = 0; id < MD_TONE_PRESETS; ++id) std::fprintf(out, "%s|%d\n", md_tone_name(id), md_tone_model(id));
  std::fclose(out);
  return 0;
}
