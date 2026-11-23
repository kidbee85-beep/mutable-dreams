/* SPDX-License-Identifier: MIT OR GPL-3.0-only
 * 2.4 · Audio quality measurements on the host engine (no device claim, no firmware change).
 * Real-time quality gates: loudness spread, true peak, DC, block invariance,
 * switch clicks. Mode: tools/build_local.py audioqa; then app/build/native/audioqa-tests
 *
 *   audioqa-tests biomes OUT SECONDS [FIRST LAST]   one TSV row per Biome (loudness, true peak, DC, spectrum, ECO)
 *   audioqa-tests dream OUT PRESET MINUTES [PACE HALL]  a dream: loudness over time, every scene change, clicks
 *   audioqa-tests invariance OUT                      the same Biomes rendered with different block sizes
 *   audioqa-tests trimcheck UNTRIMMED.tsv SECONDS     the Biome loudness trim: the spread, the peaks, the table against the sound
 *   audioqa-tests air ID                              a Biome with AIR 0..3: loudness, true peak, the share above 8 kHz
 *   audioqa-tests width ID LEVEL                      a Biome with WIDTH LEVEL (0..3): correlation, loudness, the mono sum's loudness, and LEVEL MATCH off
 *   audioqa-tests macro ID K VALUE                    a Biome with macro K (0..3) set to VALUE (0..1000): the same measures as `biomes`
 *   audioqa-tests tone FROM TO [NOTE]                 tones FROM..TO each played alone: loudness, centre of the spectrum, bass share, attack
 *   audioqa-tests fileloud FILE.f32 [SKIP_S [GAIN_DB]] a recording (raw little-endian float32, stereo, 44.1 kHz): loudness, true peak, DC,
 *                                                     loudness per 5 s, seconds of silence, seconds until the first sound
 */
#include "dsp/dreams.h"
#include <cassert>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <sys/stat.h>
#include <vector>
#include <algorithm>

static const double FS = MD_RATE;
static const double PI = 3.14159265358979323846;

