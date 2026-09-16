# Omanoise sound engine v2 — implementation plan

Status: approved research, ready to implement. Owner of the plan: Josh. Implementer: Claude Opus.
Research this plan is built on (read all three before coding):
- `docs/research-endel.md` — how Endel is really built (sample sequencer, pentatonic, Markov, probabilistic FX)
- `docs/research-science.md` — what has evidence (level, no vocals, steady-state, dark spectrum, 16 Hz AM) and what is folklore (binaural beats, continuous noise for sleep)
- `docs/research-dsp.md` — concrete DSP recipes with numbers (PolyBLEP unison, Cytomic SVF, Solina chorus, Dattorro plate, shimmer, Karplus-Strong, Risset bells, granular, ocean, master chain)

## 0. The verdict we are fixing

User's listening verdict on v1: **"it just sounds like wind in a mic."** The UI, sliders and plugin plumbing are approved and must keep working unchanged.

Root cause (research-dsp §0): every v1 layer is a variant of *noise through a swept filter* — the literal textbook wind patch — and the tonal layers are static sines with no motion. The reverb is a ringing Schroeder comb bank. Energy lives in the wrong place.

v1 baseline measurements (20 s renders, volume 0.7): Focus −21.4 LUFS with LRA 1.4 LU (static), Relax −23.7 / 3.5, Sleep −28.1 / 5.1, Ocean −25.6 / 10.6. Spectral energy above 4 kHz: Focus −42 dB, Sleep −71 dB.

## 1. Design thesis (what the research converges on)

1. **Endel is a sequencer of curated one-shots, not a synth.** We cannot ship recorded samples, so we do the next best thing: **synthesize an instrument bank of one-shots into memory at startup** (pad notes, bells, plucks, soft kick, drops) with the high-quality techniques from research-dsp, then **sequence them Endel-style**. The "sound" comes from good one-shots + probabilistic FX + a musical brain, not from per-sample oscillators fighting for attention.
2. **Harmonic material carries the mix; noise is a quiet bed.** Pads/chords/bass ≥ 80 % of energy. Noise bed at −18 to −24 dB relative to the pad, low-passed ≤ 1.5 kHz, brown-leaning. Never a narrow mid-band sweep.
3. **Pentatonic, one key, slow diatonic chord drift, no adjacent semitones, overtone-spaced registers.** Markov note generation with allowed-next-note tables. 55–70 BPM grid for anything rhythmic.
4. **Everything moves on independent incommensurate clocks** (Eno loops, myNoise "188 million years"). No two parameters share an LFO.
5. **Space = modulated plate + shimmer**, not comb bank. Reverb tails low-passed. Focus has less reverb than Relax/Sleep.
6. **Evidence-based extras as toggles, correctly labelled**: 16 Hz amplitude modulation on the 200 Hz–1 kHz band for Focus (Woods 2024, Brain.fm patents) **on by default in Focus only**; binaural beats **off by default** (weak evidence); sleep = lowest energy, slow tonal events (~1 per 5 s), thinning over time.
7. **Steady-state, no salient events** (changing-state effect): onsets ≥ 100s of ms, no sudden pans, no strong melodies, velocities 0.3–0.7, bells duck under the pad.
8. **Loudness-managed and dark**: K-weighted meter with slow auto-trim to a per-mode LUFS target, HP 30 Hz, gentle LP 9 kHz, soft limiter. Hours-long listening must never fatigue.

## 2. Architecture

```
                       ┌──────────────── BRAIN (control rate, 64-sample blocks) ────────────────┐
                       │ key/chord pool + Markov  │ per-layer Eno clocks │ drift generators ×8 │
                       │ mode profile │ daypart │ intensity/brightness/tonal │ sleep phase timer │
                       └───────────────────────────────┬─────────────────────────────────────────┘
                                                       │ triggers, gains, targets
   INSTRUMENT BANK (rendered once at start, ~10–20 MB f32 mono):
     pad notes ×N (per scale degree, 2 timbres)   → PAD/CHORD voices (sample playback, long xfade env)
     bass notes                                    → BASS voice
     Risset bells, FM bells                        → MELODY/FX events
     KS plucks / drops                             → MELODY/FX events, rain drops
     soft kick, brushed tick                       → PULSE (Focus only)
   LIVE SYNTH:
     ocean/rain/noise bed (Farnell-style, ≤1.5 kHz)  → NATURE layer
     granular cloud over a rendered chord buffer       → TEXTURE layer

   BUSES:  pad ─┬─► Solina chorus ─┬─► [duck] ─► SUM
               bass ───────────────┘             ▲
               melody/fx ─┬─ probabilistic FX (delay / octave-up / granular) ─┘
               nature ─────────────────────────────────────────────────────┘
               SEND A: Dattorro plate (mode-dependent decay)    ─► SUM
               SEND B: shimmer loop (+12 st shifter → plate → fb) ─► SUM
   MASTER: HP 30 Hz → [16 Hz AM on 200–1000 Hz band, Focus] → M/S width → comp 1.5:1
           → LP 9 kHz → K-weighted LUFS auto-trim → volume² → soft limiter −1 dBTP
```

