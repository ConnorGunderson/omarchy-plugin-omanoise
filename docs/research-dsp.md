# Research track 3: DSP techniques for an Endel/Eno-like generative ambient engine

(Research agent report, 2026-09-16. Sources listed at the end.)

## 0. Diagnosis: why the current engine sounds like wind

Every layer in the present design shares the same fingerprint, and it is the fingerprint of a microphone in wind:

- **Noise through one slowly-swept 2-pole low-pass** *is* the textbook wind synthesis (it is literally what Farnell's wind patch and every Pd tutorial does: noise → `lop~`/`bp~` with a slow random cutoff). If that is the loudest, most continuous layer, the brain labels the whole thing "wind".
- **Band-passed noise swells** are the same thing with more Q: that is the "whistle through a gap" component of wind.
- **Pure sines + 2 harmonics** have no spectral motion. A static sine is perceptually "thin" because there is nothing for the ear to track: no beating, no chorusing, no filter movement, no stereo decorrelation.
- **A Schroeder 4-comb/2-allpass reverb with a long tail** on noise input creates coloured noise with comb resonances (metallic ringing), not a wash.

The fix is a change of *where the energy lives*: harmonic pad layers must carry the mix, noise becomes a quiet, textured background (or is turned into pitched material via resonators), and everything moves slowly and stereo-widely.

## 1. Lush pads

### Detuned unison ("supersaw")
Szabo's JP-8000 analysis: 7 saws, centre + 6 side oscillators at asymmetric offsets. For pads use **±5 to ±15 cents total spread**, side gains 0.5-0.7, and **high-pass the mix near the fundamental** to remove low-frequency beating rumble.

Per note: 5-7 saws, detune offsets in cents `{0, -11, -6, -2, +3, +7, +12}` × depth (0.6-1.0), amplitudes `{1, .55, .7, .8, .8, .7, .55}`. Odd oscillators pan left, even right, ±0.4 to ±0.8; centre to both. Random initial phases; never phase-reset.

### PolyBLEP saw (band-limited)
```
// phase t in [0,1), dt = f/fs
saw = 2*t - 1;
if (t < dt)         { x = t/dt;         saw -= (x+x - x*x - 1); }
else if (t > 1-dt)  { x = (t-1)/dt;     saw -= (x*x + x+x + 1); }
```
Triangle = leaky-integrated PolyBLEP square. Saw through a 4-pole LPF at 2-4× f0 is the classic string-machine/pad tone.

### Additive with per-partial drift
8-16 sine partials per voice, amp 1/n (saw-ish) or 1/n² (triangle-ish), **each partial's frequency has its own slow random walk of ±2-5 cents** and its own slow amplitude LFO (0.05-0.3 Hz). Internal chorusing without a chorus effect.

### 2-op FM glassy pads
c:m **1:1 or 1:2**, **index 0.3-1.5**, index modulated at 0.01-0.05 Hz ±30%. `y = sin(2π fc t + I·sin(2π fm t))`.

### Sub-oscillator
One sine an octave below chord root at -10 to -14 dB, HP 30 Hz, mono, below ~110 Hz.

### Filter: 4-pole warmth, Cytomic SVF
Replace the Chamberlin SVF (unstable above fs/6, noisy under modulation) with **two cascaded Cytomic trapezoidal SVFs** (Q ≈ 0.5-0.7 each) plus a `tanh` between them:
```
// Cytomic SVF, per sample (g = tan(pi*fc/fs), k = 1/Q)
a1 = 1/(1+g*(g+k)); a2 = g*a1; a3 = g*a2;
v3 = in - ic2eq;  v1 = a1*ic1eq + a2*v3;  v2 = ic2eq + a2*ic1eq + a3*v3;
ic1eq = 2*v1 - ic1eq;  ic2eq = 2*v2 - ic2eq;   // lp = v2, bp = v1, hp = in - k*v1 - v2
```
**Keytracking**: `fc = base_fc * (f0/f_ref)^0.5..1.0`. **Slow filter movement**: cutoff = keytracked base × 2^(±0.5 oct · drift), drift from a 0.02-0.1 Hz smoothed random. This is the single most important "aliveness" ingredient.

### Saturation
`tanh` (or `x - x³/3`) **after** the filter, **before** the reverb, driven at 0.3-0.6 FS. Never saturate the reverb output.

## 2. Movement

### Solina-style ensemble chorus
Three delay lines modulated by two 3-phase LFOs 120° apart: slow **0.6-1.3 Hz** (±1-2 ms) and fast **5.5-6.5 Hz** (±0.1-0.3 ms), centre delay 5-10 ms. Measured Solina: 1.21 Hz / 5.82 Hz.
```
for k in 0..2:
  d_k = 7ms + 1.5ms*sin(2π·0.9·t + k·2π/3) + 0.2ms*sin(2π·5.9·t + k·2π/3)
  y_k = delay.read_interp(d_k)
L = dry + y0 + 0.5*y1;  R = dry + y2 + 0.5*y1
```

### Drift generators
1. Sum of 3 sines at incommensurable rates (0.031, 0.047, 0.071 Hz).
2. **Smoothed random / value noise** (Buchla 266 style): interpolate between held randoms with a cosine, T = 8-40 s per generator. **Nothing in the engine should share an LFO.**
```
if (t >= t_next) { a = b; b = rand_uniform(-1,1); t0 = t; t_next = t + T*(0.7+0.6*rand()); }
u = (t - t0)/(t_next - t0);  s = 0.5 - 0.5*cos(π*u);  drift = a + (b-a)*s;
```
Stepped randoms only for musical decisions, never audio parameters.

## 3. Space

### Why Schroeder rings
Regular mode pattern, low echo density, insufficient diffusion. Cures: more diffusion (series allpasses before the tank), damping inside the loop, **modulated long delays** (not the short input allpasses).

### Dattorro plate (recommended)
At 29.8 kHz reference: pre-delay; input LPF (bw ≈ 0.9995); **4 series input allpasses** (142, 107, 379, 277 samples; coeffs 0.75, 0.75, 0.625, 0.625); figure-8 **tank**, each half = modulated allpass (672 / 908, coeff 0.7, excursion ±8-16 samples @ ~1 Hz) → delay (4453 / 4217) → damping LPF → fixed allpass (1800 / 2656, coeff 0.5) → delay (3720 / 3163), cross-fed with decay 0.5-0.9999. Scale lengths ×1.61 for 48 kHz. ~7 output taps per channel (paper's table). Ambient settings: **decay 0.85-0.97, damping LPF 3-6 kHz, pre-delay 20-80 ms, input bandwidth 0.7-0.9**. Cost ≈ 60-80 flops/sample. Refs: el-visio/dattorro-verb, Valley Audio Plateau.

### FDN with Householder mixing (alternative)
N=8 mutually prime lengths (48 kHz: 1499, 1889, 2381, 2887, 3457, 3881, 4327, 4799), `A = I - (2/N)·1·1ᵀ` (`s = sum(x); y_i = x_i - (2/N)·s`), per-line one-pole damping, `g_i = 10^(-3·L_i/(fs·T60))`, ±0.5-1 ms random modulation 0.1-0.5 Hz per line, 2 input allpasses.

### Shimmer (the ambient trick)
**Feedback loop containing a +12 st pitch shifter and a long reverb**, feedback 0.4-0.7, HF roll-off in the loop. Cheap shifter: **2 read heads on a 50-100 ms buffer**, sweep rate (2^(12/12) - 1) = +1 octave, Hann-crossfaded 180° apart. Loop filters HPF 150 Hz / LPF 4-6 kHz. Optionally mix +7 and +12 at -6 dB.
```
s = shifter(rev_out_prev); s = lpf(hpf(s)); rev_in = dry + fb*s; rev_out = plate(rev_in);
```

### "Frozen" reverb from noise
Only works if the noise is **pitched first**: comb/KS or 3-4 band-passes at chord-tone frequencies (Q 20-80), gated in bursts 50-500 ms, then into a decay→1 reverb. Raw noise into reverb = louder wind.

## 4. Textures that do not sound like wind

### Karplus-Strong noise-burst plucks (rain, water, glass)
Delay length `L = fs/f0`, 2-point average or one-pole in loop, excited by a **2-10 ms low-passed noise burst**. Loop gain 0.99-0.999 for 1-6 s; allpass fractional delay for tuning; decay stretch S=0.5 plucks, 0.2-0.3 drips. 5-40 events/min at pentatonic pitches octaves 4-6, random pan/amplitude = "rain on glass", inherently musical.

### Formant filter banks
3-4 SVF band-passes Q 8-20: "ah" ≈ 730/1090/2440 Hz, "oh" ≈ 570/840/2410, "oo" ≈ 300/870/2240; drift between vowels over 20-60 s. On a pad = choir; on noise at high Q = breathy whispered chord.

### Granular cloud from a synthesized buffer
Render a 1-2 s pad/bell note into a buffer; play 8-32 overlapping Hann grains of 50-300 ms, random start/pan, pitch 0 / +12 / +7; density 20-60 grains/s. Unmistakably not wind.

### Farnell-style wind vs ocean vs rain
- **Wind** = noise → *narrow* band-pass wandering 300-600 Hz + LP bed, gusts 0.1-0.5 Hz. **Avoid exactly that.**
- **Ocean** = noise → LP **30-400 Hz** rumble with **5-12 s asymmetric envelope** (rise 1-2 s, fall 4-8 s) + "foam" layer HP 2-6 kHz (-3 dB/oct tilt) lagging ~1 s and decaying faster + optional bubble chirps 400-2000 Hz. 2-3 independent engines L/C/R at periods 7 / 9.5 / 12 s. The rumble→foam *sequence* is what reads as waves.
- **Rain** = Poisson impulses 100-2000/s through ~400 Hz band-pass + HP sizzle; individual drops = KS plucks.
- All noise layers together **12-18 dB below the pad**, LP 1-2 kHz.

## 5. Melodic and harmonic logic

### Eno asynchronous loops
Music for Airports 2/1: seven tape loops of incommensurable lengths (≈23½, 25⅞, 29¹⁵⁄₁₆ s). Give each of 5-8 pad voices its own period from a mutually non-integer-ratio set (17.3, 21.9, 26.1, 31.7, 37.4 s), attack 3-6 s, release 6-15 s.

### Chord-pool drift
Pool of 3-5 related chords, move every 40-120 s via a Markov table biased toward ≥2 common tones: e.g. `Cmaj9 → Am9 → Fmaj7(add9) → G6/9 → Dm11`. Voice leading: nearest chord tone ≤2 st away, else stay.

### Interval rules
- Major pentatonic (Endel is publicly pentatonic-based) or Ionian/Lydian/Dorian with 4th/7th de-weighted.
- Forbid **simultaneous minor 2nds and tritones** between sustaining voices; allow major 2nds (add9 lushness).
- **Register**: bass 55-110 Hz (root/5th only); pads 130-520 Hz spread ≥ a 4th apart; bells/plucks 500-2000 Hz. Overtone-series spacing = "big".
- **Density**: relax 4-10 events/min, focus 12-30/min (mostly plucks), sleep 0-4/min pads only.
- Just-intonation partials/bells (5/4, 3/2, 7/4) relative to root: beats vanish, pad "locks".

### Shepard/Risset drift
6-8 sines an octave apart gliding 1 oct per 60-120 s, Gaussian spectral envelope centred ~400 Hz (σ ≈ 1.5 oct), -20 dB under the pad.

## 6. Bells and plucks

### Risset bell (11 partials)
| ratio | 0.56 | 0.56 (+1 Hz) | 0.92 | 0.92 (+1.7 Hz) | 1.19 | 1.70 | 2.00 | 2.74 | 3.00 | 3.76 | 4.07 |
|---|---|---|---|---|---|---|---|---|---|---|---|
| amp | 1 | .67 | 1 | 1.8 | 2.67 | 1.67 | 1.46 | 1.33 | 1.33 | 1 | 1.33 |
| dur × | 1 | .9 | .65 | .55 | .325 | .35 | .25 | .2 | .15 | .1 | .075 |
Detuned pairs give the slow beating; total duration 4-20 s.

### FM bell
c:m **1:1.4** or 1:3.5, **index 8-12 decaying exponentially over ~1 s to <1**, amplitude decaying 3-10 s.

### Plucks
KS S=0.5, loop gain 0.995-0.999, excitation LPF 2-6 kHz. **A few ms of noise in the attack** is what makes it not sound like "a sine switching on".

### Keeping them soft
LP 4-6 kHz, HP 200 Hz, velocity 0.3-0.7 never 1.0, **sidechain-duck the pad 2-3 dB for 300 ms per event**, send bells to shimmer at higher level than pad.

## 7. Loudness and mix

- Target **-23 LUFS** background, **-26 to -30 LUFS** sleep. Implement BS.1770 K-weighting (2 biquads) + 400 ms blocks; use to auto-trim.
- Layer balance vs pad 0 dB: sub -10, chorus wet 0, bells -8 to -14 peak, plucks -10 to -16, textures/noise -15 to -24, shimmer return -6 to -12, plate return -3 to -8.
- Master **HP 30 Hz** (2nd-order), **LP 8-10 kHz** gentle, broad -2 dB dip 2.5-4 kHz.
- Bus compression 1.5:1, 1-2 dB GR, attack 30-50 ms, release 500-1500 ms.
- Width via **mid/side** (S × 1.2-1.5, side HP ~200 Hz), not Haas on the master.
- Soft limiter: look-ahead 5 ms, -1 dBTP, tanh knee.

## 8. Implementation notes
- Linear interp fine for chorus/reverb; **allpass interpolation** for KS tuning (`y[n] = c·(x[n] - y[n-1]) + x[n-1]`, `c = (1-d)/(1+d)`). Power-of-two buffers with `& mask`.
- **Denormals**: set FTZ/DAZ on the audio thread (`_MM_SET_FLUSH_ZERO_MODE`, `_MM_SET_DENORMALS_ZERO_MODE`).
- Control rate every 64 samples, linearly ramp gains and SVF `g` across the block.
- Envelopes: `env += (target - env)·k`, `k = 1 - exp(-1/(τ·fs))`; Hann table for long raised-cosine attacks.
- Budget: whole design ≈ 800-1000 flops/frame ≈ 0.2-0.5% of one core. **Not CPU-limited; design-limited.**

## Recommended architecture: 5-layer Endel-like engine

**Brain (control rate)**: chord pool of 4 chords, Markov transition every 60-90 s, common-tone bias; 6 independent value-noise drift generators (T 8-40 s); mode presets set densities, cutoff range, texture level.

**Layer 1 — PAD (0 dB reference, the star)**: 4 voices (bass + 3 upper), each with own Eno loop period (17.3, 21.9, 26.1, 31.7 s), attack 4 s / release 10 s raised-cosine, nearest-chord-tone voice leading, no m2/tritone against sounding voices; uppers 130-520 Hz. Each voice: 7 PolyBLEP saws, ±9 cents × (0.8 + 0.2·drift), odd/even panned ±0.6 → 2× cascaded Cytomic SVF LP, `fc = 2.5·f0 · 2^(0.5·drift_v)`, Q 0.6 → `tanh(0.5·x)`. Bass: single saw + sine sub, LP 1.5·f0, mono. Pad bus → Solina chorus → HP 60 Hz.

**Layer 2 — SHIMMER WASH (-9 dB return)**: send pad -6 dB, bells 0 dB. Loop: 2-head Hann octave-up shifter → HPF 150 / LPF 5 kHz → Dattorro plate (decay 0.93, damping 4 kHz, pre-delay 40 ms, tank mod ±12 samples @ 0.7 Hz) → feedback 0.55. This replaces "noise into reverb" as the wash source.

**Layer 3 — BELLS/PLUCKS (-10 dB peak)**: Risset bells just-tuned to chord, 2-6/min relax; KS plucks (S=0.5, gain 0.997, 3 ms burst LP 4 kHz) 10-25/min focus; LP 5 kHz, random pan, velocity 0.3-0.7. Duck pad 2.5 dB / 300 ms per event.

**Layer 4 — GRANULAR CLOUD (-14 dB)**: every 3-5 min render a 2 s pad chord to a buffer; 24 Hann grains 80-250 ms, density 30/s, 20% +12 st, 10% +7 st; crossfade buffers on chord change; 50% to plate.

**Layer 5 — NATURE BED (-20 dB, LP 1.5 kHz)**: ocean ×2 (L/R, periods 8.5 / 11.7 s): noise → SVF LP swept 40-350 Hz by asymmetric envelope (rise 1.5 s, fall 6 s) + foam HP 2.5 kHz -8 dB lagging 0.8 s decaying 2 s. Optional rain: Poisson KS drips 20/min. **No narrow mid band-pass sweeps; no free-running pink noise above -24 dB.**

**Master**: HP 30 Hz → M/S width 1.3 (side HP 200) → comp 1.5:1 40/800 ms → LP 9 kHz → soft limiter -1 dBTP → K-weighted LUFS meter driving slow (τ 30 s) trim to -24 LUFS relax / -28 sleep.

## Sources
- Dattorro, Effect Design Part 1: https://ccrma.stanford.edu/~dattorro/EffectDesignPart1.pdf
- Freeverb (PASP): https://ccrma.stanford.edu/~jos/pasp/Freeverb.html
- FDN / Householder (PASP): https://ccrma.stanford.edu/~jos/pasp/FDN_Reverberation.html , https://ccrma.stanford.edu/~jos/pasp/Householder_Feedback_Matrix.html
- Karplus-Strong / EKS (PASP): https://ccrma.stanford.edu/~jos/pasp/Karplus_Strong_Algorithm.html , https://ccrma.stanford.edu/~jos/pasp/Extended_Karplus_Strong_Algorithm.html
- Valhalla DSP shimmer: https://valhalladsp.com/2010/05/11/enolanois-shimmer-sound-how-it-is-made/ ; notes https://www.valhalladsp.com/shimmer/ValhallaShimmerNotes.pdf ; modulation https://valhalladsp.com/2009/07/30/modulation-in-reverbs-reality-and-unreality/ ; metallic artifacts https://valhalladsp.com/2011/01/21/reverbs-diffusion-allpass-delays-and-metallic-artifacts/
- Cytomic trapezoidal SVF: https://www.cytomic.com/files/dsp/SvfLinearTrapOptimised.pdf
- PolyBLEP: https://pbat.ch/sndkit/blep/
- Szabo supersaw: https://www.adamszabo.com/internet/adam_szabo_how_to_emulate_the_super_saw.pdf
- Solina triple chorus: http://jhaible.com/legacy/triple_chorus/triple_chorus.html , https://github.com/jpcima/ensemble-chorus
- Huovilainen Moog ladder (2004)
- Buchla 266: https://modularsynthesis.com/roman/buchla_266/266sou.htm
- Risset bell: https://msp.ucsd.edu/techniques/v0.11/book-html/node71.html
- FM bell: https://docs.cycling74.com/learn/articles/06_synthesischapter05/
- Shepard-Risset: https://csoundjournal.com/issue21/interp_visual_phenom.html
- Music for Airports loops: https://reverbmachine.com/blog/deconstructing-brian-eno-music-for-airports/
- Endel technology page: https://endel.io/technology
- Farnell, Designing Sound (MIT Press); derived notes https://mct-master.github.io/sound-programming/2020/02/11/Making-Noises.html
- EBU R128: https://tech.ebu.ch/publications/r128
- Haas vs M/S: https://www.soundonsound.com/sound-advice/q-can-haas-delays-be-mono-compatible
- Denormals: https://www.earlevel.com/main/2019/04/19/floating-point-denormals/