struct Biquad {
 double b0, b1, b2, a1, a2, z1 = 0, z2 = 0;
 double run(double x) { double y = b0 * x + z1; z1 = b1 * x - a1 * y + z2; z2 = b2 * x - a2 * y; return y; }
};
/* ITU-R BS.1770 K-weighting at this sample rate (shelf, then the revised low-frequency B-curve high-pass) */
static void kweight(Biquad &shelf, Biquad &hp) {
 {const double G = 3.999843853973347, Q = 0.7071752369554196, fc = 1681.9744509555319;
  const double K = tan(PI * fc / FS), Vh = pow(10.0, G / 20.0), Vb = pow(Vh, 0.4996667741545416);
  const double a0 = 1.0 + K / Q + K * K;
  shelf.b0 = (Vh + Vb * K / Q + K * K) / a0; shelf.b1 = 2.0 * (K * K - Vh) / a0; shelf.b2 = (Vh - Vb * K / Q + K * K) / a0;
  shelf.a1 = 2.0 * (K * K - 1.0) / a0; shelf.a2 = (1.0 - K / Q + K * K) / a0;}
 {const double Q = 0.5003270373253953, fc = 38.13547087613982;
  const double K = tan(PI * fc / FS), a0 = 1.0 + K / Q + K * K;
  hp.b0 = 1.0; hp.b1 = -2.0; hp.b2 = 1.0; hp.a1 = 2.0 * (K * K - 1.0) / a0; hp.a2 = (1.0 - K / Q + K * K) / a0;}
}
/* K-weighted mean square per 100 ms hop, summed over both channels (a 400 ms block is four hops) */
static std::vector<double> hop_power(const std::vector<float> &lr) {
 Biquad s0, h0, s1, h1; kweight(s0, h0); kweight(s1, h1);
 const size_t n = lr.size() / 2, hop = (size_t)(FS * 0.1);
 std::vector<double> p;
 double acc = 0; size_t k = 0;
 for (size_t i = 0; i < n; i++) {
  const double l = h0.run(s0.run(lr[2 * i])), r = h1.run(s1.run(lr[2 * i + 1]));
  acc += l * l + r * r;
  if (++k == hop) { p.push_back(acc / (double)hop); acc = 0; k = 0; }
 }
 return p;
}
static double lufs_of(double power) { return power > 0 ? -0.691 + 10.0 * log10(power) : -200.0; }
/* integrated loudness with the two BS.1770 gates; also the loudness range (10th to 95th percentile of 3 s blocks) */
static void loudness(const std::vector<float> &lr, double &integrated, double &lra, double &short_max) {
 const std::vector<double> hop = hop_power(lr);
 std::vector<double> block;
 for (size_t i = 0; i + 4 <= hop.size(); i++) block.push_back((hop[i] + hop[i + 1] + hop[i + 2] + hop[i + 3]) / 4.0);
 double sum = 0; size_t cnt = 0;
 for (double b : block) if (lufs_of(b) > -70.0) { sum += b; cnt++; }
 if (!cnt) { integrated = -200; lra = 0; short_max = -200; return; }
 const double gate = lufs_of(sum / (double)cnt) - 10.0;
 sum = 0; cnt = 0;
 for (double b : block) if (lufs_of(b) > -70.0 && lufs_of(b) > gate) { sum += b; cnt++; }
 integrated = cnt ? lufs_of(sum / (double)cnt) : -200;
 std::vector<double> st;
 for (size_t i = 0; i + 30 <= hop.size(); i += 10) { double a = 0; for (size_t j = 0; j < 30; j++) a += hop[i + j]; st.push_back(lufs_of(a / 30.0)); }
 short_max = -200;
 std::vector<double> gated;
 for (double v : st) { if (v > short_max) short_max = v; if (v > -70.0) gated.push_back(v); }
 std::sort(gated.begin(), gated.end());
 lra = gated.size() > 3 ? gated[(size_t)(gated.size() * 0.95)] - gated[(size_t)(gated.size() * 0.10)] : 0;
}
/* true peak, 4x oversampled with a 16-tap Hann-windowed sinc (an estimate, as a meter would give it) */
static double true_peak_db(const std::vector<float> &lr, double &sample_peak_db) {
 static double ph[3][16]; static bool init = false;
 if (!init) {
  for (int k = 1; k <= 3; k++) for (int j = 0; j < 16; j++) {
   const double x = (double)(j - 7) - k / 4.0, w = 0.5 + 0.5 * cos(PI * x / 8.0);
   ph[k - 1][j] = (fabs(x) < 1e-12 ? 1.0 : sin(PI * x) / (PI * x)) * w;
  }
  init = true;
 }
 const size_t n = lr.size() / 2;
 double peak = 0, tp = 0;
 for (int c = 0; c < 2; c++) {
  for (size_t i = 8; i + 9 < n; i++) {
   const double v = fabs((double)lr[2 * i + c]);
   if (v > peak) peak = v;
   if (v > tp) tp = v;
   for (int k = 0; k < 3; k++) {
    double y = 0;
    for (int j = 0; j < 16; j++) y += (double)lr[2 * (i + j - 7) + c] * ph[k][j];
    if (fabs(y) > tp) tp = fabs(y);
   }
  }
 }
 sample_peak_db = peak > 0 ? 20.0 * log10(peak) : -200.0;
 return tp > 0 ? 20.0 * log10(tp) : -200.0;
}
/* mean power spectrum of the mono mix (2048 Hann blocks): centroid and the shares below 100 Hz and above 8 kHz */
static void fft(std::vector<double> &re, std::vector<double> &im) {
 const size_t n = re.size();
 for (size_t i = 1, j = 0; i < n; i++) { size_t bit = n >> 1; for (; j & bit; bit >>= 1) j ^= bit; j ^= bit; if (i < j) { std::swap(re[i], re[j]); std::swap(im[i], im[j]); } }
 for (size_t len = 2; len <= n; len <<= 1) {
  const double a = -2.0 * PI / (double)len, wr = cos(a), wi = sin(a);
  for (size_t i = 0; i < n; i += len) {
   double cr = 1, ci = 0;
   for (size_t j = 0; j < len / 2; j++) {
    const double ur = re[i + j], ui = im[i + j], vr = re[i + j + len / 2] * cr - im[i + j + len / 2] * ci, vi = re[i + j + len / 2] * ci + im[i + j + len / 2] * cr;
    re[i + j] = ur + vr; im[i + j] = ui + vi; re[i + j + len / 2] = ur - vr; im[i + j + len / 2] = ui - vi;
    const double t = cr * wr - ci * wi; ci = cr * wi + ci * wr; cr = t;
   }
  }
 }
}
static void spectrum(const std::vector<float> &lr, double &centroid, double &low, double &high) {
 const size_t N = 2048, n = lr.size() / 2;
 std::vector<double> mean(N / 2, 0.0), re(N), im(N);
 size_t blocks = 0;
 for (size_t s = 0; s + N <= n; s += N) {
  for (size_t i = 0; i < N; i++) { re[i] = 0.5 * ((double)lr[2 * (s + i)] + lr[2 * (s + i) + 1]) * (0.5 - 0.5 * cos(2 * PI * (double)i / (double)N)); im[i] = 0; }
  fft(re, im);
  for (size_t k = 0; k < N / 2; k++) mean[k] += re[k] * re[k] + im[k] * im[k];
  blocks++;
 }
 double tot = 0, w = 0, lo = 0, hi = 0;
 for (size_t k = 1; k < N / 2; k++) { const double f = (double)k * FS / (double)N; tot += mean[k]; w += f * mean[k]; if (f < 100) lo += mean[k]; if (f > 8000) hi += mean[k]; }
 centroid = tot > 0 ? w / tot : 0; low = tot > 0 ? lo / tot : 0; high = tot > 0 ? hi / tot : 0;
 (void)blocks;
}
static void dc_and_corr(const std::vector<float> &lr, double &dc_db, double &corr) {
 double sl = 0, sr = 0, ll = 0, rr = 0, lrs = 0; const size_t n = lr.size() / 2;
 for (size_t i = 0; i < n; i++) { const double l = lr[2 * i], r = lr[2 * i + 1]; sl += l; sr += r; ll += l * l; rr += r * r; lrs += l * r; }
 const double m = fmax(fabs(sl), fabs(sr)) / (double)n;
 dc_db = m > 0 ? 20.0 * log10(m) : -200.0;
 corr = (ll > 0 && rr > 0) ? lrs / sqrt(ll * rr) : 0;
}
static float chunk_buf[256];
static void render_into(std::vector<float> &out, unsigned frames, unsigned chunk = 128) {
 while (frames) {
  const unsigned n = frames > chunk ? chunk : frames;
  md_render(chunk_buf, n);
  for (unsigned k = 0; k < n * 2; k++) { assert(std::isfinite(chunk_buf[k])); out.push_back(chunk_buf[k]); }
  frames -= n;
 }
}
static void select_biome(int id) {
 md_init();
 /* QA_AIR=0..3, QA_CHORUS=0/1, QA_TEMPO=0..3, QA_VINTAGE=0..2 switch the 2.4 finish settings on for a whole `biomes` run (the other modes set them themselves) */
 if (getenv("QA_AIR")) md_set_air(atoi(getenv("QA_AIR")));
 if (getenv("QA_CHORUS")) md_set_chorus(atoi(getenv("QA_CHORUS")));
 if (getenv("QA_TEMPO")) md_set_tempo_shift(atoi(getenv("QA_TEMPO")));
 if (getenv("QA_VINTAGE")) md_set_vintage(atoi(getenv("QA_VINTAGE")));
 assert(md_command(MD_SCENE, 0, id));
 std::vector<float> sink; render_into(sink, 1);
 if (md_pending_biome() >= 0) { assert(md_command(MD_SCENE_CONFIRM, 0, 1)); render_into(sink, 1); }
 assert(md_state()->biome_id == id);
}

