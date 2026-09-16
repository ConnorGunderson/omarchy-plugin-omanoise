// Omanoise v3 — the sounding engine: PADsynth pad voices, bass, the sampled
// melody layer, the noise bed and ocean, the plate and the master chain.
#ifndef OMANOISE_LAYERS_H
#define OMANOISE_LAYERS_H

#include "common.h"
#include "bank.h"
#include "env.h"
#include "sf2.h"

int  engine_init(uint64_t seed);
void engine_free(void);
// Publish the finished instrument bank (called from the worker thread).
void engine_set_bank(const bank_t *b);
// Publish the loaded SoundFont synth (worker thread). After this call the
// object belongs to the realtime thread and nothing else may touch it.
void engine_set_sf2(sf2_t *s);
// Publish the loaded environment recordings (worker thread). The object must
// outlive the engine; it is read, never written, by the realtime thread.
void engine_set_sounds(const recbank_t *r);
// Segment log of a recorded slot, for `--stats`. `i < 0` returns the number of
// entries available; otherwise 1 (and the segment's file position and length in
// seconds) or 0 past the end. Read after rendering, never from the RT thread.
int engine_seg_log(int slot, int i, float *start_s, float *len_s);
// Realtime: fills `frames` interleaved stereo frames.
void engine_render(float *out, uint32_t frames);
// Diagnostics and offline phase measurement.
float engine_trim_db(void);
// Bitmask of enabled layer groups (see LAYER_* below). Default LAYER_ALL.
#define LAYER_CORE   1   // pads, bass, noise bed (the plate always runs)
#define LAYER_EVENTS 2   // sampled melody, pulse
#define LAYER_NATURE 4   // ocean, 16 Hz AM
#define LAYER_ALL    7
void engine_set_layers(int mask);

#endif
