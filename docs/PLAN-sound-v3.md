# Omanoise sound engine v3 — sampled instruments + PADsynth, tuned for calm

Status: approved by Josh ("do what is best"). Implementer: Claude Opus. Supersedes the *instrument* parts of PLAN-sound-v2; everything else in v2 (PipeWire I/O, stdin protocol, state file, brain/sequencer, nature beds, master chain, acceptance method) stays.

Read first: `docs/research-opensource.md` (why sampled instruments + PADsynth, and the psychoacoustics of anxiety), then `docs/PROGRESS.md` + `docs/ACCEPTANCE.md` (v2 state), then the engine sources.

## 0. Listening verdicts to design against
- v1: "wind in a mic".
- v2: **Ocean = best, Relax = worst**, Relax "makes me feel a bit anxious". Relax had the loudest bells (−6.5 dB), strongest shimmer (−8 dB), Risset inharmonic/detuned partials, minor pentatonic, supersaw pads with ±cents detune.
- Conclusion: the noise/nature beds are right; the *hand-synthesized musical layer* is the anxiety source. Ocean is the reference for "calm" and its envelope statistics become an acceptance baseline (§5).

## 1. Psychoacoustic rules (non-negotiable, from research-opensource §"anxious vs calming")
1. **No periodic modulation anywhere between 1 Hz and 300 Hz** on any audible layer: no tremolo, no chorus LFO, no detune beating > ~1 Hz, no 4 Hz-ish fluctuation. Slow drifts must be ≤ 0.2 Hz and randomized (value noise), never periodic.
2. **Attack ≥ 150 ms on every musical event** (startle threshold is 12 ms; 141–220 ms fully mitigates). Sampled piano/harp attacks are forced up via the per-channel volume-envelope-attack generator.
3. **No inharmonic tones**: no Risset bells, no FM bells, no Karplus-Strong plucks. Remove them from the melody kinds (code may stay for `--render-bank`, but no mode may trigger them).
4. **No supersaw / unison detune**: pads come from PADsynth (Gaussian-spread partials, no discrete beats). Delete the detuned-saw pad from the bank.
5. **Major pentatonic only**, wide consonant voicings (add9/6, fifths, octaves), no minor mode. Root per mode as v2, daypart shifts as v2.
6. **Sparse and soft**: Relax one melodic event per 6–14 s, Focus per 3–8 s, Sleep pads only; velocities 28–62 (MIDI), never accents, no two events closer than 1.5 s.
7. **Less, darker reverb**: shimmer bus **removed from all modes**; plate decay ≤ 0.90, input high-passed at 200 Hz, damping ≤ 3.5 kHz; melody bus reverb return ≤ −10 dB.
8. **Defaults**: `modulation` (16 Hz AM is inside the 15–300 Hz roughness band) **default OFF**; `pulse` **default OFF**; `binaural` off (already). Toggles stay.
9. **Nature is a feature, not a bed**: a quiet ocean layer (the v2 Ocean engine) is added to Relax (−12 dB) and Sleep (−10 dB, then crossfading to pink after 20 min as v2). Focus keeps the brown bed only.

## 2. Instrument layer

### 2.1 libfluidsynth (sampled piano, harp, mallets)
- Dependency: `fluidsynth` (LGPL, installed) and `/usr/share/soundfonts/FluidR3_GM.sf2` (MIT, installed). `build.sh` adds `$(pkg-config --cflags --libs fluidsynth)`.
- Settings: `synth.sample-rate 48000`, `synth.reverb.active 0`, `synth.chorus.active 0`, `synth.polyphony 48`, `synth.dynamic-sample-loading 1` (keeps RSS low; FluidR3 is 148 MB), `synth.threadsafe-api 0` because **all synth calls (noteon/noteoff/cc/write_float) happen on the realtime thread**; `synth.gain` ~0.4 then trimmed in our mix. Load the SoundFont in the existing bank worker thread (`fluid_synth_sfload`); until ready, state `"sf2":false` and only PADsynth + nature play. If the file is missing, run permanently in that fallback and report `"sf2":false`.
- Channels/presets (FluidR3_GM bank 0): ch0 Acoustic Grand Piano (0), ch1 Orchestral Harp (46), ch2 Kalimba (108), ch3 Music Box (10) / Celesta (8), ch4 Vibraphone (11), ch5 Choir Aahs (52) optional low-level, ch6 Warm Pad (89) as PADsynth fallback only. Opus may audition alternatives by rendering single notes with `--render-bank`, but must stay within acoustic/mallet/keyboard families — no synth leads, no brass, no bells with inharmonic partials (Tubular Bells 14 is out).
- Per-channel generators, set once after load: `GEN_VOLENVATTACK` so attack ≥ 150 ms (timecents = 1200·log2(seconds)); `GEN_VOLENVRELEASE` ≥ 1.5 s; `GEN_FILTERFC` lowered (e.g. 4–6 kHz) for darkness; per-note pitch bend allowed for gentle just-intonation nudges (optional).
- Mixing: `fluid_synth_write_float` into a stereo scratch per block → melody bus (duck, plate send, gain) exactly where v2's melody one-shots went. Pan via CC10 per channel, drifting slowly, never jumping.