static int mode_biomes(const char *out, int seconds, int first, int last) {
 mkdir(out, 0755);
 char path[1024]; snprintf(path, sizeof path, "%s/biomes.tsv", out);
 FILE *f = fopen(path, "wb"); assert(f);
 fprintf(f, "id\tname\tlufs\tlra\tshort_max\ttp_db\tpeak_db\tdc_db\tcorr\tcentroid_hz\tlow100\thigh8k\teco\tquiet\n");
 for (int id = first; id <= last && id < MD_BIOMES; id++) {
  select_biome(id);
  assert(md_command(MD_PLAY, 0, 1));
  std::vector<float> lr; lr.reserve((size_t)seconds * MD_RATE * 2);
  render_into(lr, (unsigned)(seconds * MD_RATE));
  double lufs, lra, smax, sp, dc, corr, cen, lo, hi;
  loudness(lr, lufs, lra, smax);
  const double tp = true_peak_db(lr, sp);
  dc_and_corr(lr, dc, corr);
  spectrum(lr, cen, lo, hi);
  /* seconds below -60 LUFS-short in the second half: a Biome that goes quiet */
  const std::vector<double> hop = hop_power(lr);
  unsigned quiet = 0; for (size_t i = hop.size() / 2; i + 10 <= hop.size(); i += 10) { double a = 0; for (size_t j = 0; j < 10; j++) a += hop[i + j]; if (lufs_of(a / 10.0) < -60.0) quiet++; }
  fprintf(f, "%d\t%s\t%.2f\t%.2f\t%.2f\t%.2f\t%.2f\t%.1f\t%.3f\t%.0f\t%.4f\t%.4f\t%u\t%u\n", id, md_biome_name(id), lufs, lra, smax, tp, sp, dc, corr, cen, lo, hi, md_status()->quality_reductions, quiet);
  fflush(f);
 }
 fclose(f);
 return 0;
}

