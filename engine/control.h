// Omanoise v4 — shared control state between the command thread and the
// realtime thread. Everything here is atomic; nothing else crosses.
#ifndef OMANOISE_CONTROL_H
#define OMANOISE_CONTROL_H

#include "common.h"
#include "env.h"
#include <stdatomic.h>

struct control {
  atomic_int playing;
  atomic_int mode;
  atomic_int binaural;
  atomic_int adaptive;
  atomic_int pulse;
  atomic_int modulation;        // 16 Hz AM (focus only)
  _Atomic float volume;
  _Atomic float intensity;
  _Atomic float brightness;
  _Atomic float tonal;
  _Atomic float env[ENV_N];     // Environment bank levels, 0..1 (ENV_* order)
  // 1 when the slot can make a sound: the synthesized slots always, a recorded
  // slot once its WAV has loaded. Mirrored into the state JSON as `sounds`.
  atomic_int sounds[ENV_N];
  // time-of-day adaptation, computed by the command thread
  _Atomic float adapt_bright;   // octaves added to cutoffs
  _Atomic float adapt_root;     // semitones added to the key root
  // realtime -> command
  _Atomic float meter;          // smoothed output RMS
  _Atomic float lufs;           // short-term K-weighted loudness
  atomic_int idle;              // 1 when faded out completely
  atomic_int bank_ready;
  atomic_int sf2_ready;         // the SoundFont is loaded and playable
  atomic_int melody_events;     // stats counter
};

extern struct control ctl;

#endif
