# Implementation progress — PLAN item → state

Legend: **done** = implemented and verified in the source / by measurement ·
**partial** = present but not to the plan's numbers · **missing** = absent.

---

# v5 (recorded environment sounds, `docs/PLAN-sound-v5.md`) — 2026-09-17

Build: `./build.sh`, `gcc -O2 -Wall -Wextra -std=gnu11`, zero warnings (also
clean under `-Werror`). Measurements are under the "v5" heading of
`docs/ACCEPTANCE.md`.

## Engine

| PLAN item | file | state |
|---|---|---|
| six slots, `ENV_N` 6, names ocean/rain/fire/wind/stream/birds | `env.[ch]`, `control.h` | done |
| synthesized fire and wind **deleted** | `env.[ch]` | done — `fire_t`, `wind_t`, `crackle_t`, `pop_t`, `env_fire`, `env_wind` and their control code are gone, not disabled; `rain_t` untouched |
| defaults ocean 0.8, the rest 0 | `layers.c:ctl`, state file | done |
| WAV loader in the bank worker thread, never the RT thread | `wav.[ch]`, `main.c:sounds_load` | done — strict RIFF/WAVE: PCM (or extensible-PCM) 16-bit, 48 kHz, 1–2 channels, ≥ 1 s; anything else is rejected with one stderr line and the slot is marked unavailable |
| path `<pluginDir>/sounds/<name>.wav`, `ocean.wav`/`rain.wav` override the generators | `main.c:sounds_dir`, `layers.c` | done — plugin dir from `/proc/self/exe`; `$OMANOISE_SOUNDS` overrides; `fire` reads `fireplace.wav` (`ENV_FILE[]`) |
| stored as int16 interleaved, converted on read | `wav.h:wav_t`, `env.c:rec_audio` | done — 57.8 MB for the four, read straight into the final buffer (no intermediate copy); mono is fanned out on read |
| per-file loudness measured at load, calibration replaces `ENV_CAL_DB` | `wav.c:wav_measure`, `layers.c` | done — **deviation 1**: the gain comes from the mean *short-term* loudness (through the slot filters and the master width), not the gated integrated value, because that is the statistic the auto-trim converges on; both are measured and the gated one is reported. `ENV_CAL_DB` now only serves slots with no recording |
| published to the RT thread with one atomic pointer swap | `layers.c:engine_set_sounds` | done — `const recbank_t *_Atomic`, acquired once per control block; each player re-attaches only when its pointer changes |
| `"sounds"` object in the state JSON | `control.h:ctl.sounds`, `main.c:state_json` | done — six keys, synth slots always true |
| two heads, random segments, no plain loop, no pitch change | `env.c:rec_pick`, `rec_audio` | done — speed 1.0 only; 6–14 s segments / 2.5 s crossfade for files under 40 s, 20–45 s / 4 s above, both capped by the file's own length |
| segments never within 1 s of the file ends | `env.c:rec_attach` | done |
| consecutive segments overlap ≤ 30 % | `env.c:rec_pick` | **partial / deviation 2** — exact for stream and birds (100 % of handovers); best-effort for the 25.5 s fireplace and the 14.8 s wind, where the rule and the plan's segment lengths cannot both hold. 115 of 128 handovers measured within 30 %, worst 60 % |
| equal-power (sin/cos) crossfade | `env.c:rec_audio` | done — gains sum to constant power; measured: the largest sample step inside the crossfade windows is never worse than outside them |
| per slot HP 40 Hz, LP 9 kHz ±0.7 oct with Brightness, ≤ 0.05 Hz ±1.5 dB drift | `env.c:rec_control` | done (drift base 30 s → 0.024–0.048 Hz) |
| bus: level² × calibration × soft-knee sum × plate send | `layers.c` | done — plate send −14 dB, and −18 dB for stream and birds |
| loudness servo and master chain unchanged | `master.[ch]` | done — not edited |
| RT thread never allocates, locks or prints | `env.c`, `layers.c` | done — the segment log is a fixed array written by the audio thread and printed by `--stats` after the render ends |

## Protocol, UI, docs