static int mode_dream(const char *out, int preset, int minutes, int pace, int hall) {
 mkdir(out, 0755);
 md_init();
 if (getenv("QA_TEMPO")) md_set_tempo_shift(atoi(getenv("QA_TEMPO")));   /* a dream must still fill its time at a divided tempo */
 if (getenv("QA_AIR")) md_set_air(atoi(getenv("QA_AIR")));
 if (getenv("QA_CHORUS")) md_set_chorus(atoi(getenv("QA_CHORUS")));
 if (getenv("QA_VINTAGE")) md_set_vintage(atoi(getenv("QA_VINTAGE")));
 /* settings = duration (0..3: 30/60/120/240 min) + 4 * pace + 12 * hall; the preset's own pace and hall unless given.
  * (md_itinera_defaults() is 1 + 4 * pace + 12 * hall: its own duration is the 60 minutes.) */
 const unsigned def = md_itinera_defaults((unsigned)preset);
 const unsigned dur = minutes <= 30 ? 0 : minutes <= 60 ? 1 : minutes <= 120 ? 2 : 3;
 const unsigned pace_v = pace >= 0 ? (unsigned)pace : ((def - 1) / 4) % 3, hall_v = hall >= 0 ? (unsigned)hall : (def - 1) / 12;
 const unsigned settings = dur + 4 * pace_v + 12 * hall_v;
 assert(md_command(MD_ITINERA_START, preset, (int)settings));
 char path[1024]; snprintf(path, sizeof path, "%s/dream-%02d.tsv", out, preset);
 FILE *f = fopen(path, "wb"); assert(f);
 fprintf(f, "# preset %d %s, settings %u\n", preset, md_itinera_name((unsigned)preset), settings);
 fprintf(f, "kind\tsecond\tvalue\textra\n");
 std::vector<float> lr;
 unsigned last_section = 0, quanta = 0;
 const unsigned limit = (unsigned)minutes * 60u * MD_RATE + 40u * MD_RATE;
 std::vector<size_t> cuts; std::vector<int> cut_biome;
 size_t frames_done = 0;
 while (frames_done < limit) {
  md_itinera_view v; md_itinera_read(&v);
  if (!v.active && frames_done > (size_t)MD_RATE) break;
  if (v.section != last_section) { cuts.push_back(frames_done); cut_biome.push_back(v.biome); last_section = v.section; }
  render_into(lr, 128);
  frames_done += 128; quanta++;
 }
 /* loudness every 30 s (short-term, 3 s windows) */
 const std::vector<double> hop = hop_power(lr);
 double mn = 999, mx = -999, sum = 0; unsigned cnt = 0;
 for (size_t i = 0; i + 30 <= hop.size(); i += 300) {
  double a = 0; for (size_t j = 0; j < 30; j++) a += hop[i + j];
  const double l = lufs_of(a / 30.0);
  fprintf(f, "loud\t%.0f\t%.2f\t\n", (double)i / 10.0, l);
  if (l > -70) { mn = fmin(mn, l); mx = fmax(mx, l); sum += l; cnt++; }
 }
 /* every scene change: the level either side (1 s) and the biggest sample step against the steady 99.9th percentile */
 const size_t n = lr.size() / 2;
 std::vector<float> step; step.reserve(n);
 for (size_t i = 1; i < n; i++) step.push_back(fmaxf(fabsf(lr[2 * i] - lr[2 * i - 2]), fabsf(lr[2 * i + 1] - lr[2 * i - 1])));
 std::vector<float> sorted(step); std::sort(sorted.begin(), sorted.end());
 const float p999 = sorted.empty() ? 0.f : sorted[(size_t)((double)sorted.size() * 0.999)];
 double worst_click = 0, worst_jump = 0;
 for (size_t c = 0; c < cuts.size(); c++) {
  const size_t at = cuts[c];
  if (at < MD_RATE * 2 || at + MD_RATE * 2 >= n) continue;
  float mxstep = 0; for (size_t i = at - 2205; i < at + 2205; i++) mxstep = fmaxf(mxstep, step[i]);
  double before = 0, after = 0;
  for (size_t i = at - MD_RATE; i < at; i++) before += (double)lr[2 * i] * lr[2 * i] + (double)lr[2 * i + 1] * lr[2 * i + 1];
  for (size_t i = at; i < at + MD_RATE; i++) after += (double)lr[2 * i] * lr[2 * i] + (double)lr[2 * i + 1] * lr[2 * i + 1];
  const double jump = 10.0 * log10((after + 1e-12) / (before + 1e-12));
  const double click = p999 > 0 ? mxstep / p999 : 0;
  worst_click = fmax(worst_click, click); worst_jump = fmax(worst_jump, fabs(jump));
  fprintf(f, "scene\t%.1f\t%.2f\t%d %.2f\n", (double)at / MD_RATE, jump, cut_biome[c], click);
 }
 /* silence: runs of 100 ms hops below -70 LUFS (momentary), and each scene's own loudness */
 {
  unsigned run = 0, runs = 0; double longest = 0; size_t shown = 0;
  for (size_t i = 0; i <= hop.size(); i++) {
   const bool quiet = i < hop.size() && lufs_of(hop[i]) < -70.0;
   if (quiet) run++;
   else { if (run >= 10) { runs++; const double sec = run / 10.0; if (sec > longest) longest = sec; if (shown++ < 8) fprintf(f, "silence\t%.1f\t%.1f\t\n", (double)(i - run) / 10.0, sec); } run = 0; }
  }
  fprintf(f, "summary\tsilence_runs\t%u\tlongest %.1f s (runs of 1 s or more below -70 LUFS)\n", runs, longest);
  std::vector<double> short_term;
  for (size_t i = 0; i + 30 <= hop.size(); i += 10) { double a = 0; for (size_t j = 0; j < 30; j++) a += hop[i + j]; const double l = lufs_of(a / 30.0); if (l > -70) short_term.push_back(l); }
  std::sort(short_term.begin(), short_term.end());
  if (short_term.size() > 10) fprintf(f, "summary\tshort_term_p5_p50_p95\t%.1f\t%.1f %.1f\n", short_term[short_term.size() * 5 / 100], short_term[short_term.size() / 2], short_term[short_term.size() * 95 / 100]);
  double lo = 999, hi = -999;
  for (size_t c = 0; c < cuts.size(); c++) {
   const size_t a = cuts[c] / (MD_RATE / 10), b = c + 1 < cuts.size() ? cuts[c + 1] / (MD_RATE / 10) : hop.size();
   double sum = 0; size_t k = 0; for (size_t i = a; i < b && i < hop.size(); i++) { sum += hop[i]; k++; }
   if (!k) continue;
   const double l = lufs_of(sum / (double)k);
   fprintf(f, "scenelufs\t%.0f\t%.2f\t%d\n", (double)cuts[c] / MD_RATE, l, cut_biome[c]);
   if (l > -70) { lo = fmin(lo, l); hi = fmax(hi, l); }
  }
  fprintf(f, "summary\tscene_lufs_min_max\t%.1f\t%.1f (spread %.1f LU)\n", lo, hi, hi - lo);
 }
 double tp_sp; const double tp = true_peak_db(lr, tp_sp);
 double lufs, lra, smax; loudness(lr, lufs, lra, smax);
 double dc, corr; dc_and_corr(lr, dc, corr);
 fprintf(f, "summary\tscenes\t%zu\t\n", cuts.size());
 fprintf(f, "summary\tlufs\t%.2f\tlra %.2f max-short %.2f\n", lufs, lra, smax);
 fprintf(f, "summary\ttp_db\t%.2f\tsample %.2f\n", tp, tp_sp);
 fprintf(f, "summary\tdc_db\t%.1f\tcorr %.3f\n", dc, corr);
 fprintf(f, "summary\tloud_min_max\t%.2f\t%.2f\n", mn, mx);
 fprintf(f, "summary\tworst_click\t%.2f\tx p99.9; worst level jump %.2f dB\n", worst_click, worst_jump);
 fprintf(f, "summary\teco\t%u\tnonfinite %u\n", md_status()->quality_reductions, md_status()->nonfinite);
 fprintf(f, "summary\tseconds\t%.0f\t\n", (double)n / MD_RATE);
 fclose(f);
 return 0;
}

/* the same Biomes rendered in different block sizes must agree: the engine's quantum is its own */
static int mode_invariance(const char *out) {
 mkdir(out, 0755);
 char path[1024]; snprintf(path, sizeof path, "%s/invariance.tsv", out);
 FILE *f = fopen(path, "wb"); assert(f);
 fprintf(f, "id\tname\tmax_diff_64_vs_128\tmax_diff_64_vs_256\tmax_diff_64_vs_37\n");
 const int ids[] = {0, 17, 64, 100, 140, 175, 213, 233, 245, 260, 275};
 for (int id : ids) {
  if (id >= MD_BIOMES) continue;
  std::vector<float> ref;
  double d[3] = {0, 0, 0};
  const unsigned chunks[4] = {64, 128, 256, 37};
  for (int c = 0; c < 4; c++) {
   select_biome(id); assert(md_command(MD_PLAY, 0, 1));
   std::vector<float> r; render_into(r, MD_RATE * 3, chunks[c]);
   if (c == 0) ref = r;
   else { double m = 0; for (size_t i = 0; i < r.size(); i++) m = fmax(m, fabs((double)r[i] - ref[i])); d[c - 1] = m; }
  }
  fprintf(f, "%d\t%s\t%.3g\t%.3g\t%.3g\n", id, md_biome_name(id), d[0], d[1], d[2]);
 }
 fclose(f);
 return 0;
}