### 2.2 PADsynth pads (public domain algorithm, Paul Nasca)
- Implement Nasca's algorithm in C (own radix-2 complex FFT, N = 2^18 or 2^19 samples ≈ 5.5–11 s loop at 48 kHz): harmonic profile A_n, bandwidth per harmonic bw_n = bw_cents·(2^(cents/1200)−1)·f·n^(bwscale), Gaussian spread, random phases, IFFT, normalise, seamless loop by construction. Two profiles: **warm** (A_n = 1/n^1.6, n ≤ 32, odd harmonics slightly favoured, bw 50 cents) and **glass** (A_n = 1/n^2.4 with a gentle bump at n = 2,3, bw 35 cents). Two independent tables per profile (different random phases) → L and R read separately for width **without** beating between channels (different phases, identical spectra, no detune).
- Generate in the bank worker (report time; target < 1.5 s for four tables). Playback: linear/4-point interpolated read at ratio f_target/f_table; base table frequency ~110 Hz; keep ratio within 0.5–2.
- Pad voices keep the v2 Eno clocks and voice-leading; attack 4–6 s, release 8–12 s raised-cosine; per-voice 2-pole low-pass at 2–3·f0 with **value-noise drift ≤ 0.1 Hz**, no LFO. Cascaded Cytomic SVF from v2 stays; the tanh stage is removed (no need, no harmonics wanted).
- Delete: supersaw pad renderer, Risset, FM bell, KS pluck from active layers; the Solina chorus is **removed** (rule 1: its 0.9 Hz and 5.9 Hz LFOs are periodic modulation). Width comes from the two-table PADsynth trick and the plate.

### 2.3 Mode profiles (volume 1.0)

| | Focus | Relax | Sleep | Ocean |
|---|---|---|---|---|
| LUFS target | −21 | −24 | −28 | −23 (unchanged) |
| Pads | PADsynth warm, cutoff 2.5·f0 | PADsynth warm 70 % + glass 30 % | PADsynth glass, cutoff 1.6·f0 | unchanged (quiet pad or none) |
| Melody instruments | piano (low-mid register C3–C5) + kalimba | harp + piano (C2–C4), occasional music box very soft | none | none |
| Melody rate | 1 event / 3–8 s, phrases 3–6 notes then 10–20 s rest | 1 event / 6–14 s, mostly single notes and dyads | 0 | 0 |
| Velocity | 34–62 | 28–52 | — | — |
| Melody bus level | −12 dB | −12 dB | — | — |
| Plate decay / damping / return | 0.84 / 3.5 kHz / −10 dB | 0.90 / 3 kHz / −8 dB | 0.90 / 2.5 kHz / −8 dB | unchanged |
| Shimmer | off (removed) | off | off | off |
| Noise bed | brown −20 dB LP 1.2 kHz | pink-brown −22 dB + **ocean −12 dB** | pink −18 dB + **ocean −10 dB** (20 min crossfade as v2) | ocean engine as v2 |
| Granular texture | off | off | off | off (remove from all modes; it re-introduces grain-rate modulation) |
| Pulse | toggle, **default off**, −22 dB if on | off | off | off |
| 16 Hz AM | toggle, **default off**, depth cap 0.35 | off | off | off |

Sliders unchanged: Intensity scales event rate ×(0.4…1.6) and noise ±3 dB; Brightness ±1 octave on pad cutoff / plate damping / fluid `GEN_FILTERFC`; Tonal crossfades instruments+pads vs nature/noise (at 0 → pure nature, i.e. "Ocean-like" in every mode).

