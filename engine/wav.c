// Omanoise v5 — minimal RIFF/WAVE reader and BS.1770 loudness meter.
//
// The reader is deliberately strict: 48 kHz, 16-bit PCM, 1 or 2 channels, which
// is what `sounds/*.wav` are and what the README tells a user to drop in. Files
// are read straight into their final buffer (no intermediate copy), because the
// four shipped recordings are ~60 MB together and the whole process has a
// 120 MB budget.
#include "wav.h"
#include "dsp.h"

#include <stdio.h>

// ------------------------------------------------------------ little-endian

static inline uint16_t rd16(const uint8_t *p) { return (uint16_t)(p[0] | (p[1] << 8)); }
static inline uint32_t rd32(const uint8_t *p) {
  return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

// ------------------------------------------------------------------ loader

int wav_load(const char *path, wav_t *w) {
  memset(w, 0, sizeof(*w));
  FILE *f = fopen(path, "rb");
  if (!f) return -1;

  uint8_t hdr[12];
  if (fread(hdr, 1, 12, f) != 12 || memcmp(hdr, "RIFF", 4) || memcmp(hdr + 8, "WAVE", 4)) {
    fprintf(stderr, "omanoise: %s is not a RIFF/WAVE file — slot disabled\n", path);
    fclose(f);
    return -2;
  }

  int have_fmt = 0, ch = 0, bits = 0;
  uint32_t rate = 0;
  for (;;) {
    uint8_t ck[8];
    if (fread(ck, 1, 8, f) != 8) break;
    uint32_t size = rd32(ck + 4);
    if (!memcmp(ck, "fmt ", 4)) {
      uint8_t fmt[40];
      uint32_t want = size > sizeof fmt ? (uint32_t)sizeof fmt : size;
      if (fread(fmt, 1, want, f) != want) break;
      if (size > want && fseek(f, (long)(size - want), SEEK_CUR)) break;
      uint16_t tag = rd16(fmt);
      // 0xFFFE is WAVE_FORMAT_EXTENSIBLE; its sub-format GUID starts with the
      // same tag, so the first two bytes of `SubFormat` decide.
      if (tag == 0xFFFE && want >= 26) tag = rd16(fmt + 24);
      ch = rd16(fmt + 2);
      rate = rd32(fmt + 4);
      bits = rd16(fmt + 14);
      if (tag != 1) {
        fprintf(stderr, "omanoise: %s is not PCM (format tag %u) — slot disabled\n", path, tag);
        fclose(f); return -2;
      }
      have_fmt = 1;
    } else if (!memcmp(ck, "data", 4)) {
      if (!have_fmt) break;
      if (rate != (uint32_t)RATE || bits != 16 || (ch != 1 && ch != 2)) {
        fprintf(stderr, "omanoise: %s is %u Hz %d-bit %d-channel; need 48000 Hz 16-bit "
                        "mono or stereo — slot disabled\n", path, rate, bits, ch);
        fclose(f); return -2;
      }
      int frames = (int)(size / (uint32_t)(ch * 2));
      if (frames < RATE) {          // under a second is not a usable segment source
        fprintf(stderr, "omanoise: %s is only %.2f s — slot disabled\n",
                path, (double)frames / RATE);
        fclose(f); return -2;
      }
      int16_t *d = malloc((size_t)frames * (size_t)ch * sizeof(int16_t));
      if (!d) { fclose(f); return -2; }
      size_t got = fread(d, sizeof(int16_t), (size_t)frames * (size_t)ch, f);
      fclose(f);
      frames = (int)(got / (size_t)ch);
      if (frames < RATE) { free(d); return -2; }
      w->d = d;
      w->frames = frames;
      w->ch = ch;
      w->seconds = (float)frames / (float)RATE;
      return 0;
    } else {
      if (fseek(f, (long)(size + (size & 1u)), SEEK_CUR)) break;
      continue;
    }
    if (size & 1u) fseek(f, 1, SEEK_CUR);     // chunks are word-aligned
  }
  fprintf(stderr, "omanoise: %s has no usable data chunk — slot disabled\n", path);
  fclose(f);
  return -2;
}

void wav_free(wav_t *w) {
  free(w->d);
  w->d = NULL;
  w->frames = 0;
}

// ------------------------------------------------------------ BS.1770-4
//
// Stage 1 is the head/shoulder shelf, stage 2 the RLB high-pass; both are the
// standard's 48 kHz coefficients. Mean squares are accumulated in 100 ms
// sub-blocks, so a 400 ms gating block at 75 % overlap is the sum of four
// consecutive sub-blocks and the whole file is filtered exactly once.

#define K_SUB   (RATE / 10)            // 100 ms
#define K_BLOCK 4                      // 400 ms

static void k_stage1(bq_t *f) {
  bq_set(f, 1.53512485958697f, -2.69169618940638f, 1.19839281085285f,
            -1.69065929318241f, 0.73248077421585f);
}
static void k_stage2(bq_t *f) {
  bq_set(f, 1.0f, -2.0f, 1.0f, -1.99004745483398f, 0.99007225036621f);
}

void wav_measure(wav_t *w) {
  w->lufs = -70.0f;
  w->servo_lufs = -70.0f;
  if (!w->d || w->frames < K_SUB * K_BLOCK) return;
  int nsub = w->frames / K_SUB;
  double *ms = calloc((size_t)nsub * 2, sizeof(double));
  if (!ms) return;

  bq_t s1[2], s2[2], t1[2], t2[2];
  for (int c = 0; c < 2; c++) {
    k_stage1(&s1[c]); k_stage2(&s2[c]); bq_reset(&s1[c]); bq_reset(&s2[c]);
    k_stage1(&t1[c]); k_stage2(&t2[c]); bq_reset(&t1[c]); bq_reset(&t2[c]);
  }

  // Pass A fills the gating blocks and warms the short-term meter; pass B reads
  // the short-term meter with every filter already settled (it is the same as
  // playing the file twice and measuring the second time round).
  float st = 0.0f;
  const float st_k = tau_k(3.0f, (float)RATE);      // master.c's ms_k
  double sdb = 0.0;
  long sn = 0;
  // The short-term measurement is taken through the slot's own filters (env.c:
  // rec_control), so a bright recording is not credited with the energy the
  // 9 kHz low-pass is about to remove.
  svf_t hp[2], lp[2];
  for (int c = 0; c < 2; c++) {
    svf_reset(&hp[c]); svf_reset(&lp[c]);
    svf_set(&hp[c], 40.0f, 0.7f);
    svf_set(&lp[c], 9000.0f, 0.7f);
  }
  // …and through the master's 1.3x stereo width, which credits a decorrelated
  // field recording with the side energy it is about to gain (master.c).
  op_t side_hp;
  side_hp.z = 0.0f;
  op_set(&side_hp, 200.0f);
  for (int pass = 0; pass < 2; pass++) {
    for (int b = 0; b < nsub; b++) {
      double acc[2] = { 0.0, 0.0 };
      for (int i = 0; i < K_SUB; i++) {
        const int16_t *fr = w->d + (size_t)(b * K_SUB + i) * (size_t)w->ch;
        float sq = 0.0f, fl[2];
        for (int c = 0; c < 2; c++) {
          float x = (float)fr[w->ch == 1 ? 0 : c] * (1.0f / 32768.0f);
          float y = bq_run(&s2[c], bq_run(&s1[c], x));
          acc[c] += (double)y * y;
          fl[c] = svf_lp(&lp[c], svf_hp(&hp[c], x));
        }
        {
          float mid = 0.5f * (fl[0] + fl[1]);
          float side = op_hp(&side_hp, 0.5f * (fl[0] - fl[1])) * 1.3f;
          float zl = bq_run(&t2[0], bq_run(&t1[0], mid + side));
          float zr = bq_run(&t2[1], bq_run(&t1[1], mid - side));
          sq = zl * zl + zr * zr;
        }
        st += st_k * (sq - st);
        if (pass == 1 && ((b * K_SUB + i) % BLOCK) == 0 && st > 1e-12f) {
          sdb += -0.691 + 10.0 * log10((double)st);
          sn++;
        }
      }
      if (pass == 0) {
        ms[b * 2 + 0] = acc[0] / K_SUB;
        ms[b * 2 + 1] = acc[1] / K_SUB;
      }
    }
  }
  if (sn) w->servo_lufs = (float)(sdb / (double)sn);

  int nblk = nsub - (K_BLOCK - 1);
  double sum[2] = { 0.0, 0.0 };
  int kept = 0;
  // Pass 1: the -70 LUFS absolute gate.
  for (int j = 0; j < nblk; j++) {
    double z0 = 0.0, z1 = 0.0;
    for (int k = 0; k < K_BLOCK; k++) { z0 += ms[(j + k) * 2]; z1 += ms[(j + k) * 2 + 1]; }
    z0 /= K_BLOCK; z1 /= K_BLOCK;
    double l = -0.691 + 10.0 * log10(z0 + z1 + 1e-30);
    if (l > -70.0) { sum[0] += z0; sum[1] += z1; kept++; }
  }
  if (!kept) { free(ms); return; }
  double gamma = -0.691 + 10.0 * log10(sum[0] / kept + sum[1] / kept + 1e-30) - 10.0;

  // Pass 2: the -10 LU relative gate.
  double rsum[2] = { 0.0, 0.0 };
  int rkept = 0;
  for (int j = 0; j < nblk; j++) {
    double z0 = 0.0, z1 = 0.0;
    for (int k = 0; k < K_BLOCK; k++) { z0 += ms[(j + k) * 2]; z1 += ms[(j + k) * 2 + 1]; }
    z0 /= K_BLOCK; z1 /= K_BLOCK;
    double l = -0.691 + 10.0 * log10(z0 + z1 + 1e-30);
    if (l > -70.0 && l > gamma) { rsum[0] += z0; rsum[1] += z1; rkept++; }
  }
  free(ms);
  w->lufs = rkept ? (float)(-0.691 + 10.0 * log10(rsum[0] / rkept + rsum[1] / rkept + 1e-30))
                  : (float)(gamma + 10.0);
}