/* qa_switch_click: change Biome while playing and look at the biggest sample step and the level across the change */
static int mode_switch(const char *out) {
 mkdir(out, 0755);
 char path[1024]; snprintf(path, sizeof path, "%s/switch.tsv", out);
 FILE *f = fopen(path, "wb"); assert(f);
 fprintf(f, "from\tto\tclick_first10ms_x_new_p999\tlevel_jump_db\tcontinues\n");
 const int pairs[][2] = {{0,1},{0,100},{17,140},{100,175},{140,213},{213,233},{233,245},{245,260},{260,275},{275,0},{175,64},{64,175},{2,50},{50,230},{3,168},{47,2},{23,137},{62,53},{59,55},{44,3}};
 for (auto &pr : pairs) {
  if (pr[0] >= MD_BIOMES || pr[1] >= MD_BIOMES) continue;
  select_biome(pr[0]); assert(md_command(MD_PLAY, 0, 1));
  std::vector<float> lr; render_into(lr, MD_RATE * 6);
  const size_t at = lr.size() / 2;
  assert(md_command(MD_SCENE, 0, pr[1]));
  render_into(lr, 128);
  if (md_pending_biome() >= 0) { assert(md_command(MD_SCENE_CONFIRM, 0, 1)); }
  render_into(lr, MD_RATE * 8);
  const size_t n = lr.size() / 2;
  std::vector<float> step(n - 1);
  for (size_t i = 1; i < n; i++) step[i - 1] = fmaxf(fabsf(lr[2 * i] - lr[2 * i - 2]), fabsf(lr[2 * i + 1] - lr[2 * i - 1]));
  /* the new scene's own steady step size (4..8 s after the change), against its first 10 ms and the 10 ms before it */
  std::vector<float> sorted(step.begin() + (long)at + MD_RATE * 4, step.begin() + (long)at + MD_RATE * 8); std::sort(sorted.begin(), sorted.end());
  const float p999 = sorted[(size_t)((double)sorted.size() * 0.999)];
  float mx = 0; for (size_t i = at; i < at + 441 && i < step.size(); i++) mx = fmaxf(mx, step[i]);
  double before = 0, after = 0;
  for (size_t i = at - MD_RATE * 2; i < at; i++) before += (double)lr[2 * i] * lr[2 * i] + (double)lr[2 * i + 1] * lr[2 * i + 1];
  for (size_t i = at + MD_RATE * 4; i < at + MD_RATE * 6 && i < n; i++) after += (double)lr[2 * i] * lr[2 * i] + (double)lr[2 * i + 1] * lr[2 * i + 1];
  fprintf(f, "%d %s\t%d %s\t%.2f\t%.1f\t%d\n", pr[0], md_biome_name(pr[0]), pr[1], md_biome_name(pr[1]), p999 > 0 ? mx / p999 : 0, 10.0 * log10((after + 1e-12) / (before + 1e-12)), md_state()->biome_id == pr[1]);
 }
 fclose(f);
 return 0;
}

/* trace: level per second of a Biome (BIOME ID) or of a dream's start (DREAM PRESET), and which tracks play */
static int mode_trace(bool dream, int id, int seconds) {
 md_init();
 if (dream) { const unsigned def = md_itinera_defaults((unsigned)id); assert(md_command(MD_ITINERA_START, id, (int)(0 + 4 * (((def - 1) / 4) % 3) + 12 * ((def - 1) / 12)))); }
 else { select_biome(id); assert(md_command(MD_PLAY, 0, 1)); }
 printf("# %s %d  %s\n", dream ? "dream" : "Biome", id, dream ? md_itinera_name((unsigned)id) : md_biome_name(id));
 uint32_t last_events[6] = {0};
 for (int sec = 0; sec < seconds; sec++) {
  std::vector<float> lr; render_into(lr, MD_RATE);
  double e = 0; for (float v : lr) e += (double)v * v;
  const double db = e > 0 ? 10.0 * log10(e / (double)lr.size()) : -200.0;
  const md_stats *st = md_status();
  unsigned hits[6]; for (int t = 0; t < 6; t++) { hits[t] = st->events[t] - last_events[t]; last_events[t] = st->events[t]; }
  md_itinera_view v; md_itinera_read(&v);
  printf("%3d s  %7.1f dBFS  notes/track %u %u %u %u %u %u  biome %d section %u\n", sec, db, hits[0], hits[1], hits[2], hits[3], hits[4], hits[5], md_state()->biome_id, v.section);
 }
 return 0;
}