| PLAN item | file | state |
|---|---|---|
| `env <one of six> <0..1>` | `main.c` | done, with the six-name usage error |
| six keys in the `env` object, state file version 5 + migration | `main.c` | done — a v4 file keeps every field and gains `stream`/`birds` at 0 |
| `--env` override, `--stats` reports six levels | `main.c` | done, plus a new `segments` line per recorded slot |
| six slider rows, unavailable rows at 40 % opacity with the missing-file text | `Panel.qml` | done — the existing `ParamRow` gained nothing; the row sets `opacity`/`enabled` and puts `sounds/<file>.wav missing` in the hint |
| glyphs verified against the bar font | `Service.qml:envSounds` | done — all six codepoints are inside JetBrainsMono Nerd Font's `f0001-f1af0` range (`fc-query --format='%{charset}'`); Stream 󰶟 U+F0D9F, Birds 󱗆 U+F15C6 are the new ones |
| service mirrors `sounds`, `envAvailable()` | `Service.qml` | done; `stateJson()` carries it too |
| CLI help lists the six names | `~/.local/bin/omanoise` | done (and its `help` no longer prints three lines of script) |
| README: recordings, licences, drop-in, segment scheduling | `README.md` | done |
| manifest 5.0.0 | `manifest.json` | done |

## Not met, kept with reasons (details in ACCEPTANCE v5)

- **Birds' integrated loudness** is −18.0 / −20.4 against −23 ±1.5, and its
  1-second loudness steps are 16.9 / 18.0 LU against ≤ 4. Both are the
  recording: `birds.wav` has an LRA of 16.4 LU, so a gated power average follows
  the close calls while the loudness servo follows the whole recording. The gap
  is a property of the material and **cannot be closed by calibration** — the
  trim cancels any gain change. Measured to come from the birdsong, not the
  scheduler (the loudness steps are larger *outside* the crossfade windows than
  inside them). In the blend test birds sits at 0.3 and the mix lands on target.
- **The ≤ 0.25 sample-step "click" limit** is met by wind only (0.017). For
  fire, stream and birds it is measuring the recordings' own transients: the
  source files' largest steps scaled by the chain gain predict the rendered
  numbers to within 0.01–0.06, and the crossfades add nothing.
- **True peak −0.9 dBTP on one fire render** (limit −1). `fireplace.wav` has a
  29.6 dB peak-to-loudness ratio; at −23 LUFS its loudest crackles reach the
  limiter's soft knee for 165 samples in 14.4 M (0.001 %), and inter-sample
  peaks put the true peak 0.7 dB over the 0.83 ceiling.
- **Trim outside ±2 dB on 2 of 8 solo renders** (stream +2.24, birds +3.47);
  the other six are −1.37 … +0.69.
- **The 30 % overlap rule** on the two short files, above.

---

# v4 / v4.1 (three modes + the environment bank, `docs/PLAN-sound-v4.md`) — superseded by v5 above

v4.1 (2026-09-16, late): rain's droplet chirps removed after the "bubble
popping" complaint, and the synthesized fire rebuilt crackle-forward. That fire
and that wind are the two generators v5 deletes in favour of recordings; rain's
v4.1 form is what ships.


Build: `./build.sh`, `gcc -O2 -Wall -Wextra -std=gnu11`, zero warnings (also
clean under `-Werror`). All measurements are under the "v4" heading of
`docs/ACCEPTANCE.md`.

## Modes

| PLAN item | file | state |
|---|---|---|
| enum `M_FOCUS, M_RELAX, M_ENV`, names focus / relax / environment | `brain.h`, `brain.c` | done |
| Focus untouched | `brain.c` | done — mode row byte-identical; regression measured over 6 × 180 s (ACCEPTANCE v4-1) |
| Relax absorbs Sleep, parameters halfway | `brain.c:MODES[M_RELAX]` | done — the full parameter table with both v3 sources is in ACCEPTANCE v4-2 |
| noise bed "halfway": colour, dB, LP, boost, ocean_db | `brain.c` | done — 0.70 / −12.5 dB / 2592 Hz / 8.5 dB / −11.0 dB (dB averaged in dB, Hz geometrically) |
| Sleep's 20-minute phasing and all Sleep-only code removed | `layers.c`, `brain.c`, `master.[ch]` | done — phasing ramps, `sleep_t`, `sleep_fade`, the Sleep chord pool, the pad period stretch and `master_run`'s `post_gain` are all deleted |
| Environment tonal layer faint (pad −18, bass −20, warm 1.0/glass 0.15, cut 2.0, chords 120–160 s, no instruments, plate −10) | `brain.c:MODES[M_ENV]` | done |
| Environment `lufs_target` −23, noise bed off | `brain.c`, `layers.c` | done (`noise_db ≤ −90` gates the bed off entirely) |
| tonal slider still scales the tonal layer (0 = pure environment) | `layers.c` | done; the tonal-0 loudness tilt is now a per-mode field (`tonal_tilt`), −11 dB for Focus/Relax and 0 for Environment |
| aliases `sleep` → relax, `ocean`/`env` → environment | `brain.c:MODE_ALIAS` | done, used by both the command and the state file |