### 2.1 Instrument bank (rendered at startup, in a worker thread so the engine reports `state` immediately)
Render at 48 kHz mono, f32. Notes at the 5 pentatonic degrees over 3 octaves where needed; pitch-shift ±1 octave at playback via resampling so the bank stays small.
- **Pad A ("strings")**: 7 PolyBLEP saws, detune offsets {0,−11,−6,−2,+3,+7,+12} cents × 0.8, gains {1,.55,.7,.8,.8,.7,.55}, random phases → 2× cascaded Cytomic SVF LP at 2.5·f0 (Q 0.6) with tanh(0.5x) between → HP at 0.9·f0. Length 8 s, looping region 3–7 s (crossfade loop). Stereo achieved at playback by playing two decorrelated copies (different random phases) L/R.
- **Pad B ("glass")**: 2-op FM 1:2, index 0.6 with slow index drift baked in, 12 s. 
- **Bass**: single saw + sine sub, LP 1.5·f0, 6 s.
- **Risset bell**: 11-partial table from research-dsp §6, just-tuned, 8 s.
- **FM bell**: c:m 1:1.4, index 9 → <1 over 1 s, 6 s.
- **Pluck/drop**: extended Karplus-Strong, 3 ms noise burst LP 4 kHz, gain 0.997 (pluck) / 0.99 (drop), allpass-tuned, 3 s.
- **Soft kick**: 55 Hz sine, pitch drop 1 octave over 60 ms, exp decay 250 ms, LP 200 Hz. **Brushed tick**: 2 ms noise LP 3 kHz, decay 40 ms.
Each one-shot gets a 5 ms fade-in and exponential tail-out. Store peak-normalised.

### 2.2 Musical brain
- **Key**: C major pentatonic for Focus/Ocean (C D E G A), A minor pentatonic for Relax (A C D E G), G major pentatonic 1 octave down for Sleep (G A B D E). Daypart shifts root by 0 / −2 / −5 semitones (morning/day / evening / night) exactly as v1.
- **Chord pool** (4 per mode), e.g. Focus: Cmaj9(no3) [C G D E], Am9 [A E G B→omit] , Fmaj7add9 [F C E G], G6/9 [G D E A]; transitions every 60–90 s via a Markov table biased to ≥2 common tones. Voice leading: nearest chord tone ≤2 semitones, else stay.
- **Interval rules**: never sound a minor 2nd or tritone between sustaining voices; major 2nds allowed. Bass 55–110 Hz root/fifth only; pads 130–520 Hz spaced ≥ a 4th; melody 500–2000 Hz.
- **Eno clocks**: pad voices have periods {17.3, 21.9, 26.1, 31.7} s (× mode factor), attack 4 s / release 10 s raised-cosine; bass period 37.4 s.
- **Melody generator**: Markov over the 5 degrees with allowed-next tables (favour steps and fifths, forbid repeating the same note >2×), phrases of 4–9 notes, then rest. Event rate per mode below. Velocity uniform 0.3–0.7. Random pan −0.5..0.5 with slow drift, never a jump >0.1 between consecutive events.
- **Drift generators**: 8 value-noise generators (research-dsp §2), T = 8–40 s each, feeding: pad filter cutoff, chorus depth, plate damping, pan positions, layer gains between a "low" and "high" profile (myNoise Animate), noise bed level.

### 2.3 Mode profiles (targets at volume = 1.0)