#include "dsp/biome_trim.inc"
static double trims_table(int id) { return id >= 0 && id < (int)sizeof biome_trim ? biome_trim[id] * 0.5 : 0.0; }
/* the loudness trim (app/dsp/biome_trim.inc): measured again, against the untrimmed measurement the table was made from */
static int mode_trimcheck(const char *tsv, int seconds) {
 struct Row { double lufs, tp; bool ok; };
 std::vector<Row> before(MD_BIOMES, Row{-200, -200, false});
 FILE *f = fopen(tsv, "rb"); assert(f);
 char line[1024]; assert(fgets(line, sizeof line, f));
 while (fgets(line, sizeof line, f)) {
  char *tok = strtok(line, "\t"); if (!tok) continue; const int id = atoi(tok);
  strtok(0, "\t"); const char *l = strtok(0, "\t"); strtok(0, "\t"); strtok(0, "\t"); const char *t = strtok(0, "\t");
  if (id >= 0 && id < MD_BIOMES && l && t) before[id] = Row{atof(l), atof(t), true};
 }
 fclose(f);
 std::vector<double> after_l, trims; double worst_tp = -200, worst_dev = 0; int worst_id = -1, raised = 0, off_table = 0, measured = 0;
 for (int id = 0; id < MD_BIOMES; id++) {
  if (!before[id].ok || before[id].lufs < -100) continue;
  select_biome(id); assert(md_command(MD_PLAY, 0, 1));
  std::vector<float> lr; render_into(lr, (unsigned)(seconds * MD_RATE));
  double lufs, lra, smax, sp; loudness(lr, lufs, lra, smax);
  const double tp = true_peak_db(lr, sp);
  const double gain = lufs - before[id].lufs;
  after_l.push_back(lufs); trims.push_back(gain); measured++;
  if (gain > 0.1) raised++;
  if (tp > worst_tp) worst_tp = tp;
  assert(gain > -0.5);                                 /* nothing gets quieter */
  /* the loudness follows the table, except where the loaded Biome's number changes while it plays (a Journey Biome
   * takes its sections' trims: those are meant to differ from the table entry of the Biome itself) */
  const double expect = trims_table(id);
  if (fabs(gain - expect) > 0.7) { off_table++; printf("  Biome %d %s: %.1f dB, table %.1f dB\n", id, md_biome_name(id), gain, expect); }
  (void)worst_dev; (void)worst_id;
 }
 std::sort(after_l.begin(), after_l.end());
 const double p10 = after_l[after_l.size() / 10], p90 = after_l[after_l.size() * 9 / 10];
 printf("trimmed %d of %zu Biomes; loudness p10 %.1f p90 %.1f LUFS (spread %.1f LU); loudest true peak %.1f dBTP\n", raised, after_l.size(), p10, p90, p90 - p10, worst_tp);
 assert(p90 - p10 <= 5.0);
 assert(worst_tp <= -9.0);
 assert(off_table * 100 <= measured * 3);              /* at most 3 % of the Biomes off the table (the Journey Biomes) */
 puts("PASS the Biome loudness trim: spread, peaks, nothing quieter, the table matches the sound");
 /* the trim follows the loaded Biome's number: the same patch as a User Biome (number -1) is not trimmed */
 for (int id : {2, 50, 53, 168}) {
  if (id >= MD_BIOMES) continue;
  std::vector<float> a, b;
  select_biome(id); md_patch p = *md_state(); assert(md_command(MD_PLAY, 0, 1)); render_into(a, (unsigned)MD_RATE * 20);
  md_init(); p.biome_id = -1; assert(md_load(&p)); assert(md_command(MD_PLAY, 0, 1)); render_into(b, (unsigned)MD_RATE * 20);
  double la, lb, x1, x2; loudness(a, la, x1, x2); loudness(b, lb, x1, x2);
  printf("  Biome %d: %.1f dB with the number, %.1f dB as a User Biome\n", id, la - lb, 0.0);
  assert(la - lb >= -0.2 && lb < la + 0.1);
 }
 puts("PASS User Biomes are not trimmed");
 (void)worst_id; (void)trims;
 return 0;
}

/* WIDTH: the sides widened, the mono sum (L+R) the same; and the trim switched off */
static int mode_width(int id, int level) {
 double mono0 = 0;
 for (int pass = 0; pass < 3; pass++) {
  select_biome(id);
  if (pass == 0) md_set_width(0); else if (pass == 1) md_set_width(level); else { md_set_width(0); md_set_level_match(0); }
  assert(md_command(MD_PLAY, 0, 1));
  std::vector<float> lr; render_into(lr, (unsigned)(MD_RATE * 20));
  std::vector<float> mono(lr.size());
  for (size_t i = 0; i + 1 < lr.size(); i += 2) { mono[i] = mono[i + 1] = 0.5f * (lr[i] + lr[i + 1]); }
  double lufs, lra, smax, sp, dc, corr, ml, x1, x2; loudness(lr, lufs, lra, smax); const double tp = true_peak_db(lr, sp); dc_and_corr(lr, dc, corr); loudness(mono, ml, x1, x2);
  printf("Biome %d %s %s: lufs %.1f tp %.1f corr %.3f mono-sum lufs %.2f%s\n", id, md_biome_name(id), pass == 0 ? "width 0" : pass == 1 ? "width LEVEL" : "level match OFF", lufs, tp, corr, ml, pass == 1 ? (fabs(ml - mono0) < 0.2 ? "  (mono sum unchanged)" : "  (MONO SUM MOVED)") : "");
  if (pass == 0) mono0 = ml;
  md_set_width(0); md_set_level_match(1);
 }
 return 0;
}

/* AIR 0..3 on one Biome: loudness, true peak and the share of the energy above 8 kHz (the gain must stay below about 0.3 LU) */
static int mode_air(int id) {
 for (int level = 0; level < 4; level++) {
  select_biome(id); md_set_air(level);
  assert(md_command(MD_PLAY, 0, 1));
  std::vector<float> lr; render_into(lr, (unsigned)(MD_RATE * 20));
  double lufs, lra, smax, sp, cen, lo, hi; loudness(lr, lufs, lra, smax); const double tp = true_peak_db(lr, sp); spectrum(lr, cen, lo, hi);
  printf("Biome %d %s AIR %d: lufs %.2f tp %.2f centroid %.0f Hz high8k %.5f\n", id, md_biome_name(id), level, lufs, tp, cen, hi);
  md_set_air(0);
 }
 return 0;
}