## Environment bank

| PLAN item | file | state |
|---|---|---|
| four generators on one bus, user level each, all running at once | `layers.c` | done |
| ocean = the v3 engine, unchanged | `layers.c:ocean_t` | done — same code, only its gain source changed |
| rain: patter / hiss / droplets | `env.c:env_rain` | done, **deviation**: the hiss low-pass is a 4-pole cascade at 6.5 kHz (ACCEPTANCE v4 deviation 3) |
| fire: glow / mid hiss / crackles / pops | `env.c:env_fire` | done, **deviation**: mid hiss −9 dB rel glow instead of −12 (deviation 2) |
| wind: brown noise, two cascaded LP 180–800 Hz, gusts, decorrelated channels, no whistle | `env.c:env_wind` | **partial / deviation 1** — pink instead of brown plus a −14 dB leaf-rustle band; the plan's version misses the fluctuation limit by 6 dB, the shipped one by 0.37 dB on one render in two |
| level² mapping, each generator calibrated to the target at level 1.0 | `layers.c:ENV_CAL_DB` | done — ocean −4.4, rain +7.5, fire −16.6, wind −12.6 dB |
| soft-knee sum → plate send −14 dB → master | `layers.c` | done — power-preserving sum, gain 1/√max(1, Σ level²) |
| defaults ocean 0.8, rain/fire/wind 0 | `layers.c:ctl`, state file | done |
| only value-noise drifts ≤ 0.3 Hz and Poisson events | `env.c` | done, enumerated in ACCEPTANCE v4-6 |

## Protocol, UI, docs

| PLAN item | file | state |
|---|---|---|
| `env <ocean\|rain\|fire\|wind> <0..1>` | `main.c:handle_command` | done, with clamping and a usage error |
| `"env"` object in the state file and in every `state` line | `main.c:state_json`, `load_state` | done |
| state file version 4 + migration | `main.c` | done — v3 `sleep`/`ocean` modes resolve to the new names; only pre-v3 files still get modulation/pulse forced off |
| `--env name=v,...` render override | `main.c:apply_env_override` | done; `--stats` also reports the four levels |
| 3 mode buttons, new blurbs, Sleep entry gone | `Service.qml:modes` | done |
| ENVIRONMENT section between MODE and SOUND, four sliders | `Panel.qml` | done — a `Repeater` over `svc.envSounds` reusing `ParamRow`, which gained one optional `apply` callback rather than a duplicated component |
| service mirrors `env`, `setEnv()` | `Service.qml` | done |
| binaural hidden in Environment | `Panel.qml` | done (modulation and pulse were already Focus-only) |
| IPC `env(name, v)` | `Panel.qml:IpcHandler` | done — **but the shell's registered handler is the stale pre-edit one until the next shell restart**, see ACCEPTANCE v4-8 |
| manifest version 4.0.0 | `manifest.json` | done |
| README: modes, environment bank, protocol | `README.md` | done |
| `~/.local/bin/omanoise env <name> <0-1>` | CLI | done |
| acceptance runs recorded | `docs/ACCEPTANCE.md` | done, under the v4 heading |

## Not met, kept with reasons (details in ACCEPTANCE v4)