| | Focus | Relax | Sleep | Ocean |
|---|---|---|---|---|
| LUFS target (integrated) | −20 | −23 | −28 | −23 |
| Pad timbre | A+B mix, cutoff 2.5·f0 | A, cutoff 2.0·f0 | B, cutoff 1.6·f0 | A quiet (−12 dB) |
| Chord change | 60–90 s | 75–120 s | 120–180 s | 120 s |
| Melody events/min | 12–24 (plucks, bells) | 4–10 (bells, harp-like plucks) | ≤ 4, one note ≈ every 5–15 s (Costa 2025), bells only | 0–2 drops |
| Pulse | soft kick 60–64 BPM, −18 dB, tick on off-beats −26 dB (toggle) | off | off | off |
| Plate decay / damping | 0.86 / 5 kHz, return −8 dB | 0.94 / 4 kHz, −5 dB | 0.96 / 2.5 kHz, −4 dB | 0.92 / 3 kHz, −6 dB |
| Shimmer return | −14 dB | −9 dB | −12 dB | off |
| Noise bed | brown, −20 dB, LP 1.2 kHz | pink-brown −22 dB, LP 1 kHz | pink −18 dB, LP 800 Hz | ocean engine ×2 (rumble+foam), −8 dB |
| Granular texture | −16 dB | −14 dB | −18 dB | off |
| 16 Hz AM (200–1000 Hz) | on, depth ramps 25 % → 60 % over 10 min | off | off | off |
| Sleep phasing | — | — | 0–10 min: waves + rare bell; 10–20 min: crossfade waves→pink bed; 20+ min: sparse pads, bells stop, −3 dB per 20 min | — |

Slider mapping: **Intensity** scales event rates ×(0.3…1.7), pulse level, granular density, noise bed by ±4 dB. **Brightness** shifts pad cutoff ±1 octave, plate damping ±1 octave, noise LP ±0.7 octave, bell partial LP. **Tonal** crossfades pad/melody bus (0 → −20 dB) against noise bed (0 → +6 dB); at tonal 0 the engine is a pure calibrated noise/ocean generator.

### 2.4 Effects (numbers from research-dsp)
- **Solina chorus** on the pad bus: 3 taps, centre 7 ms, slow LFO 0.9 Hz ±1.5 ms, fast 5.9 Hz ±0.2 ms, 3-phase. HP 60 Hz after.
- **Dattorro plate**, scaled ×1.61 to 48 kHz, tank allpass modulation ±12 samples @ 0.7 Hz, pre-delay 40 ms, input bandwidth 0.8.
- **Shimmer**: 2-head Hann octave-up shifter on a 80 ms buffer → HPF 150 / LPF 5 kHz → plate → feedback 0.55.
- **Probabilistic per-event FX** (Endel stem-multiplier idea): each melody event independently gets stereo delay (p=0.4, 3/8 and 5/8 beat at 60 BPM, fb 0.35, wet −10 dB), octave-up doubling (p=0.25, −12 dB), granular smear (p=0.2). 
- **Duck**: one-pole envelope follower on the melody bus reduces pad bus by up to 2.5 dB, release 300 ms.
- **16 Hz AM**: split master with a 2-pole LP at 1 kHz and HP at 200 Hz (Linkwitz-Riley or two SVFs); modulate the band with `1 − d·(0.5 − 0.5·cos(2π·16·t))`, phase-locked to the pulse grid (16 Hz = 32nd notes at 120 BPM ≡ 16th-note grid at 60 BPM). Depth `d` ramps 0.25 → 0.6 over the first 10 min and back to 0.45 after 50 min (patent envelope).
- **Binaural** (toggle, default off): ±half beat frequency detune on pad voices only (12/8/3 Hz as v1). Keep the command; flip the default.

### 2.5 Master chain
HP 30 Hz (2nd order) → AM stage → M/S width 1.3 with side HP 200 Hz → bus comp 1.5:1, 40 ms / 800 ms, ~1–2 dB GR → LP 9 kHz (1st order) → K-weighted (BS.1770 two-biquad) short-term LUFS meter driving a trim toward the mode target with τ = 30 s, clamped ±6 dB → `volume²` → soft limiter (5 ms look-ahead, −1 dBTP, tanh knee) → out. Denormals: FTZ/DAZ on the audio thread.

## 3. Protocol and UI changes (small, must stay backward compatible)

Engine stdin commands: keep all v1 commands. Add `modulation 0|1` (16 Hz AM, Focus only), keep `binaural 0|1` (default **false** now), keep `pulse`. `state` JSON gains `"modulation":bool` and `"lufs":number` (short-term, for the meter). `meter` line unchanged.