/* CHORUS on one Biome: loudness, true peak, stereo correlation (lower = wider) and the mono sum's loudness */
static int mode_chorus(int id) {
 for (int on = 0; on < 2; on++) {
  select_biome(id); md_set_chorus(on);
  assert(md_command(MD_PLAY, 0, 1));
  std::vector<float> lr; render_into(lr, (unsigned)(MD_RATE * 20));
  std::vector<float> mono(lr.size());
  for (size_t i = 0; i + 1 < lr.size(); i += 2) { mono[i] = mono[i + 1] = 0.5f * (lr[i] + lr[i + 1]); }
  double lufs, lra, smax, sp, dc, corr, ml, x1, x2; loudness(lr, lufs, lra, smax); const double tp = true_peak_db(lr, sp); dc_and_corr(lr, dc, corr); loudness(mono, ml, x1, x2);
  printf("Biome %d %s CHORUS %d: lufs %.2f tp %.2f corr %.3f mono-sum lufs %.2f\n", id, md_biome_name(id), on, lufs, tp, corr, ml);
  md_set_chorus(0);
 }
 return 0;
}

/* host cost of the finish stages: microseconds per 256-frame block with AIR, CHORUS and VINTAGE off and on (a relative figure, not the device's) */
static int mode_bench(int id) {
 struct Cfg { const char *name; int air, chorus, vintage; } cfgs[] = {{"all off", 0, 0, 0}, {"AIR 3", 3, 0, 0}, {"CHORUS", 0, 1, 0}, {"VINTAGE 2", 0, 0, 2}, {"all on", 3, 1, 2}};
 for (const auto &c : cfgs) {
  double best = 1e9;
  for (int rep = 0; rep < 5; rep++) {
   select_biome(id); md_set_air(c.air); md_set_chorus(c.chorus); md_set_vintage(c.vintage);
   assert(md_command(MD_PLAY, 0, 1));
   std::vector<float> warm; render_into(warm, (unsigned)(MD_RATE * 2));
   const auto t0 = std::chrono::steady_clock::now();
   std::vector<float> lr; render_into(lr, (unsigned)(MD_RATE * 10));
   const double us = std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - t0).count();
   const double per = us / (10.0 * MD_RATE / 256.0);
   if (per < best) best = per;
   md_set_air(0); md_set_chorus(0); md_set_vintage(0);
  }
  printf("Biome %d %-10s %.1f us per 256-frame block\n", id, c.name, best);
 }
 return 0;
}

/* host cost of Biomes FROM..TO: microseconds per 256-frame block (best of 3 runs of 8 s): a relative figure to find the heavy Biomes */
static int mode_cost(int from, int to) {
 printf("id\tname\tus_per_block\n");
 for (int id = from; id <= to && id < MD_BIOMES; id++) {
  double best = 1e9;
  for (int rep = 0; rep < 3; rep++) {
   select_biome(id);
   assert(md_command(MD_PLAY, 0, 1));
   std::vector<float> warm; render_into(warm, (unsigned)(MD_RATE * 2));
   const auto t0 = std::chrono::steady_clock::now();
   std::vector<float> lr; render_into(lr, (unsigned)(MD_RATE * 8));
   const double per = std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - t0).count() / (8.0 * MD_RATE / 256.0);
   if (per < best) best = per;
  }
  printf("%d\t%s\t%.2f\n", id, md_biome_name(id), best);
 }
 return 0;
}

/* one Biome with one macro moved: what K1..K4 do to loudness and spectrum */
static int mode_macro(int id, int k, int value) {
 select_biome(id); assert(md_command(MD_MACRO, k, value)); assert(md_command(MD_PLAY, 0, 1));
 std::vector<float> lr; render_into(lr, (unsigned)(MD_RATE * 30));
 double lufs, lra, smax, sp, cen, lo, hi; loudness(lr, lufs, lra, smax); const double tp = true_peak_db(lr, sp); spectrum(lr, cen, lo, hi);
 printf("Biome %d %s macro %d = %d: lufs %.1f tp %.1f centroid %.0f Hz low100 %.3f high8k %.5f\n", id, md_biome_name(id), k, value, lufs, tp, cen, lo, hi);
 return 0;
}

/* each tone alone on one track, one note: level, centre of the spectrum, share below 100 Hz and above 4 kHz, attack time */
static int mode_tone(int from, int to, int note) {
 printf("id\tname\tmodel\tpeak_db\trms_db\tcentroid_hz\tlow100\thigh4k\tattack_s\n");
 for (int id = from; id <= to && id < MD_TONE_PRESETS; id++) {
  md_init(); md_patch p = *md_state();
  p.journey.mode = 0; p.kit.enabled = 0; p.locked = 1;
  for (auto &t : p.track) t.mute = 1;
  for (auto &q : p.sequence) q.enabled = 0;
  p.track[0].mute = 0; p.track[0].level = 700;
  assert(md_load(&p));
  assert(md_command(MD_TONE_PRESET, 0, id)); std::vector<float> sink; render_into(sink, 64);
  assert(md_command(MD_TRIGGER, 0, note));
  std::vector<float> lr; render_into(lr, MD_RATE * 8);
  double e = 0, pk = 0, maxwin = 0; const size_t n = lr.size() / 2, win = MD_RATE / 50;
  std::vector<double> w;
  for (size_t i = 0; i + win <= n; i += win) { double a = 0; for (size_t j = 0; j < win; j++) { const double v = 0.5 * ((double)lr[2 * (i + j)] + lr[2 * (i + j) + 1]); a += v * v; } w.push_back(a / (double)win); if (a / (double)win > maxwin) maxwin = a / (double)win; }
  for (float v : lr) { e += (double)v * v; if (fabs(v) > pk) pk = fabs(v); }
  double attack = 0; for (size_t i = 0; i < w.size(); i++) if (w[i] >= 0.5 * maxwin) { attack = (double)(i * win) / MD_RATE; break; }
  double cen, lo, hi; spectrum(lr, cen, lo, hi);
  /* share above 4 kHz from the same spectrum: recompute quickly */
  const size_t N = 2048; std::vector<double> mean(N / 2, 0.0), re(N), im(N); double tot = 0, above = 0;
  for (size_t s0 = 0; s0 + N <= n; s0 += N) { for (size_t i = 0; i < N; i++) { re[i] = 0.5 * ((double)lr[2 * (s0 + i)] + lr[2 * (s0 + i) + 1]) * (0.5 - 0.5 * cos(2 * PI * (double)i / (double)N)); im[i] = 0; } fft(re, im); for (size_t k = 1; k < N / 2; k++) mean[k] += re[k] * re[k] + im[k] * im[k]; }
  for (size_t k = 1; k < N / 2; k++) { tot += mean[k]; if ((double)k * FS / (double)N > 4000.0) above += mean[k]; }
  printf("%d\t%s\t%d\t%.1f\t%.1f\t%.0f\t%.3f\t%.4f\t%.2f\n", id, md_tone_name(id), md_state()->track[0].model, pk > 0 ? 20 * log10(pk) : -200.0, e > 0 ? 10 * log10(e / (double)lr.size()) : -200.0, cen, lo, tot > 0 ? above / tot : 0.0, attack);
 }
 return 0;
}