- **Wind solo FLUCT** is 0.37 dB over the ocean + 3 dB limit on one render of
  two (the plan's own wind design was 6 dB over).
- **Relax LRA** is 5.1–5.4 against the v2 window's ≥ 6: the merge pulled
  `drift_depth` and the macro contour toward Sleep's steadier values, which is
  what the merge is.
- **One Focus render in six** reports its three spectral band ratios 2.7–3.4 dB
  low together (a low bass octave in that render); every other metric of that
  render, and all five other renders, are inside the v3 range.

---

# v3 (sound engine v3, `docs/PLAN-sound-v3.md`) — superseded by v4 above

The v3 table below describes the tree as it was before v4. Sleep is gone; the
Ocean mode became Environment.

Build: `./build.sh`, `gcc -O2 -Wall -Wextra -std=gnu11`, zero warnings.
All measurements are under the "v3" heading of `docs/ACCEPTANCE.md`.

## Phase 1 — FluidSynth voice layer

| PLAN item | file | state |
|---|---|---|
| `fluidsynth` linked via pkg-config | `build.sh` | done |
| settings: 48 kHz, reverb/chorus off, polyphony 48, `threadsafe-api 0`, `dynamic-sample-loading 1`, gain 0.4 | `sf2.c:sf2_create` | done |
| SoundFont loaded in the existing bank worker thread | `main.c:bank_worker` | done (37–40 ms) |
| verify `noteon` does not load samples on the RT thread | measured | done — `program_select` is what pages data in (RSS grows there, 0.1–4 ms/preset); `noteon` afterwards costs 4–20 µs and does not grow RSS. Presets are selected once, in the worker, and never change, so the RT thread never loads. No pre-warm needed; dynamic loading stays on. |
| per-channel `GEN_VOLENVATTACK` for attack ≥ 150 ms | `sf2.c` | done, audited (`--render-bank`): 184–1164 ms |
| per-channel darker `GEN_FILTERFC` | `sf2.c` | **partial** — a fixed −300…−500 cent offset per channel, plus a Cytomic low-pass on the melody bus that the Brightness slider drives. The generator alone is unreliable: SoundFont generators are additive over a preset-dependent base, and an "absolute 4.5 kHz" offset cost Vibraphone 12 dB while leaving the piano untouched. |
| per-channel `GEN_VOLENVRELEASE` ≥ 1.5 s | `sf2.c` | **partial** — a +1200 cent (×2) offset rather than an absolute value, same additivity problem; notes are instead held 2.5–6 s until the sample has decayed on its own, so the release is inaudible either way. |
| channel map: piano / harp / mallet / music box | `sf2.c:CHANS` | done — 0, 46, **11 (Vibraphone)**, 10. Kalimba (108) was auditioned and rejected, see ACCEPTANCE v3 §3. |
| `write_float` into the melody bus | `layers.c` (`E.fl_l/fl_r`, filled once per 64-sample control block) | done |
| CC10 pan per channel, drifting, never jumping | `layers.c` + `sf2.c:sf2_pan` | done (one 40 s value-noise drift, four fixed offsets, 0.02 dead-band) |
| `"sf2"` state field, fallback when the file is missing | `main.c`, `control.h` | done, tested with `OMANOISE_SF2=/nonexistent` |
| `--render-bank` preset audit (attack / peak / centroid) | `main.c:audit_note` | done |

## Phase 2 — PADsynth pads and the removals

| PLAN item | file | state |
|---|---|---|
| own radix-2 FFT, no FFTW | `padsynth.c:fft_radix2` | done (round-trip error 2.4e-7 at n = 1024) |
| Nasca's algorithm, N = 2^18 | `padsynth.c:padsynth_render` | done |
| two profiles (warm / glass) | `padsynth.c:pad_amp` | done |
| two independent-phase tables each, L/R | `bank.c`, `layers.c` | done |
| bandwidth 50 / 35 cents | `padsynth.c` | **deviation: 22 / 16 cents**, see ACCEPTANCE v3 "deviations" 1 |
| generated in the worker, < 1.5 s for four tables | `bank.c:bank_render` | done — 55–60 ms, 4 MB |
| interpolated table playback, ratio within 0.5–2 | `layers.c` (pad register capped at 128–460 Hz against a 220 Hz table) | done |
| pad attack 4–6 s, release 8–12 s raised-cosine | `layers.c:pad_control` | done |
| per-voice low-pass with ≤ 0.1 Hz value-noise drift, no LFO | `layers.c` | done (drift base periods 17–33 s) |
| tanh stage on pads removed | `layers.c` | done |
| supersaw pad renderer deleted | `bank.c` | done |
| Risset / FM bell / Karplus-Strong deleted | `bank.c` | done (no renderer, no sample, no trigger) |
| Solina chorus deleted | `fx.[ch]` | done |
| shimmer bus deleted | `fx.[ch]`, `layers.c` | done |
| granular texture deleted | `layers.c` | done |
| major pentatonic only, no minor pool | `brain.c` | done |
| sparser, softer events; ≥ 1.5 s apart | `brain.c`, `layers.c` | done |
| ocean layer added to Relax (−12 dB) and Sleep (−10 dB) | `brain.c` | done |
| plate decay ≤ 0.90, damping ≤ 3.5 kHz, input HP 200 Hz, return ≤ −8…−10 dB | `brain.c`, `layers.c` | done |

Also removed, beyond the plan: the **ping-pong delay** and its per-event
probability. It was not on the removal list, but every delayed repeat is an
extra onset 0.75–1.25 s after the note, which contradicts §1.6's "no two events
closer than 1.5 s".

Also added, beyond the plan:
- **Just-intonation snapping** of the sustaining voices (`brain.c:ji_snap`).
  PLAN §2.1 allows "gentle just-intonation nudges"; they turned out to matter,
  because equal temperament puts a 1–8 Hz beat on every nearly-coincident
  harmonic in a chord — the fluctuation band the acceptance metric measures.
- **A low-register spacing rule**: below 250 Hz no two sustaining voices may be
  closer than an octave (Plomp & Levelt — a fifth down there is inside one
  critical band).
- The Dattorro plate's tank modulation is now two slow **value-noise drifts**
  instead of the paper's 0.7 Hz sine, so no periodic oscillator reaches the
  audio at all.

## Phase 3 — defaults, UI, docs, acceptance

| PLAN item | file | state |
|---|---|---|
| `modulation` / `pulse` default off, depth cap 0.35 | `layers.c`, `brain.c` | done |
| `binaural` default off | `layers.c` | done (already was) |
| state file `"version":3` + v2 migration | `main.c` | done |
| `"sf2"` in `state` JSON and mirrored in QML | `main.c`, `Service.qml` | done |
| Panel mode blurbs rewritten | `Service.qml` | done |
| "can feel tense — off by default" on the modulation toggle | `Panel.qml` | done |
| "LOADING SOUNDS" hero status while `sf2` is false | `Panel.qml` | done |
| "Install soundfont-fluid for piano and harp" when the file is missing | `Panel.qml` + `Service.qml:sf2Proc` | done |
| README: dependencies, how v3 sounds are built, protocol | `README.md` | done |
| roughness / fluctuation metric in `--stats` | `main.c:envcap_*` | done |
| acceptance runs recorded under a "v3" heading | `docs/ACCEPTANCE.md` | done |
| `~/anrin-setup/SETUP.md` notes the packages | (outside the plugin) | already present |

---

# v2 (superseded; kept for the record)

The v2 table below describes the tree as it was before v3. Items marked
**removed in v3** are gone from the source entirely.

## Phase 1 — foundations & pad

| PLAN item | state |
|---|---|
| engine split into `engine/*.c\|h` | done |
| DSP primitives: PolyBLEP, Cytomic SVF, value-noise drift, delay lines, biquads | done |
| Instrument bank rendered in a worker thread | done |
| Pad A 7-saw / Pad B FM / bass / bells / plucks / drop / kick / tick | **removed in v3** except bass, kick and tick |
| crossfade sustain loops, peak normalisation | done (bass only now) |
| sample voices with Eno clocks 17.3/21.9/26.1/31.7 s, bass 37.4 s | done |
| chord pool + Markov with ≥2 common-tone bias | done |
| voice leading, no m2/tritone between sustaining voices | done, plus the v3 low-register rule |
| Solina chorus | **removed in v3** |
| Dattorro plate ×1.61, pre-delay 40 ms | done; tank modulation de-periodised in v3 |
| master chain with LUFS auto-trim | done |
| noise bed brown/pink | done; louder and broader in v3 |

## Phase 2 — melody, bells, pulse, FX, duck, shimmer

| PLAN item | state |
|---|---|
| Risset + FM bells, KS plucks | **removed in v3** |
| Markov melody, phrases, pan drift | done; rates, velocities and instruments replaced in v3 |
| probabilistic per-event FX (delay / octave-up / granular smear) | **removed in v3** |
| ping-pong delay | **removed in v3** |
| ducking ≤2.5 dB, ~300 ms release | done |
| shimmer bus | **removed in v3** |
| soft kick + off-beat tick, Focus, 62 BPM | done, default off in v3 |

## Phase 3 — nature, texture, evidence toggles

| PLAN item | state |
|---|---|
| ocean engines, rumble + lagging foam | done; now in Relax and Sleep too |
| rain/water drops (KS) | **removed in v3** (Karplus-Strong, 6 ms attack) |
| granular cloud | **removed in v3** |
| 16 Hz AM stage | done, default off and depth-capped in v3 |
| binaural default off | done |
| sleep phasing 0–10 / 10–20 / 20+ min | done |
| daypart hooks | done |

## Phase 4 — polish

| PLAN item | state |
|---|---|
| `--render`, `--tail`, `--stats`, `--layers <mask>`, `--render-bank` | done; `--layers 1` now really gates the core, `--stats` carries the roughness metric, `--render-bank` audits the SoundFont presets |
| `modulation` stdin command, `state` gains `modulation` / `bank` / `lufs` | done; v3 adds `version` and `sf2` |
| QML mirrors the engine state | done |
| README describes the protocol / modes / sound | done (rewritten for v3) |