Panel.qml / Service.qml: 
- Add `modulation` to the mirrored state and `setFlag("modulation", …)`.
- Options section: first toggle becomes **"Neural modulation"** (desc: "16 Hz amplitude modulation on the low-mids, the one focus effect with EEG evidence"), visible in Focus only. "Binaural beats" toggle stays, description "Weak evidence — off by default", available in Focus/Relax/Sleep.
- Nothing else in the UI changes. The bar icon glyph is a separate, later task.

## 4. Implementation phases (each ends with the acceptance tests in §5)

**Phase 1 — Foundations & pad (the biggest audible win).** Split the engine into `engine/*.c|h` (dsp primitives: PolyBLEP, Cytomic SVF, value-noise drift, delay lines; bank renderer; brain; layers; fx; master; pipewire/io main). Implement instrument bank rendering in a worker thread, sample-playback voices with Eno clocks and voice-leading, chord pool Markov, Solina chorus, Dattorro plate, master chain with LUFS auto-trim. Remove the v1 wash/swell layers entirely. Noise bed = brown/pink at −20 dB LP 1.2 kHz only.
**Phase 2 — Melody, bells, pulse, probabilistic FX, duck, shimmer.** Risset/FM bells, KS plucks, Markov melody, per-event FX, ducking, shimmer bus, soft kick + tick for Focus.
**Phase 3 — Nature & texture & evidence toggles.** Ocean engine ×2 (rumble + lagging foam), rain drops (KS), granular cloud over a rendered chord buffer, 16 Hz AM stage with band split and depth envelope, binaural default off, sleep phasing timeline, daypart hooks.
**Phase 4 — Polish.** Tune per-mode gains to hit the LUFS/spectral targets, anti-click audit, CPU check, README + docs update, QML toggle changes, `--render` still works for all modes.

## 5. Acceptance tests (run with `bin/omanoise-engine --render <mode> <sec> out.f32` and ffmpeg; write the numbers into `docs/ACCEPTANCE.md`)

For each mode, render **180 s** at volume 1.0 with adaptive off (write a temporary state file), then:
1. **Loudness**: `ffmpeg -af ebur128` integrated within ±1.5 LU of the mode target; **LRA 4–12 LU** for Focus/Relax (v1 was 1.4 = static), 3–10 for Sleep, 6–14 for Ocean.
2. **Spectral tilt** (band mean volumes via `lowpass/highpass` + `volumedetect`): energy in 200–1000 Hz band must exceed the <200 Hz band by no more than 4 dB and exceed the >4 kHz band by ≥ 18 dB (dark, pad-centred). The 1–4 kHz band must sit 8–16 dB below 200–1000 Hz.
3. **Harmonic dominance**: render once with `tonal 1.0` and once with `tonal 0.0`; the integrated loudness difference must be ≥ 8 LU in Focus/Relax/Sleep (pads carry the mix).
4. **Motion without salience**: per-second short-term loudness (ebur128 `S` values) standard deviation between 0.8 and 3 LU; no single 1 s step > 4 LU (no salient events).
5. **Events**: count melody triggers via an engine `--stats` stderr line at end of render; must fall in the §2.3 ranges for the mode at intensity 0.5.
6. **No artifacts**: no NaN/Inf (check `astats` Flat factor/peak), true-peak ≤ −1 dBTP, DC offset < 0.002, and a click detector: max |x[n]−x[n−1]| < 0.25 after the first 5 s.
7. **CPU**: live engine ≤ 3 % of one core while playing (`ps -o %cpu`), RSS ≤ 64 MB. Bank render finishes < 3 s (report time on stderr).
8. **Plugin plumbing unchanged**: `omarchy-shell omanoise state` works, `omanoise play/pause/mode/volume` round-trip, single engine process, `state` JSON parses in Service.qml.

## 6. Non-goals / constraints
- No recorded samples shipped; no network; no new runtime dependencies beyond libpipewire and libm (pthread is fine for the bank worker).
- Do not start playback on the user's speakers during implementation. Verify offline with `--render`; after building, `pkill -x omanoise-engine` so the service restarts the new binary paused.
- Keep `Service.qml`/`Panel.qml` behaviour otherwise identical; the bar icon change is a separate task.
- Keep `build.sh` a single gcc invocation (may list several .c files).