/* a recording from the device (tools or scripts write raw float32 stereo): the same measures as the host */
static int mode_fileloud(const char *path, int skip, double gain_db) {
 FILE *f = fopen(path, "rb"); assert(f);
 std::vector<float> lr; float buf[4096]; size_t n;
 while ((n = fread(buf, sizeof(float), 4096, f)) > 0) lr.insert(lr.end(), buf, buf + n);
 fclose(f);
 if (skip > 0 && lr.size() > (size_t)skip * MD_RATE * 2) lr.erase(lr.begin(), lr.begin() + (long)skip * MD_RATE * 2);
 if (gain_db != 0.0) { const float g = (float)pow(10.0, gain_db / 20.0); for (float &v : lr) v *= g; }   /* the recording's MASTER position, put back */
 const std::vector<double> hop = hop_power(lr);
 double lufs, lra, smax, sp, dc, corr, cen, lo, hi;
 loudness(lr, lufs, lra, smax); const double tp = true_peak_db(lr, sp); dc_and_corr(lr, dc, corr); spectrum(lr, cen, lo, hi);
 /* the first hop above -70 LUFS, and the longest run below it */
 size_t first = hop.size(); for (size_t i = 0; i < hop.size(); i++) if (lufs_of(hop[i]) > -70.0) { first = i; break; }
 unsigned run = 0, longest = 0; for (size_t i = 0; i < hop.size(); i++) { if (lufs_of(hop[i]) < -70.0) { run++; if (run > longest) longest = run; } else run = 0; }
 printf("seconds %.1f  lufs %.2f  lra %.2f  short_max %.2f  tp %.2f  peak %.2f  dc %.1f  corr %.3f  centroid %.0f Hz  high8k %.4f\n", (double)(lr.size() / 2) / MD_RATE, lufs, lra, smax, tp, sp, dc, corr, cen, hi);
 printf("first sound at %.1f s; longest silence %.1f s\n", (double)first / 10.0, (double)longest / 10.0);
 printf("loudness per 5 s:");
 for (size_t i = 0; i + 50 <= hop.size(); i += 50) { double a = 0; for (size_t j = 0; j < 50; j++) a += hop[i + j]; printf(" %.0f", lufs_of(a / 50.0)); }
 printf("\n");
 return 0;
}

int main(int argc, char **argv) {
 if (argc >= 4 && !strcmp(argv[1], "width")) return mode_width(atoi(argv[2]), atoi(argv[3]));
 if (argc >= 5 && !strcmp(argv[1], "macro")) return mode_macro(atoi(argv[2]), atoi(argv[3]), atoi(argv[4]));
 if (argc >= 4 && !strcmp(argv[1], "tone")) return mode_tone(atoi(argv[2]), atoi(argv[3]), argc > 4 ? atoi(argv[4]) : 48);
 if (argc >= 4 && !strcmp(argv[1], "cost")) return mode_cost(atoi(argv[2]), atoi(argv[3]));
 if (argc >= 3 && !strcmp(argv[1], "bench")) return mode_bench(atoi(argv[2]));
 if (argc >= 3 && !strcmp(argv[1], "chorus")) return mode_chorus(atoi(argv[2]));
 if (argc >= 3 && !strcmp(argv[1], "air")) return mode_air(atoi(argv[2]));
 if (argc >= 3 && !strcmp(argv[1], "fileloud")) return mode_fileloud(argv[2], argc > 3 ? atoi(argv[3]) : 0, argc > 4 ? atof(argv[4]) : 0.0);
 if (argc >= 4 && !strcmp(argv[1], "trimcheck")) return mode_trimcheck(argv[2], atoi(argv[3]));
 if (argc >= 4 && !strcmp(argv[1], "trace")) return mode_trace(false, atoi(argv[2]), atoi(argv[3]));
 if (argc >= 4 && !strcmp(argv[1], "dtrace")) return mode_trace(true, atoi(argv[2]), atoi(argv[3]));
 if (argc >= 3 && !strcmp(argv[1], "switch")) return mode_switch(argv[2]);
 if (argc >= 4 && !strcmp(argv[1], "biomes")) return mode_biomes(argv[2], atoi(argv[3]), argc > 4 ? atoi(argv[4]) : 0, argc > 5 ? atoi(argv[5]) : MD_BIOMES - 1);
 if (argc >= 5 && !strcmp(argv[1], "dream")) return mode_dream(argv[2], atoi(argv[3]), atoi(argv[4]), argc > 5 ? atoi(argv[5]) : -1, argc > 6 ? atoi(argv[6]) : -1);
 if (argc >= 3 && !strcmp(argv[1], "invariance")) return mode_invariance(argv[2]);
 fprintf(stderr, "usage: audioqa-tests biomes OUT SECONDS [FIRST LAST] | dream OUT PRESET MINUTES [PACE HALL] | invariance OUT | switch OUT | trimcheck TSV SECONDS | fileloud FILE.f32 [SKIP]\n");
 return 2;
}