## 3. Protocol / UI / docs
- New state fields: `"sf2":bool` (SoundFont loaded). `modulation`/`pulse` defaults become false in the engine (state file values still win if present — **but on first v3 start, if the state file was written by v2 (no `"version":3` key), reset modulation and pulse to false and write `"version":3`**).
- Panel.qml: mode blurbs → Focus "Soft piano and kalimba over a warm pad", Relax "Harp, low piano, distant waves", Sleep "Slow glass pad, waves fading to pink noise", Ocean unchanged. Neural-modulation toggle description adds "(can feel tense — off by default)". Hero status shows "LOADING SOUNDS" while `sf2` is false and the file exists; if the SoundFont is missing show a one-line hint "Install soundfont-fluid for piano and harp". Nothing else in QML; Service.qml mirrors `sf2`.
- README: dependencies (fluidsynth, soundfont-fluid), v3 sound description, protocol fields. `~/anrin-setup/SETUP.md` already notes the packages.
- Remove dead code paths rather than leaving them disabled, except what `--render-bank` needs for auditing; keep the build warning-free.

## 4. Phases
1. **FluidSynth voice layer**: settings, worker-thread load, generators, channel map, write_float into melody bus, `sf2` state, fallback. Audit presets with `--render-bank` (render one soft note per candidate preset to files; check attack ≥150 ms after the generator override, peak, spectral centroid < 2 kHz).
2. **PADsynth pads**: FFT, tables, playback, replace supersaw; remove Solina chorus, shimmer, granular, Risset/FM/KS triggers; brain retuned to major pentatonic + new densities/velocities; ocean layer into Relax/Sleep.
3. **Defaults, UI, docs**, state-file migration, acceptance runs, tuning.

## 5. Acceptance (extends v2 §5; record in `docs/ACCEPTANCE.md` under a "v3" heading, 3 renders × 180 s per mode)
1. v2 tests 1, 2, 4, 6, 7, 8 still hold with the new LUFS targets; test 3 (tonal 1.0 vs 0.0 ≥ 8 LU) holds for Focus/Relax; Sleep ≥ 5 LU.
2. **Roughness/fluctuation metric (new, the key one)**: add an offline analysis in `--stats` that computes the signal envelope (|x| → one-pole LP 100 Hz → decimate to 400 Hz), removes the mean, takes its power spectrum (reuse the PADsynth FFT), and reports **FLUCT** = energy in 1–8 Hz and **ROUGH** = energy in 15–200 Hz, both in dB relative to the envelope's DC power. **Requirement: Focus, Relax and Sleep must each have FLUCT and ROUGH ≤ Ocean's values + 2 dB** (Ocean is the user's "calm" reference), measured on the same runs. Also report the peak modulation frequency; it must not be a stable line (same ±0.2 Hz peak in all three renders) anywhere in 0.5–300 Hz for any mode except Ocean's wave period.
3. **Attack audit**: for every melody event kind actually triggered, render a single event via `--render-bank`; time from onset to 90 % of peak must be ≥ 150 ms.
4. **No periodic LFO**: grep-level audit listed in ACCEPTANCE.md — every remaining periodic oscillator in the control path is enumerated with its rate; anything between 0.2 Hz and 300 Hz that reaches the audio must be justified or removed (Ocean wave envelopes are randomized, not periodic).
5. **Resources**: RSS ≤ 120 MB with dynamic sample loading (report), CPU ≤ 4 % of a core, SoundFont + PADsynth ready < 3 s total (report ms).
6. **Fallback**: with `XDG_STATE_HOME` temp and the SoundFont path overridden to a non-existent file (add `OMANOISE_SF2` env override), the engine must still render all modes and report `"sf2":false`.

## 6. Constraints (unchanged from v2 §6)
No playback on the live system; verify offline; `pkill -x omanoise-engine` after a good build; no `omarchy restart shell` (report if hot-reload doesn't take); real state file must end with sane v3 defaults (`version 3`, mode focus, volume 0.7, sliders 0.5, binaural/modulation/pulse false, adaptive true); C11, −Wall −Wextra clean; RT thread never allocates/locks/prints (fluidsynth with threadsafe-api 0 and dynamic loading: verify that `fluid_synth_noteon` does not load samples synchronously on the RT thread — if it does, pre-warm every used preset in the worker by playing and immediately releasing one silent note, or disable dynamic loading and accept the RSS).
