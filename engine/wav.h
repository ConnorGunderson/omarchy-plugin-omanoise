// Omanoise v5 — recorded environment sounds: the WAV loader and the loudness
// measurement that calibrates them.
//
// Loading happens in the bank worker thread (never the realtime thread) and the
// result is published to the audio thread with one atomic pointer swap, exactly
// like the PADsynth bank and the SoundFont. Nothing here is called from the RT
// thread; the RT thread only ever reads a finished, immutable `wav_t`.
#ifndef OMANOISE_WAV_H
#define OMANOISE_WAV_H

#include "common.h"

// The mode's loudness target. A recorded slot at level 1.0, alone, has to land
// here before the master's auto-trim has to do anything (PLAN v5 §2.2).
#define REC_TARGET_LUFS (-23.0f)
// What the rest of the chain does to a slot between the environment bus and the
// master's loudness meter: the mode's -4 dB mix gain, the macro contour, the
// plate return, the stereo width and the 40 Hz / 9 kHz slot filters. Fitted
// from one sweep of renders and then verified on all four files (ACCEPTANCE
// v5-1).
#define REC_OFFSET_DB (4.1f)

typedef struct {
  int16_t *d;          // interleaved PCM, exactly as it is on disk
  int      frames;
  int      ch;         // 1 or 2 (mono is fanned out to both channels on read)
  float    lufs;       // BS.1770 gated integrated loudness of the whole file
  float    servo_lufs; // mean short-term loudness: what the master servo sees
  float    cal_db;     // level-1.0 calibration gain derived from `servo_lufs`
  float    seconds;
} wav_t;

// 0 = loaded. -1 = no such file. -2 = not 48 kHz 16-bit PCM mono/stereo, or
// truncated. Prints one line on stderr when it rejects a file.
int  wav_load(const char *path, wav_t *w);
void wav_free(wav_t *w);

// Fills `lufs` and `servo_lufs`:
//   lufs        BS.1770-4 integrated (K-weighting, 400 ms blocks at 75 %
//               overlap, -70 LUFS absolute and -10 LU relative gates) — the
//               number ffmpeg's ebur128 reports for the file.
//   servo_lufs  the mean of the short-term loudness (K-weighted, 3 s one-pole,
//               averaged in LU), which is the statistic the master's auto-trim
//               converges on. This is what the calibration is derived from, so
//               that a slot at level 1.0 leaves the trim at zero.
// Allocates a small scratch array; worker thread only.
void wav_measure(wav_t *w);

#endif
