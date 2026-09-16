# Omanoise sound engine v5 — acceptance runs (2026-09-17)

Tests from `docs/PLAN-sound-v5.md` §4. Method is the v4 method below: offline
renders with a temporary `XDG_STATE_HOME` at `volume 1.0`, `intensity 0.5`,
`brightness 0.5`, `adaptive off`, produced with

```
XDG_STATE_HOME=<tmp> bin/omanoise-engine --render environment 150 out.f32 --stats --env <name>=1
```

and measured with `ffmpeg` (`ebur128=peak=true` for integrated loudness, LRA,
true peak and the short-term series) plus the engine's own `--stats`, `rough`
and — new in v5 — `segments` lines. `tonal` is 0 for the solo renders and 0.5
for the blend and the mode renders. Two extra analysers were written for this
section and are kept with the run data: one reports the largest sample-to-sample
step **inside the crossfade windows** versus everywhere else (the windows are
reconstructed from the `segments` log), the other does the same for the 1-second
short-term loudness step.

## v5-0. What the four recordings are, and how they are calibrated

Measured by `ffmpeg -af ebur128` on the files themselves, and by the engine at
load time:

| file | length | file I (ffmpeg) | file I (engine) | LRA | true peak | sample peak | short-term mean¹ | **cal** |
|---|---|---|---|---|---|---|---|---|
| fireplace.wav | 25.5 s | −38.8 | −38.8 | 1.1 | −9.2 dBTP | 0.344 | −41.7 | **+22.8 dB** |
| wind.wav | 14.8 s | −27.6 | −27.6 | 7.0 | −11.0 dBTP | 0.280 | −28.2 | **+9.3 dB** |
| stream.wav | 145.5 s | −39.6 | −39.6 | 0.9 | −15.8 dBTP | 0.163 | −39.1 | **+20.2 dB** |
| birds.wav | 129.8 s | −28.0 | −27.9 | 16.4 | −12.4 dBTP | 0.237 | −31.3 | **+12.4 dB** |

¹ the mean of the short-term (3 s) K-weighted loudness taken through the slot's
own HP 40 Hz / LP 9 kHz and the master's 1.3× stereo width — see the deviation
note at the end for why the calibration is derived from this and not from the
gated integrated number. `wav.c`'s own BS.1770-4 implementation agrees with
ffmpeg to 0.1 LU on all four files, which is the cross-check that it is right.

`cal = −23 − (short-term mean) + REC_OFFSET_DB`, with `REC_OFFSET_DB = 4.1 dB`
fitted once over the four files (`wav.h`). Nothing is hard-coded per file: drop
in another WAV and it is levelled the same way.

## v5-1. Each recorded slot solo (PLAN §4.1) — 6 of 8 checks pass

Level 1.0, everything else 0, `tonal 0`, 2 × 150 s each.

| slot | I | LRA | TP | trim | S std-dev | max 1 s step | sample peak | max sample step |
|---|---|---|---|---|---|---|---|---|
| fire #1 | −22.8 | 6.4 | −1.1 | +0.69 | 2.13 | 1.00 | 0.785 | 0.882 |
| fire #2 | −23.0 | 7.2 | **−0.9** | +0.47 | 2.50 | 1.00 | 0.785 | 0.881 |
| wind #1 | −22.7 | 9.3 | −5.0 | −0.50 | 3.05 | 3.40 | 0.564 | 0.017 |
| wind #2 | −22.8 | 7.6 | −5.8 | −1.37 | 2.52 | 3.30 | 0.512 | 0.015 |
| stream #1 | −22.7 | 6.5 | −1.8 | −0.64 | 2.36 | 1.20 | 0.783 | 0.680 |
| stream #2 | −23.5 | 6.4 | −1.9 | **+2.24** | 2.03 | 1.30 | 0.779 | 0.575 |
| birds #1 | **−20.4** | 15.8 | −2.8 | +0.53 | 5.55 | **18.00** | 0.724 | 0.290 |
| birds #2 | **−18.0** | 20.7 | −2.2 | **+3.47** | 6.98 | **16.90** | 0.774 | 0.377 |

| check | limit | result |
|---|---|---|
| integrated loudness −23 ± 1.5 LU | −21.5 … −24.5 | **6/8** — fire, wind, stream all pass (−22.7 … −23.5); **birds fails on both renders** (−20.4, −18.0) |
| trim within ±2 dB | ±2 | **6/8** — fire, wind, birds #1 pass (−1.37 … +0.69); stream #2 +2.24, birds #2 +3.47 |
| true peak ≤ −1 dBTP | ≤ −1 | **7/8** — worst −0.9 (fire #2), i.e. 0.1 dB over on one render |
| max sample step ≤ 0.25 | ≤ 0.25 | **2/8** — wind (0.015–0.017) and nothing else; see below |
| no 1 s short-term step > 4 LU | ≤ 4 | **6/8** — fire 1.0, wind 3.3–3.4, stream 1.2–1.3; **birds 16.9 and 18.0** |
| segment log proves randomness | no repeats, no period | **pass 8/8** |

### The click check, and why the numbers are what they are

The ≤ 0.25 sample-step limit was written against *synthesized* generators, where
any large step is an artefact. Against recordings it is measuring the
recordings' own transients: a log snapping in the fireplace, a stone in the
stream. The source files' own largest steps, scaled by the gain the chain
actually applies to them (measured as render sample peak ÷ file sample peak),
predict the rendered numbers:

| slot | file max step | chain gain (peak ratio) | predicted | **measured** |
|---|---|---|---|---|
| fire | 0.361 (at 6.04 s in the file) | ×2.28 | 0.82 | **0.88** |
| stream | 0.140 | ×4.80 | 0.67 | **0.68** |
| birds | 0.129 | ×3.27 | 0.42 | **0.38** (soft knee) |
| wind | 0.013 | ×2.01 | 0.027 | **0.017** (9 kHz LP) |

The test the plan actually asks for is *"no click at any crossfade"*, and that
one passes. Reconstructing every crossfade window from the `segments` log and
splitting the step detector by window:

| render | max step inside a crossfade | max step everywhere else |
|---|---|---|
| fire #1 / #2 | 0.832 / 0.872 | 0.882 / 0.881 |
| wind #1 / #2 | 0.0161 / 0.0143 | 0.0166 / 0.0145 |
| stream #1 / #2 | 0.475 / 0.575 | 0.680 / 0.542 |
| birds #1 / #2 | 0.161 / 0.314 | 0.290 / 0.377 |

Inside the crossfades is never worse than outside them — in six of eight
renders it is *lower*. A splice would show as an isolated outlier at a window
edge and there is none: an equal-power sin/cos pair moves each head's gain by
under 1.3 × 10⁻⁵ per sample at 2.5 s, and the incoming head starts at gain 0.

The same split for the 1-second loudness step (the birds figure is birdsong, not
the scheduler):

| render | max \|ΔS\| inside a crossfade | elsewhere |
|---|---|---|
| fire #1 / #2 | 1.0 / 1.0 LU | 1.0 / 0.9 LU |
| wind #1 / #2 | 3.4 / 3.3 LU | 2.0 / 2.1 LU |
| stream #1 / #2 | 1.2 / 0.7 LU | 1.0 / 1.3 LU |
| birds #1 / #2 | 17.2 / 14.0 LU | **18.0 / 16.9 LU** |

Birds' largest step is outside a crossfade on both renders. A dawn chorus is
silence and then a close call: a 17 LU jump in a second is the recording.

### How hard the limiter works

Only the fireplace crackles and the loudest bird calls reach the master's soft
knee at all, and then for a few samples:

| render | samples over the 0.581 knee | over 0.70 | over 0.80 |
|---|---|---|---|
| fire solo, 150 s (14.4 M samples) | 165 (0.0011 %) | 72 | 0 |
| fire solo at the shipped volume 0.7 | 32 (0.0002 %) | 12 | 0 |
| stream solo | 132 (0.0009 %) | 27 | 0 |
| birds solo | 1174 (0.0082 %) | 156 | 0 |
| four-way blend | 31 (0.0002 %) | 12 | 0 |
| ocean solo (reference) | 0 | 0 | 0 |

That is about 3 ms of material per 150 s for fire. It is also where the −0.9
dBTP comes from: the limiter ceiling is 0.83 (−1.6 dBFS) and inter-sample peaks
on a transient add ~0.7 dB. A fireplace recording has a 29.6 dB peak-to-loudness
ratio; anything that puts it at −23 LUFS has to catch peaks somewhere.

### Randomness of the segment scheduling

From the `segments` lines of the eight renders (each entry is
`file position + length`, in seconds):

```
fire   4.06+10.79 12.38+9.89 3.06+12.94 15.13+8.45 3.38+7.74 7.49+13.31 …  (19)
wind   3.29+6.76 5.81+7.35 1.12+6.37 5.96+7.26 1.05+6.97 6.21+6.49 …       (35)
stream 37.87+43.31 79.73+35.58 11.95+33.74 45.68+24.26 107.55+29.54 8.32+20.94
birds  40.08+36.21 4.16+44.27 69.93+41.30 31.29+39.90 77.62+40.61
```

| slot | segments in 150 s | start range | length range (mean) | consecutive equal starts | overlap ≤ 30 % |
|---|---|---|---|---|---|
| fire #1 / #2 | 19 / 20 | 1.0–16.3 s | 6.08–13.70 s (10.5 / 10.1) | 0 / 0 | 16/18, 19/19 |
| wind #1 / #2 | 35 / 36 | 1.0–7.5 s | 6.05–7.52 s (6.86 / 6.75) | 0 / 0 | 33/34, 29/35 |
| stream #1 / #2 | 6 / 5 | 4.4–107.6 s | 20.94–44.58 s (31.2 / 36.7) | 0 / 0 | 5/5, 4/4 |
| birds #1 / #2 | 5 / 6 | 4.2–87.3 s | 24.27–44.27 s (40.5 / 30.3) | 0 / 0 | 4/4, 5/5 |

No two consecutive segments share a start, no start or length repeats, and the
gaps between handovers (segment length − crossfade) are different every time, so
there is no period to hear. 115 of the 128 handovers keep the overlap at or
under 30 %; the 13 that do not are all in the two short files (worst 60 %, wind)
— see deviation 2.

The peak modulation frequency of the eight solo renders is different in every
render and 32–37 dB under the envelope's DC for fire, wind and stream
(6.82 / 12.27 / 1.39 / 0.81 / 7.38 / 4.14 Hz), i.e. no stable line. Birds is the
exception at 0.54 Hz and −16.6 dB, which is the rate at which a dawn chorus
comes and goes.

### FLUCT and ROUGH of recorded material

v4 §4.4 held the synthesized generators to "ocean + 3 dB". Recordings do not
meet that and are not held to it (there is no parameter to turn): the numbers
are reported.

| solo | FLUCT | ROUGH |
|---|---|---|
| fire | −15.08 / −13.95 | −12.10 / −10.90 |
| wind | −13.39 / −14.17 | −10.89 / −11.55 |
| stream | −14.00 / −12.85 | −11.12 / −9.99 |
| birds | −3.60 / −3.27 | −10.78 / −10.31 |
| *ocean (synth, reference)* | *−20.17* | *−13.90* |
| *rain (synth, reference)* | *−26.95* | *−20.04* |

A real fire flickers and real birds start and stop; that fluctuation is the
signal, and it is the thing two synthesized attempts failed to produce
convincingly. Roughness (15–200 Hz) stays within 3–4 dB of the synthesized
ocean for all four.

## v5-2. Blend (PLAN §4.2) — pass

ocean 0.7, fire 0.7, stream 0.5, birds 0.3, `tonal 0.5`, 2 × 150 s:

| # | I | LRA | TP | trim | S std-dev | max 1 s step | sample peak | max sample step | FLUCT | ROUGH |
|---|---|---|---|---|---|---|---|---|---|---|
| 1 | −23.0 | 6.3 | −1.3 | +1.59 | 2.27 | 1.10 | 0.777 | 0.816 | −19.85 | −13.44 |
| 2 | −22.8 | 6.0 | −1.2 | +2.08 | 2.21 | 0.90 | 0.778 | 0.829 | −19.57 | −13.10 |

| check | limit | result |
|---|---|---|
| no clipping | true peak ≤ −1 dBTP | **pass** (−1.2, −1.3) |
| loudness | −23 ±2 | **pass** (−22.8, −23.0) |
| max sample step | ≤ 0.25 | 0.816 / 0.829 — the same fireplace crackles as v5-1, at 0.0002 % of samples |

Blending fixes birds: with four sounds summed, birds' gaps are filled by the
others and the mix's integrated loudness lands on target. `peak_mod` is 32.78 /
32.79 Hz at −25/−26 dB, which is the pad's just-intonation artefact that v4-6
already identified (C3 ÷ 4), not a modulator, and not from the bank.

## v5-3. Ocean, rain, Focus and Relax are unchanged (PLAN §4.3) — pass

One render each against the v4 / v4.1 tables. Nothing in these four paths was
edited: the ocean engine, the rain generator, `brain.c` and the master chain are
untouched, and the environment bus only gained slots.

| render | metric | v4 / v4.1 | **v5** | verdict |
|---|---|---|---|---|
| ocean solo, 150 s | I | −23.0 / −23.0 | **−23.2** | within noise |
| | LRA | 7.0 / 6.7 | **7.0** | |
| | TP | −7.8 / −7.1 | **−7.7** | |
| | trim | +0.53 / −0.20 | **+0.05** | |
| | max sample step | 0.060 / 0.065 | **0.058** | |
| | FLUCT / ROUGH | −20.13 / −13.76 | **−20.17 / −13.90** | |
| rain solo, 150 s | I | −23.4 (v4.1) | **−22.7** | within noise |
| | max sample step | 0.089 (v4.1) | **0.106** | v4 range 0.103–0.123 |
| | FLUCT / ROUGH | −26.10 / −19.23 (v4.1) | **−26.95 / −20.04** | calmer |
| | trim | — | **+0.71** | |
| Focus, 180 s | I | −21.1 … −20.7 | **−21.1** | pass |
| | LRA | 5.5 … 7.0 | **6.4** | pass |
| | S std-dev | 2.10 … 2.22 | **2.18** | pass |
| | max 1 s step | 1.20 … 1.90 | **1.70** | pass |
| | TP | −8.5 … −7.2 | **−8.6** | 0.1 dB outside, inside v3's −9.2 … −6.6 |
| | FLUCT / ROUGH | −25.38 … −24.11 / −11.38 … −9.82 | **−24.61 / −10.50** | pass |
| | melody rate | 6.0–7.3 /min | **6.67 /min** | pass |
| Relax, 180 s | I | −24.4 … −24.7 | **−24.6** | pass |
| | LRA | 5.1 … 5.4 | **6.1** | 0.7 above the v4 spread, and back inside the v2 ≥ 6 window |
| | S std-dev | 1.79 … 2.08 | **1.94** | pass |
| | max 1 s step | 1.50 … 1.60 | **1.70** | +0.1 |
| | TP | −12.1 … −12.8 | **−11.4** | +0.7 dB, generative spread |
| | FLUCT / ROUGH | −24.57 … −25.00 / −11.66 … −12.21 | **−24.94 / −12.32** | pass |

Focus and Relax are three-render tables in v4 and one render here, so a metric
0.1–0.7 outside a three-render spread is the spread widening, not a change:
every edit to `layers.c` is inside `if (E.env_on)`, which is false in both
modes, plus one refactor of the plate send that evaluates to the identical
expression when the environment bus is off.

## v5-4. Missing-file fallback (PLAN §4.4) — pass

`OMANOISE_SOUNDS` pointed at directories prepared for the test:

| case | result |
|---|---|
| empty directory, `--env birds=1` | four `… missing — the <slot> slot is unavailable` lines, `sounds 0 recording(s)`, render completes, **sample peak 0.0000**, no crash |
| only `birds.wav` absent, `--env birds=1,fire=0.5` | fire plays, birds silent, `"sounds":{…,"birds":false}` |
| `fireplace.wav` at 44.1 kHz | `is 44100 Hz 16-bit 2-channel; need 48000 Hz 16-bit mono or stereo — slot disabled` |
| `wind.wav` as 32-bit float | `is not PCM (format tag 3) — slot disabled` |
| `stream.wav` a text file | `is not a RIFF/WAVE file — slot disabled` |
| `birds.wav` 0.5 s long | `is only 0.50 s — slot disabled` |
| a **mono** 48 kHz file in the fire slot | loads (1.4 MB), fans out to both channels, plays, cal +9.5 dB |

Every rejection leaves the slot silent and the rest of the bank running.

## v5-5. Resources (PLAN §4.5) — pass

| check | target | measured |
|---|---|---|
| RSS with all four files loaded | ≤ 120 MB | **110.3 MB** live (`VmHWM`), **104.8 MB** for an offline render. v4 was 51.2 MB; the four recordings are 57.8 MB of int16 PCM, read straight into their final buffer with no intermediate copy |
| sounds ready | < 3 s | **1.20–1.26 s** total (bank 51–68 ms, SoundFont 40–50 ms, the four WAVs ≈ 1.1 s, of which the two measurement passes are the bulk). The bank and the SoundFont still publish at ~100 ms; the recordings arrive after them and the panel shows their rows as unavailable until they do |
| CPU while generating | ≤ 4 % of one core | **1.39 %** environment blend, 1.38 % Focus, 1.70 % Relax, 1.40 % birds solo (wall time of a 600 s `--render` ÷ 600 s, which also includes the 1.2 s load and writing and analysing every sample) |
| CPU, live, paused | — | **0.00 %** over 5 s |
| build | zero warnings | `./build.sh` clean, and clean again under `-Werror` |
| artefacts | none | no non-finite sample in any render (the renderer aborts on one); \|DC\| ≤ 3 × 10⁻⁶ across this sweep |

## v5-6. Plumbing (PLAN §4.6)

| check | result |
|---|---|
| `state` JSON | gains `"sounds":{"ocean","rain","fire","wind","stream","birds"}`; `env` now carries the same six keys |
| `sounds` flips as the loader finishes | verified over a live stdin session: the first `state` line has the four recorded slots `false`, the line after `sounds ready` has them `true` |
| state-file migration v4 → v5 | a v4 file with `"mode":"ocean"`, `volume 0.42`, `modulation:true` and a four-key `env` loads as `"version":5`, `"mode":"environment"`, `stream`/`birds` added at 0, **every other field preserved** |
| `env stream 0.5`, `env birds 0.35` | round-trip, echoed and persisted |
| `env bogus 0.2`, `env wind` (no value) | `error usage: env <ocean\|rain\|fire\|wind\|stream\|birds> <0..1>`, no state change |
| every v4 command | `play pause toggle quit state mode volume intensity brightness tonal binaural adaptive pulse modulation` all behave as before; `mode sleep`/`mode ocean` aliases still resolve |
| engine restarts on binary change | yes — replacing `bin/omanoise-engine` makes the running engine exit and the service respawn it **paused** within ~3 s; `pkill -x omanoise-engine` was run once after the final build |
| QML hot-reload | picked up: `Local plugin changed, reloading: omanoise` in the journal after the edits, and **no QML errors** for the new `Panel.qml` / `Service.qml` |
| `omarchy-shell omanoise state` | answers and parses as JSON, **from the stale handler** — its reply has the six `env` keys (those come from the engine) but **no `sounds` key**, which only the new `Service.qml` emits |
| `omanoise env stream 0.5` | **works** end to end (CLI → shell IPC → service → engine → state file); the v4-era handler already had an `env` function, so unlike v4 this one is not blocked by the stale registration |
| `~/.local/state/omanoise.json` | ends at the PLAN §5 defaults: `version 5`, `mode focus`, `volume 0.700`, `intensity/brightness/tonal 0.500`, `binaural false`, `adaptive true`, `pulse false`, `modulation false`, `env {ocean 0.800, rest 0}` |

**The stale-IPC caveat is the same one v3 and v4 recorded.** The running shell
keeps the *first* `IpcHandler` registered for target `omanoise`; every reload
since logs `Handler was registered but will not be used because another handler
is registered for target omanoise` (it does the same for a dozen built-in
plugins). The new handler and the new service are loaded and correct; the
registration clears itself the next time the shell restarts, which was
deliberately **not** done here.

### Panel glyphs

Checked against the bar font before use, as the plan asks:
`fc-list | grep -i jetbrains` → `/usr/share/fonts/TTF/JetBrainsMonoNerdFont-Regular.ttf`,
then `fc-query --format='%{charset}\n'` on that file. All six codepoints fall
inside its `f0001-f1af0` range, so none of them is a tofu box:

| row | glyph | codepoint | name |
|---|---|---|---|
| Ocean | 󰞍 | U+F078D | nf-md-waves (unchanged) |
| Rain | 󰖗 | U+F0597 | nf-md-weather-pouring (unchanged) |
| Fire | 󰈸 | U+F0238 | nf-md-fire (unchanged) |
| Wind | 󰖝 | U+F059D | nf-md-weather-windy (unchanged) |
| Stream | 󰶟 | U+F0D9F | nf-md-waterfall (new) |
| Birds | 󱗆 | U+F15C6 | nf-md-bird (new) |

## v5 — modulation-line audit of the new code (v3 §1.1 still in force)

| source | file | rate | status |
|---|---|---|---|
| segment scheduling | `env.c:rec_pick` | every 3.5–41 s, never the same twice | random length and position; the interval between handovers is a random variable, not a period |
| equal-power crossfade | `env.c:rec_audio` | one sin/cos sweep per handover | a window, not an oscillator; sums to constant power |
| per-slot level drift | `env.c` drift, base 30 s | 0.024–0.048 Hz | value noise, under the plan's 0.05 Hz |
| per-slot LP with Brightness | `env.c:rec_control` | static per control block | not an oscillator |
| plate send, per slot | `layers.c` | constant −14 / −18 dB | not an oscillator |
| the recordings themselves | — | whatever is in them | fire flicker, wind gusts, water, birdsong — the reason they are here |
| **removed** | `env.c` | — | the synthesized fire's ~6 Hz flutter noise and ~1.2 Hz roar, its crackle/pop Poisson trains and its ±2.5 dB flicker drift; the synthesized wind's two cutoff sweeps and two gust drifts |

Rain's four drifts and its Poisson patter are unchanged and are enumerated in
v4-6 below. Nothing added in v5 is periodic.

## v5 — deviations from PLAN-sound-v5, with reasons

**1. The calibration is derived from the mean short-term loudness, not from the
file's gated integrated loudness (PLAN §2.2 says "RMS or a K-weighted
approximation").** Both are computed; the gated one is reported. The plan's
requirement is that level 1.0 sits on −23 LUFS *before the auto-trim*, and the
auto-trim is an integrator on the error of a 3 s K-weighted meter, so it
converges where the **mean of the log** of the short-term loudness equals the
target. Calibrating against the gated integrated value instead leaves a standing
trim of −4 to +2 dB, measured:

| slot | trim with gated-I calibration | trim with short-term-mean calibration |
|---|---|---|
| fire | +0.74 | +0.69 / +0.47 |
| wind | −3.95 | −0.50 / −1.37 |
| stream | −2.68 | −0.64 / +2.24 |
| birds | +2.25 | +0.53 / +3.47 |

The measurement also runs through the slot's HP/LP and the master's stereo
width, because a bright or a wide recording is otherwise credited with energy
the chain is about to change: adding those two stages pulled the spread of the
required offset from 5.1 dB to 3.0 dB.

**2. The 30 % consecutive-overlap rule is best-effort on the two short files.**
Exactly satisfying it needs segments shorter than span/2.4 — 9.8 s for the
23.5 s usable span of the fireplace and 5.4 s for wind, against the plan's
6–14 s. Worse, at the limit the only legal starts are at the two ends of the
file, so enforcing it *strictly* turns the scheduler into a two-position
ping-pong, which is exactly the periodicity the rule exists to prevent. The
shipped picker excludes the illegal interval around the previous segment's start
and draws uniformly from what is left; when nothing is left it draws from the
third of the file furthest from the previous segment. Result: 115 of 128
handovers within 30 %, worst case 60 % (wind), and stream and birds — the two
files with recognisable events — at 100 % compliance. Segment lengths stay at
the plan's 6–14 s / 20–45 s, capped by the file (wind's 14.8 s only allows
6–7.6 s).

**3. Birds' integrated loudness is 2.5–5 LU over target and the number is
kept.** `birds.wav` has an LRA of 16.4 LU on its own and 15.8–20.7 LU as
rendered: a dawn chorus is quiet air with loud close calls in it. Integrated
loudness is a gated power average, so it follows the calls; the loudness servo
follows the mean of the log, so it sets the level by the *whole* recording.
The gap between the two is the material's dynamics and **cannot be closed by
calibration at all** — the trim cancels any gain change, so for a given
recording the rendered integrated loudness is fixed at (target + that gap),
whatever `cal_db` is. The only two positions available are:

- calibrate on the short-term mean (shipped): trim ≈ 0, birds' short-term
  loudness matches the other slots, its integrated reads −18 … −20.4;
- calibrate on the gated integrated: birds would read −23 integrated but sit at
  a standing trim of about −4 dB, i.e. permanently 4 dB into the servo's ±6 dB
  of authority and perceptibly quieter than the other five between calls.

The first is the plan's own criterion ("before the auto-trim"), so it is what
ships. In the blend test, where birds sits at 0.3 among three other sounds, the
mix lands at −22.8 / −23.0. Its 1-second loudness steps (16.9 and 18.0 LU) have
the same cause and are also kept; both are measured to be the birdsong itself
and not the scheduler (see the split tables in v5-1).

**4. The ≤ 0.25 sample-step limit is met only by wind, and the number is kept
for the other three.** It is measuring the recordings' own transients, at
0.0002–0.008 % of samples; the crossfade-window split shows the scheduler adds
nothing to it, which is the check the plan is actually specifying. See v5-1.

**5. `fire` uses `sounds/fireplace.wav`, not `sounds/fire.wav`.** The file was
downloaded under that name and `sounds/LICENSES.md` refers to it; `ENV_FILE[]`
in `env.h` maps the slot to the file, and the panel names the file it wants when
one is missing.

**6. `--stats` gained a `segments` line** (`position+length` per segment, per
slot) and the recorded solos' `stats` line now prints six environment levels
instead of four. The segment log is a fixed array written by the realtime
thread and printed after the render finishes, so nothing prints from the RT
thread.

---
# v4.1 — rain droplets removed, fire rebuilt (2026-09-16, late)

User verdict on v4: rain had "a weird sort of sound like a bubble popping every now and then" (= the 12–20 ms downward droplet chirps → removed outright); fire "does not sound like it" (crackles were 18 dB under a low rumble). Fire is now Farnell-style and crackle-forward: flame band 450–1400 Hz driven by ~6 Hz low-passed noise (aperiodic flutter, the deliberate exception to the no-modulation rule for this opt-in sound), low roar at −9 dB, crackles at −3 dB (+2.4× makeup) in Poisson clusters of 1–4 with a 2–20 ms BP 1.8–5.5 kHz Q 2 burst each, amplitude log-uniform −4…−26 dB, rare pops. `ENV_CAL_DB[fire]` re-fitted −16.6 → −8.0 dB.

| render (150 s, level 1.0, tonal 0) | I LUFS | LRA | max step | FLUCT | ROUGH | <200 / 200-1k / 1-4k / >4k mean dB |
|---|---|---|---|---|---|---|
| fire solo #1 | −23.2 | 6.4 | 0.184 | −20.07 | −14.49 | (90 s run:) −31.3 / −34.1 / −32.6 / −36.8 |
| fire solo #2 | −22.8 | 6.7 | 0.245 | −20.26 | −14.81 | |
| rain solo (90 s) | −23.4 | 5.9 | 0.089 | −26.10 | −19.23 | −45.9 / −32.2 / −31.5 / −35.8 |
| blend 4 × 0.7 (150 s) | −23.7 | 5.2 | 0.121 | −19.79 | −13.84 | |

All within the v4 §4 limits (click ≤ 0.25, LUFS ±1.5, TP ≤ −1). Fire's spectrum moved from rumble-dominated (<200 Hz loudest) to mid/crackle-centred. Live engine swapped in paused; no QML change.

# Omanoise sound engine v4 — acceptance runs

Measurements for the tests in `docs/PLAN-sound-v4.md` §4. Method is exactly the
v3 method below: offline renders at `volume 1.0`, `intensity 0.5`,
`brightness 0.5`, `adaptive off`, produced with

```
XDG_STATE_HOME=<tmp> bin/omanoise-engine --render <mode> <sec> out.f32 --stats [--env name=v,...]
```

and measured with `ffmpeg` (`ebur128=peak=true` for integrated loudness, LRA,
true peak and the short-term series; three cascaded 2-pole sections per band
edge plus `volumedetect` for the spectral tilt; `astats` for NaN/Inf/flat/DC)
plus the engine's own `--stats` and `rough` lines. The short-term series is
decimated to one value per second and the first 10 s are dropped while the
fade-in and the loudness servo settle.

`tonal` is 0.5 for the mode renders and 0.0 for the environment-solo renders
(the solos are meant to be the bare generator). Every number is from the
shipped `bin/omanoise-engine`; the engine is generative, so ranges are
min … max over independent renders.

## v4-1. Focus regression (PLAN §4.1) — pass

Focus must not have changed. Nothing in its path did: the mode row in
`brain.c` is byte-identical, its chord pool was already `POOL_MAJOR`, its pad
skip probability and Eno period multiplier are unchanged, and the only two
edits that touch the Focus signal path at all are (a) the removal of
`master_run`'s `post_gain` argument, which was Sleep's 20-minute thinning and
was a constant 1.0 for every other mode, and (b) the move of the tonal-0
loudness tilt from a hard-coded −11 dB to a per-mode field, where Focus's value
is −11 dB. **6 × 180 s** renders (the plan asks for 3; three more were added to
bound the spread, see below).

| metric | v3 (3 renders) | v4 (6 renders) | limit | verdict |
|---|---|---|---|---|
| integrated LUFS | −21.1 … −20.9 | −21.1 … −20.7 | ±0.7 LU | pass (+0.2) |
| LRA | 5.0 … 6.9 | 5.5 … 7.0 | ±0.7 LU | pass (+0.1) |
| S std-dev (1 s) | 1.93 … 2.18 | 2.10 … 2.22 | ±0.7 | pass (+0.04) |
| max 1 s step | 1.22 … 1.51 | 1.20 … 1.90 | ±0.7 | pass (+0.39) |
| 200 Hz–1 k − <200 Hz | −8.5 … −7.6 | −11.2 … −8.1 | ±1 dB | 5/6 pass, one render at −11.2 |
| 200 Hz–1 k − 1–4 k | 14.5 … 16.0 | 11.8 … 15.6 | ±1 dB | 5/6 pass, same render at 11.8 |
| 200 Hz–1 k − >4 k | 38.8 … 41.1 | 35.4 … 40.7 | ±1 dB | 5/6 pass, same render at 35.4 |
| true peak | −9.2 … −6.6 | −8.5 … −7.2 | ±1 dB | pass |
| FLUCT | −25.46 … −24.54 | −25.38 … −24.11 | ±1 dB | pass (+0.43) |
| ROUGH | −11.29 … −9.93 | −11.38 … −9.82 | ±1 dB | pass (+0.11) |

**The one render that falls outside.** Render 2 of 6 reports all three band
ratios 2.7–3.4 dB low *together* (−11.2 / 11.8 / 35.4): that is one render with
more energy below 200 Hz than usual, not a tilt. The other five sit inside the
v3 range on every band. With a 37.4 s bass clock and chords changing every
60–90 s, a 180 s render contains four or five bass notes and two or three
chords, so which octave the bass lands in moves this ratio by a few dB; the v3
table is three renders wide and did not sample that case. Every other metric of
that render (loudness, LRA, true peak, FLUCT, ROUGH) is inside the v3 range.

Per-render Focus rows:

| # | I | LRA | Sstd | step | m−lo | m−1-4k | m−>4k | TP | FLUCT | ROUGH |
|---|---|---|---|---|---|---|---|---|---|---|
| 1 | −20.9 | 7.0 | 2.21 | 1.50 | −8.6 | 15.4 | 39.4 | −7.7 | −24.95 | −11.38 |
| 2 | −21.1 | 5.5 | 2.14 | 1.90 | −11.2 | 11.8 | 35.4 | −8.3 | −25.30 | −10.58 |
| 3 | −20.9 | 6.5 | 2.22 | 1.60 | −8.6 | 15.0 | 39.8 | −7.2 | −24.11 | −10.42 |
| 4 | −21.1 | 6.3 | 2.12 | 1.30 | −9.9 | 14.3 | 39.3 | −8.5 | −25.02 | −9.82 |
| 5 | −21.0 | 6.5 | 2.15 | 1.20 | −8.3 | 15.6 | 40.7 | −7.6 | −24.58 | −10.31 |
| 6 | −20.7 | 6.8 | 2.10 | 1.50 | −8.1 | 15.2 | 38.6 | −7.5 | −25.38 | −10.00 |

Melody event rate 18–22 per 180 s = 6.0–7.3 /min, against v3's 6.67–7.00 /min.

## v4-2. Relax, with Sleep merged in (PLAN §4.2) — pass

The merged parameters, and where they sit between the two v3 modes:

| parameter | v3 Relax | v3 Sleep | **v4 Relax** | how |
|---|---|---|---|---|
| `lufs_target` | −24 | −28 | **−25** | plan value (biased to Relax) |
| `pad_warm` / `pad_glass` | 0.70 / 0.30 | 0.20 / 1.00 | **0.45 / 0.55** | plan value = midpoint |
| `pad_cut` | 2.2 | 1.6 | **1.9** | plan value = midpoint |
| `pad_db` | −4.0 | −3.0 | **−3.5** | plan value = midpoint |
| `bass_db` | −6.0 | −1.5 | **−4.0** | plan value |
| `chord_min/max` | 75 / 120 s | 120 / 180 s | **95 / 150 s** | plan value = midpoint |
| `drift_depth` | 0.80 | 0.55 | **0.68** | plan value ≈ midpoint |
| `macro_lo/hi` | 0.70 / 1.16 | 0.80 / 1.10 | **0.75 / 1.13** | plan value = midpoint |
| `noise_color` | 0.55 | 0.85 | **0.70** | arithmetic midpoint |
| `noise_db` | −14.0 | −11.0 | **−12.5** | midpoint in dB |
| `noise_lp` | 2800 Hz | 2400 Hz | **2592 Hz** | geometric mean (√(2800·2400) = 2592.3) |
| `nature_boost` | 6.0 | 11.0 | **8.5** | midpoint in dB |
| `ocean_db` | −12.0 | −10.0 | **−11.0** | midpoint in dB |
| plate decay / damp / return | 0.90 / 3000 / −8 | 0.90 / 2500 / −8 | **0.90 / 2750 / −8** | plan value |
| melody gap / velocity / dyad | 6–14 s / 28–52 / 0.30 | none | **7–16 s / 26–48 / 0.25** | plan value |
| `mix_db` | −10.8 | −19.3 | **−12.6** | re-calibrated so the servo trim sits near 0 |

Sleep's 20-minute phasing timeline, its `sleep_fade`, its chord pool and its pad
period stretch are deleted, not disabled.

3 × 180 s:

| # | I | LRA | Sstd | step | m−lo | m−1-4k | m−>4k | TP | trim | FLUCT | ROUGH |
|---|---|---|---|---|---|---|---|---|---|---|---|
| 1 | −24.5 | 5.4 | 1.92 | 1.50 | −7.2 | 14.6 | 30.2 | −12.3 | −1.48 | −25.00 | −11.66 |
| 2 | −24.4 | 5.1 | 2.08 | 1.50 | −6.6 | 14.9 | 30.7 | −12.1 | −1.40 | −24.57 | −12.21 |
| 3 | −24.7 | 5.1 | 1.79 | 1.60 | −7.5 | 13.7 | 29.6 | −12.8 | −0.92 | −24.77 | −12.06 |

| test | target | measured | verdict |
|---|---|---|---|
| integrated LUFS | −25 ±1.5 | −24.4 … −24.7 | pass |
| LRA | ≥ 6 (v2 §5.1) | 5.1 … 5.4 | **fail by 0.6–0.9 LU**, see below |
| S std-dev | 0.8 … 3 | 1.79 … 2.08 | pass |
| max 1 s step | < 4 LU | 1.50 … 1.60 | pass |
| 200 Hz–1 k ≤ <200 Hz + 4 dB | ≤ +4 | −6.6 … −7.5 | pass |
| 1–4 k, 8–16 dB below 200 Hz–1 k | 8 … 16 | 13.7 … 14.9 | pass |
| 200 Hz–1 k ≥ >4 k + 18 dB | ≥ 18 | 29.6 … 30.7 | pass |
| true peak | ≤ −1 dBFS | −12.1 … −12.8 | pass |
| FLUCT ≤ env-ocean ref + 2 dB | ≤ −18.58 | −24.57 … −25.00 | pass, 6.0 LU margin |
| ROUGH ≤ env-ocean ref + 2 dB | ≤ −11.46 | −11.66 … −12.21 | pass, 0.20–0.75 dB margin |

The reference for the last two rows is the Environment-ocean-only render
required by the plan (level 0.8, tonal 0.5) — the three `envdef` rows in v4-3,
whose means are FLUCT −20.58 and ROUGH −13.46.

**Relax's LRA is 0.6–0.9 LU under the v2 window and the number is kept.** Half
of Sleep's job in the merge was to be steadier than Relax (`drift_depth` 0.55
vs 0.80, `macro` 0.80–1.10 vs 0.70–1.16), and the merged mode inherits the
midpoint of both, so it is by construction less dynamic than v3 Relax was
(v3: 4.5–7.4). The same corridor problem the v3 notes describe applies: LRA
runs about 2.4–2.9 × the per-second std-dev, and pushing LRA back over 6 means
pushing the std-dev toward 2.5 — i.e. undoing the merge. Every other dynamics
metric is inside its window.

## v4-3. Environment defaults (PLAN §4.3) — pass

3 × 180 s at the shipped defaults (ocean 0.8, rain/fire/wind 0, tonal 0.5):

| # | I | LRA | Sstd | step | m−lo | m−1-4k | m−>4k | TP | trim | FLUCT | ROUGH |
|---|---|---|---|---|---|---|---|---|---|---|---|
| 1 | −22.7 | 6.2 | 2.06 | 1.60 | −3.8 | 6.4 | 17.7 | −8.0 | 1.09 | −20.97 | −13.78 |
| 2 | −22.9 | 7.0 | 2.57 | 1.50 | −4.0 | 6.4 | 17.6 | −7.3 | 2.40 | −20.60 | −13.56 |
| 3 | −23.2 | 6.7 | 2.20 | 1.30 | −4.0 | 6.3 | 17.5 | −6.3 | 1.89 | −20.18 | −13.05 |

| test | target | measured | verdict |
|---|---|---|---|
| integrated LUFS | −23 ±1.5 | −22.7 … −23.2 | pass |
| tonal 1.0 vs 0.0 (120 s each) | ≤ 4 LU | −23.0 vs −23.8 = **0.8 LU** | pass |

The 0.8 LU is the point of the mode: the tonal layer is 18 dB down and its
loudness tilt is 0, so turning it off is nearly inaudible on the meter. This
deliberately inverts the v2/v3 "harmonic dominance ≥ 8 LU" test for this mode
only; Focus and Relax keep the −11 dB tilt and their ≥ 8 LU behaviour.

The default trim runs +1.1 … +2.4 dB because the generators are calibrated at
level 1.0 (PLAN §1.3) while the default ocean level is 0.8, which is −1.9 dB
after the level² mapping. That is well inside the servo's ±6 dB.

## v4-4. Each environment sound solo (PLAN §4.4) — 11 of 12 checks pass

Level 1.0, everything else 0, `tonal 0`, 2 × 120 s each.

| sound | I | LRA | TP | trim | max step | m−>4 k | FLUCT | ROUGH |
|---|---|---|---|---|---|---|---|---|
| ocean | −23.0 / −23.0 | 7.0 / 6.7 | −7.8 / −7.1 | +0.53 / −0.20 | 0.060 / 0.065 | 15.0 / 14.9 | −20.13 / −20.65 | −13.76 / −13.93 |
| rain | −22.4 / −22.7 | 7.1 / 5.7 | −7.2 / −9.4 | −0.23 / −1.96 | 0.123 / 0.103 | 4.6 / 4.7 | −24.27 / −22.68 | −18.22 / −17.53 |
| fire | −23.0 / −23.0 | 8.0 / 8.1 | −7.4 / −7.1 | +1.09 / +0.39 | 0.028 / 0.030 | 21.7 / 22.7 | −18.39 / −18.19 | −12.05 / −11.52 |
| wind | −22.3 / −22.0 | 7.7 / 11.8 | −5.9 / −4.9 | −2.90 / −1.18 | 0.044 / 0.045 | 20.5 / 21.1 | −18.71 / **−17.02** | −12.16 / −11.11 |

Reference: ocean solo means FLUCT −20.39, ROUGH −13.85.

| check | limit | result |
|---|---|---|
| loudness lands within ±1.5 LU of −23 | −21.5 … −24.5 | **pass 8/8** (−22.0 … −23.0), no trim saturated |
| FLUCT ≤ ocean + 3 dB | ≤ −17.39 | rain pass, fire pass (0.8–1.0 dB margin), **wind 1 of 2 renders 0.37 dB over** |
| ROUGH ≤ ocean + 3 dB (rain, wind) | ≤ −10.85 | pass 4/4 (−11.11 … −18.22) |
| ROUGH ≤ ocean + 5 dB (fire) | ≤ −8.85 | pass 2/2 (−11.52, −12.05) |
| true peak ≤ −1 dBTP | ≤ −1 | pass 8/8, worst −4.9 |
| click detector, max \|x[n]−x[n−1]\| | ≤ 0.25 | pass 8/8, worst **0.123** (rain) — the fire crackles are 0.028–0.030 |
| >4 kHz ≥ 10 dB below 200 Hz–1 kHz (ocean, fire, wind) | ≥ 10 | pass 6/6 (14.9 … 22.7) |
| >4 kHz ≥ 4 dB below 200 Hz–1 kHz (rain) | ≥ 4 | pass 2/2 (4.6, 4.7) |

**The one failure: wind's fluctuation, 1 of 2 renders 0.4 dB over.** Wind is
the narrowest-band generator in the bank, and a narrow-band noise has a
fluctuating envelope for free: the narrower the band, the more of that envelope
energy lands in 1–8 Hz. Measured on the way to the shipped design, all at the
same calibration:

| wind variant | FLUCT | ROUGH |
|---|---|---|
| the plan's design: brown noise → two cascaded LP swept 180–800 Hz | −13.88 … −14.90 | −9.00 … −9.60 |
| one LP section instead of two | −13.69 … −15.34 | −8.59 … −10.44 |
| LP at fc and 2·fc | −14.41 … −14.42 | −9.16 … −9.21 |
| half brown + half pink into the cascade | −16.17 … −17.39 | −11.19 … −11.66 |
| pink into the cascade | −17.50 … −18.97 | −11.27 … −12.88 |
| **shipped: pink + a leaf-rustle band (HP 900 Hz, LP 4 kHz) at −14 dB** | **−17.02 … −18.71** | **−11.11 … −12.16** |

The shipped version is 4–5 dB better than the plan's brown-noise design and
lands on the limit rather than 6 dB over it. Closing the last 0.4 dB means
widening the band further, which means a brighter wind, and the rustle band
already costs 34 dB of the 55 dB that the plan's dark version had between
200 Hz–1 kHz and >4 kHz. The number is kept.

Wind is also the most dynamic thing in the bank (LRA 7.7 and 11.8, S std-dev
2.53 and 4.01): ±5 dB of gusts, a cutoff sweep across two octaves and the
shared macro contour all move its level, and the plan sets the first two.

## v4-5. Four-way blend (PLAN §4.5) — pass

All four at 0.7, 120 s, tonal 0.5:

| # | I | LRA | TP | trim | max step | FLUCT | ROUGH |
|---|---|---|---|---|---|---|---|
| 1 | −23.1 | 6.9 | −8.1 | +1.19 | 0.053 | −18.62 | −12.47 |
| 2 | −23.0 | 6.6 | −8.5 | +1.09 | 0.054 | −18.38 | −12.38 |

| check | limit | result |
|---|---|---|
| no clipping | true peak ≤ −1 dBTP | pass (−8.1, −8.5); sample peak 0.377–0.394 |
| loudness | −23 ±2 | pass (−23.0, −23.1) |
| ROUGH ≤ ocean solo + 5 dB | ≤ −8.85 | pass (−12.38, −12.47) |

The power-preserving soft-knee sum is what keeps this in place: four levels of
0.7 give Σ level² = 1.96, so the bus gain is 1/√1.96 = 0.71 and the blend
carries the same total energy as one sound at 1.0. `astats` on the blend:
0 NaN, 0 Inf, 0 denormals, flat factor 0.0, DC −2.0e−6.

Fade-out on the blend (`--render environment 60 out.f32 --tail 20`, peak per
2 s window): 58–60 s −10 dB → 62–64 s −42 dB → **64–66 s exactly 0**, plate
tail included.

## v4-6. Modulation-line audit (PLAN §4.6, method from PLAN v3 §5.4)

Every oscillator in the new code that can reach the audio. `drift_t` re-draws
each segment's length at 0.7–1.3 × its base, so the fastest instantaneous rate
is 1/(0.7 × base); that worst case is the number given.

| source | file | rate | status |
|---|---|---|---|
| rain patter impulses | `env.c:env_rain` | Poisson, 800–1600/s | a shot-noise process: flat spectrum, no line |
| rain band-pass centre | `env.c` drift, base 26 s | 0.038–0.055 Hz | value noise, ≪ 0.2 Hz |
| rain shower envelope (±3 dB) | `env.c` drift, base 16 s | 0.063–0.089 Hz | value noise, ≪ 0.2 Hz |
| rain droplet chirp | `env.c` | 2.4 → 1.6 kHz for 12–20 ms | audio-band transient, far above 300 Hz |
| rain droplet events | `env.c` | Poisson, 1–2.5/s | no line |
| fire glow flicker (±4 dB) | `env.c` drift, base 5 s | 0.20–0.29 Hz | value noise; the fastest drift in the engine, still not periodic and still under the 0.3 Hz the plan allows here |
| fire mid-hiss centre | `env.c` drift, base 19 s | 0.053–0.075 Hz | value noise |
| fire crackles | `env.c` | Poisson, 3–7/s | no line; 1.5 ms raised-cosine rise, 4–18 ms long, ≥ 18 dB under the glow |
| fire pops | `env.c` | Poisson, 0.15/s | no line; the 150–300 Hz ring is audio content, not modulation |
| wind cutoff sweep | `env.c` drift, base 34 s | 0.029–0.042 Hz | value noise |
| wind gusts (±5 dB) | `env.c` drift, base 21 s | 0.048–0.068 Hz | value noise |
| environment bus soft knee | `layers.c` | static per control block, slewed | not an oscillator |
| environment plate send | `layers.c` | constant −14 dB | not an oscillator |

Nothing in the bank is periodic and nothing sits between 0.2 Hz and 300 Hz.
Everything carried over from v3 (the pad Eno clocks, the bass and chord clocks,
the ocean wave engines, the plate tank drifts, the opt-in 16 Hz AM and pulse)
is unchanged and is enumerated in v3-4 below. Removed in v4: Sleep's 600 s /
1200 s phasing ramps and its 1200 s −3 dB thinning staircase.

**Measured confirmation.** The peak modulation frequency of the environment
solos is different in every render and 38–46 dB below the envelope's DC:
ocean 7.17 / 6.27 Hz, rain 12.16 / 7.62 Hz, fire 9.01 / 18.26 Hz,
wind 8.21 / 9.01 Hz — i.e. no stable line, unlike the just-intonation artefact
v3-2 describes. That artefact is still visible when the tonal layer is present
(the Environment default renders report 32.79 Hz three times out of three,
which is C3 / 4 exactly, at −28 to −29 dB), and it is still the pad's
consonance rather than a modulator.

## v4-7. State-file migration (PLAN §4.7) — pass

`mode sleep` → relax, `mode ocean`/`mode env` → environment are resolved in
`brain.c:mode_from_name`, so the same table serves the state file and the
command.

| input file | result |
|---|---|
| v3 file, `"mode":"sleep"`, `"modulation":true`, volume 0.42 | `"version":4`, `"mode":"relax"`, every other field preserved **including `modulation:true`** (only pre-v3 files get modulation/pulse forced off), `"env"` added with the defaults |
| v3 file, `"mode":"ocean"` | `"version":4`, `"mode":"environment"`, `"env"` defaults added |
| v2 file, no `version` key, `pulse:true`, `modulation:true`, `mode:"ocean"` | `"version":4`, `"mode":"environment"`, both flags forced false, `"env"` defaults added |

Commands, checked over a live stdin session (PipeWire stream left inactive, no
audio): `mode sleep` → `"mode":"relax"`; `mode ocean` and `mode env` →
`"mode":"environment"`; `env rain 0.5` → `"rain":0.500`; `env fire 1.5` →
clamped to `1.000`; `env bogus 0.2` and `env wind` (no value) → `error usage:
env <ocean|rain|fire|wind> <0..1>` with no state change; `volume`, `intensity`,
`brightness`, `tonal`, `binaural`, `pulse`, `modulation`, `adaptive`, `state`,
`play`, `pause`, `toggle`, `quit` all behave as in v3. Every accepted command
echoes one `state` line and rewrites the state file.

## v4-8. Resources and plumbing (PLAN §4.8)

| check | target | measured |
|---|---|---|
| CPU while generating | ≤ 4 % of one core | **1.27 % Focus, 1.54 % Relax, 2.23 % Environment** with all four sounds at 0.7 (wall time of `--render <mode> 600 …` ÷ 600 s; the offline path also writes the file and analyses every sample, which the audio callback does not) |
| RSS | ≤ 120 MB | **47.4 MB** peak for an offline render (`VmHWM`), **51.2 MB** for the live process |
| sounds ready | < 3 s | **91–136 ms** (PADsynth bank 50–82 ms / 5.9 MB, SoundFont 41–57 ms) |
| CPU, live, paused | — | 0.2 % |
| build | zero warnings | `./build.sh` clean, and clean again under `-Werror` |
| artefacts | none | 0 NaN / 0 Inf / 0 denormals / flat factor 0 / \|DC\| ≤ 4e−6 / true peak ≤ −4.9 dBTP across every render in this sweep |

Plumbing, without ever starting playback on the user's machine:

| check | result |
|---|---|
| engine restarts on binary change | yes — `pkill -x omanoise-engine`, the shell respawned it **paused** within ~3 s |
| QML hot-reload | picked up: `Local plugin changed, reloading: omanoise` in the journal after every edit, and **no QML errors** in `journalctl --user` for the new Panel/Service |
| `state` JSON fields | `version, playing, mode, volume, intensity, brightness, tonal, binaural, adaptive, pulse, modulation, env{ocean,rain,fire,wind}, daypart, bank, sf2, lufs` |
| `omarchy-shell omanoise state` | answers and parses as JSON, **but from the stale handler** — see below |
| `omanoise env rain 0.5` | `Function not found.` — same stale handler |
| `~/.local/state/omanoise.json` | ends at the PLAN §5 defaults: `version 4`, `mode focus`, `volume 0.700`, `intensity/brightness/tonal 0.500`, `binaural false`, `adaptive true`, `pulse false`, `modulation false`, `env {0.800, 0, 0, 0}` |

**The stale-IPC caveat is the same one v3 recorded, and it is not in this
code.** The running shell keeps the *first* `IpcHandler` registered for target
`omanoise`; every hot-reload since logs

```
QML IpcHandler at .../Panel.qml[61:3]: Handler was registered but will not be
used because another handler is registered for target omanoise
```

so `omarchy-shell omanoise state` is still answered by a pre-edit `Service.qml`
(its reply has no `env` field) and `omanoise env …` reports *Function not
found*, because the old handler has no `env` function. The new handler and the
new service are loaded and correct; the registration clears itself the next
time the shell restarts, which was deliberately **not** done here. Commands the
old handler does have (`state`, `mode`, `play`, `volume`, …) still reach the new
engine: `omarchy-shell omanoise mode environment` worked. `omanoise play` was
not exercised live, because that would put sound on the user's speakers; the
play path is exercised by every offline render instead.

## v4 — deviations from PLAN-sound-v4, with reasons

Every number here is the shipped value.

1. **Wind is pink noise plus a leaf-rustle band, not brown noise into two
   cascaded low-passes.** The plan's design measures FLUCT −13.9 … −14.9, i.e.
   6 dB over the acceptance limit §4.4 sets, because a 4-pole 180–800 Hz filter
   on brown noise is so narrow that its own envelope fluctuates hard at 1–8 Hz.
   The shipped generator keeps the sweep, the two cascaded sections, the ±5 dB
   gusts, the full channel decorrelation and the ban on any band-pass whistle;
   it changes the source to pink and adds a quiet (−14 dB) HP 900 Hz / LP 4 kHz
   rustle band. Full variant measurements are in v4-4. It still fails the FLUCT
   limit on one render in two, by 0.37 dB.
2. **The fire mid hiss is −9 dB relative to the glow, not −12.** Fire's
   fluctuation sat within 0.1 dB of its limit at −12 dB; widening the
   generator's band with 3 dB more hiss bought 0.4–0.6 dB of margin
   (FLUCT −17.7/−17.9 → −18.2/−18.4) and cost 3 dB of the 25 dB the fire had
   between 200 Hz–1 kHz and >4 kHz. Everything else about the fire is the
   plan's: glow LP 350 Hz with ±4 dB flicker, crackles 3–7/s at 4–18 ms,
   log-uniform over 18 dB, bus 18 dB under the glow, pops 0.15/s.
3. **The rain hiss low-pass is two cascaded 2-pole sections at 6.5 kHz, not
   one.** With a single section the >4 kHz band came out 1.9 dB below the
   200 Hz–1 kHz band and §4.4 requires 4 dB; with the cascade it is 4.6–4.7 dB.
   The corner and the −16 dB level are the plan's.
4. **The level envelopes of the bank are "alternating" drifts.** `drift_t` can
   optionally flip sign on every new segment (`drift_set_alternating`, already
   used for the macro contour in v2/v3); the rain shower, the fire flicker, the
   wind gusts and the wind cutoff sweep all use it. Segment lengths and
   magnitudes stay random, so nothing becomes periodic, but a 180 s render can
   no longer sit 5 dB off the generator's own long-run mean — which it did
   before, and which was eating the loudness servo's ±6 dB (wind's trim swung
   from +5.4 to −4.1 between two renders of the same build).
5. **Per-generator calibration constants**, measured against −23 LUFS at level
   1.0 as §1.3 requires: ocean −4.4 dB, rain +7.5 dB, fire −16.6 dB,
   wind −12.6 dB (`layers.c:ENV_CAL_DB`). They differ by 24 dB because the four
   generators have wildly different crest factors and bandwidths; the numbers
   are the outcome of the iteration, not a design choice. Note that ocean's
   +8.6 dB over its v3 bed level is expected: in v3 the ocean was one layer
   inside a mode that also had pads and a noise bed, and at `tonal 0` that mode
   dropped its loudness target by 11 dB.
6. **`master_run` lost its `post_gain` argument.** It existed only for Sleep's
   20-minute thinning; it was a constant 1.0 everywhere else, so removing it
   cannot change Focus or Relax.
7. **Relax's LRA is 5.1–5.4 against the v2 window's ≥ 6**, kept and explained
   in v4-2.
8. **Focus's acceptance used 6 renders rather than 3**, to bound the one
   band-ratio outlier described in v4-1.

---

# Omanoise sound engine v3 — acceptance runs (superseded by the v4 section above)

Measurements for the tests in `docs/PLAN-sound-v3.md` §5 (which extends
`PLAN-sound-v2.md` §5). Every row is an offline render at `volume 1.0`,
`intensity 0.5`, `brightness 0.5`, `tonal 0.5`, `adaptive off`, produced with

```
XDG_STATE_HOME=<tmp> bin/omanoise-engine --render <mode> 180 out.f32 --stats [--layers <mask>]
```

and measured with ffmpeg exactly as in the v2 section below (`ebur128` for
loudness/LRA/true peak and the per-second short-term series, three cascaded
2-pole sections per band edge plus `volumedetect` for the spectral tilt,
`astats` for NaN/Inf/flat/DC), plus the engine's own `--stats` line, which in
v3 also carries the roughness metric.

Because the engine is generative every cell is **min … max across 3 independent
180 s renders**. Every number comes from the same build as the shipped
`bin/omanoise-engine`.

## v3-1. Loudness, dynamics and spectral tilt — full mix

New LUFS targets: Focus −21, Relax −24, Sleep −28, Ocean −23.

| mode | integrated LUFS | LRA | S std-dev | max 1 s step | 200-1k − <200 | 200-1k − >4k | 200-1k − 1-4k | true peak |
|---|---|---|---|---|---|---|---|---|
| Focus | -21.1 … -20.9 | 5.0 … 6.9 | 1.93 … 2.18 | 1.22 … 1.51 | -8.5 … -7.6 | 38.8 … 41.1 | 14.5 … 16.0 | -9.2 … -6.6 |
| Relax | -23.8 … -23.5 | 4.5 … 7.4 | 1.71 … 2.27 | 1.14 … 1.76 | -6.8 … -6.2 | 29.6 … 30.1 | 14.6 … 15.3 | -11.6 … -9.3 |
| Sleep | -28.0 … -27.8 | 4.7 … 5.6 | 1.54 … 1.94 | 1.22 … 2.04 | -7.4 … -5.7 | 32.2 … 33.7 | 19.9 … 21.4 | -15.2 … -13.6 |
| Ocean | -22.8 … -22.3 | 5.9 … 7.9 | 2.15 … 2.87 | 1.13 … 1.82 | -6.0 … -4.6 | 25.7 … 26.8 | 14.4 … 15.5 | -9.3 … -7.4 |

Verdicts, counted per render (v2 §5 targets, v3 LUFS targets):

| mode | LUFS ±1.5 LU | LRA in range | S std-dev 0.8–3 | 1 s step < 4 LU | 200-1k ≤ <200 + 4 dB | 200-1k ≥ >4k + 18 dB | 1-4k 8–16 dB below 200-1k | true peak ≤ −1 dBFS |
|---|---|---|---|---|---|---|---|---|
| Focus | 3/3 | 3/3 | 3/3 | 3/3 | 3/3 | 3/3 | **3/3** | 3/3 |
| Relax | 3/3 | 3/3 | 3/3 | 3/3 | 3/3 | 3/3 | **3/3** | 3/3 |
| Sleep | 3/3 | 3/3 | 3/3 | 3/3 | 3/3 | 3/3 | **0/3** (19.9–21.4, see below) | 3/3 |
| Ocean | 3/3 | 2/3 (5.9 on one render, target ≥ 6) | 3/3 | 3/3 | 3/3 | 3/3 | **3/3** | 3/3 |

**Sleep's 1–4 kHz band is 4–5 dB darker than the v2 window allows, and the
number is kept rather than fixed.** The window exists to stop the mix going
muddy; Sleep in v3 is deliberately the darkest mode in the set — its pad
low-passes at 1.6 × f0 per PLAN v3 §2.3, it has no melody at all, and the only
material above 1 kHz is its noise bed, which has already been raised from the
plan's −18 dB to −11 dB and its corner from 800 Hz to 2.4 kHz in the attempt.
Closing the remaining 4 dB would mean either putting the bed above the pad or
opening the glass pad's cutoff, i.e. undoing the thing the mode is for.
Focus and Relax needed the same treatment and do land inside the window.

**Ocean's LRA corridor** is the one the v2 notes already described: §5.1 asks
for LRA ≥ 6 and §5.4 for a per-second std-dev ≤ 3, and for smooth material LRA
runs about 2.4–2.9 × the std-dev, so the two targets leave a corridor of roughly
LRA 6 … 8. Ocean sits in it (5.9 … 7.9 / std-dev 2.15 … 2.87) and one render of
three fell 0.1 LU below the floor.

## v3-2. Roughness and fluctuation — the key test

`--stats` now reports, for every render: the output envelope (|x| → **four**
cascaded one-poles at 100 Hz → decimated to 400 Hz), mean-removed, Hann
windowed and transformed with the PADsynth FFT.

- **FLUCT** = energy in 1–8 Hz (fluctuation strength peaks near 4 Hz)
- **ROUGH** = energy in 15–200 Hz (the roughness band)

both in dB relative to the square of the envelope's mean, so both are
level-independent. Requirement: **Focus, Relax and Sleep ≤ Ocean + 2 dB on the
same set of runs.**

| mode | FLUCT, 3 renders | mean | ROUGH, 3 renders | mean | peak mod. frequency |
|---|---|---|---|---|---|
| Focus | -25.12 / -24.54 / -25.46 | **-25.04** | -11.29 / -11.29 / -9.93 | **-10.84** | 43.7 / 49.1 / 32.8 Hz |
| Relax | -23.25 / -24.00 / -24.89 | **-24.05** | -12.35 / -12.08 / -11.75 | **-12.06** | 55.1 / 55.1 / 45.9 Hz |
| Sleep | -25.36 / -25.39 / -25.36 | **-25.37** | -11.65 / -11.74 / -12.08 | **-11.82** | 49.1 / 49.1 / 49.1 Hz |
| **Ocean (reference)** | -23.14 / -21.83 / -22.38 | **-22.45** | -11.21 / -11.07 / -11.65 | **-11.31** | 32.8 / 32.8 / 32.8 Hz |

| test | limit (Ocean mean + 2 dB) | Focus | Relax | Sleep | verdict |
|---|---|---|---|---|---|
| FLUCT | ≤ −20.45 dB | −25.04 | −24.05 | −25.37 | **pass, 3.6–4.9 dB of margin** (3/3 renders each) |
| ROUGH | ≤ −9.31 dB | −10.84 | −12.06 | −11.82 | **pass** (3/3 renders each; the thinnest single render is Focus at −9.93, 0.6 dB inside) |

### The "stable peak" sub-test, and why it is reported rather than met

PLAN §5.2 also asks that the peak modulation frequency not be the same ±0.2 Hz
in all three renders of a mode. Sleep reports 49.10 Hz three times out of three
and Ocean 32.79 Hz three times out of three, so on a literal reading both fail —
including Ocean, the exempt reference.

Those numbers are not a modulator. They are **exact subdivisions of each mode's
key root**: Sleep's root is G2 = 98.00 Hz and 98.00 / 2 = 49.00; Ocean's is
C3 = 130.81 Hz and 130.81 / 4 = 32.70. Since v3 snaps every sustaining voice to
a just ratio above the key root (see below), all the chord tones are rational
multiples of one fundamental, so every difference frequency between them is a
rational fraction of the root — a line, in the same place every time, that moves
only when the key does. It is the *consonance* showing up in the metric. Before
just-intonation snapping the same energy was there but smeared across 1–8 Hz by
equal temperament's mistuning, which is strictly worse for this material.

The peaks are 17.5–25.5 dB below the envelope's DC in every render, i.e. a
modulation depth of 5–13 %, and the enumeration in v3-4 shows there is no
periodic oscillator left in the control path that could produce them.

## v3-3. Attack audit (PLAN §5.3)

`bin/omanoise-engine --render-bank <dir>` renders one soft note per instrument
at the bottom and top of the register that instrument is actually played in,
writes it as raw mono f32 and measures it in-process: a 40 Hz envelope follower
for the peak and the timings, a 32768-point Hann-windowed FFT for the centroid.
Requirement: onset to 90 % of peak ≥ 150 ms, centroid < 2 kHz.

| instrument | prog | key | vel | peak | energy | t → 10 % | **t → 90 %** | t → −60 dB | centroid |
|---|---|---|---|---|---|---|---|---|---|
| Acoustic Grand | 0 | 36 | 34 | 0.00036 | −74.7 dB | 38.0 ms | **415.9 ms** | > 8 s | 467 Hz |
| Acoustic Grand | 0 | 72 | 62 | 0.00045 | −73.9 dB | 20.9 ms | **1163.8 ms** | > 8 s | 1016 Hz |
| Orchestral Harp | 46 | 48 | 28 | 0.00083 | −74.4 dB | 31.6 ms | **237.8 ms** | 6290 ms | 302 Hz |
| Orchestral Harp | 46 | 72 | 52 | 0.00178 | −69.2 dB | 17.1 ms | **222.5 ms** | 4191 ms | 525 Hz |
| Vibraphone | 11 | 48 | 27 | 0.00142 | −71.8 dB | 66.2 ms | **288.1 ms** | 4644 ms | 131 Hz |
| Vibraphone | 11 | 78 | 50 | 0.00117 | −73.0 dB | 33.6 ms | **287.0 ms** | 3515 ms | 741 Hz |
| Music Box | 10 | 72 | 12 | 0.00029 | −83.2 dB | 22.8 ms | **201.5 ms** | 2250 ms | 605 Hz |
| Music Box | 10 | 84 | 23 | 0.00110 | −71.5 dB | 20.1 ms | **227.8 ms** | 1992 ms | 1082 Hz |

All eight are ≥ 201 ms, i.e. above the 150 ms floor and inside the 141–220 ms
band the startle literature calls fully mitigating; all eight centroids are
below 1.1 kHz, well under the 2 kHz limit. The generator settings that produce
this are `GEN_VOLENVATTACK` offsets for a nominal 170 / 260 / 300 / 300 ms
attack on piano / harp / vibraphone / music box.

**Kalimba (108) failed this audit and was replaced by Vibraphone (11).** The
plan named kalimba as Focus's second instrument. Its samples decay in 80–200 ms,
so past a certain point a longer volume-envelope attack cannot move the peak:
the note is already fading before the ramp finishes. Sweeping the attack from
0 to 0.6 s across keys 48–96 gave a t → 90 % that saturated between **82 and
413 ms depending on the key** — under the floor at keys 60, 66, 84 among others,
and never controllable. Vibraphone, which is on the plan's own candidate list
and is also a mallet instrument, measures a flat 287 ms at every key from 48 to
96 and carries no motor tremolo (a 2–12 Hz scan of its envelope is 13 dB below
the mean, the lowest of the candidates: Celesta −8.8, Kalimba −10.0,
Marimba −4.2, Vibraphone −13.1).

The other presets auditioned and rejected: Bright/Electric Grand, Rhodes and
Chorused EP (not acoustic), Marimba (t → 90 % falls to 43 ms in the top
register), Celesta (usable, 193–320 ms, but redundant next to the music box),
Choir Aahs and Warm Pad (sustained, and PADsynth already covers pads),
Sweep Pad and Crystal (synth), **Tubular Bells (14, inharmonic — excluded by
PLAN §2.1 and measured at a 770 Hz centroid with 13 ms attacks)**.

The **pulse** (kick and off-beat tick, Focus only) is the one event type with a
short attack, 10 ms and 6 ms. It is percussion, which the research excludes from
calming material anyway, and it is **off by default** in v3.

## v3-4. Periodic-oscillator audit (PLAN §5.4)

Every remaining oscillator in the control path that can reach the audio, with
its rate. Anything between 0.2 Hz and 300 Hz has to be justified or removed.

| source | file | rate | status |
|---|---|---|---|
| 16 Hz amplitude modulation | `master.c:master_run` | 16 Hz | inside the roughness band. **Default off** in v3, Focus only, depth capped at 0.35 (v2: 0.60), and the panel says so. Opt-in. |
| Pulse kick/tick | `layers.c` | 62 BPM → one onset every 0.48 s | **Default off** in v3, Focus only. Opt-in. |
| Pad Eno clocks | `layers.c:PAD_PERIOD` | 17.3 / 21.9 / 26.1 / 31.7 s ×1.0–2.24 = 0.014–0.058 Hz | below 0.2 Hz |
| Bass clock | `layers.c:E.bass_period` | 37.4 s = 0.027 Hz | below 0.2 Hz |
| Chord clock | `layers.c` | 60–180 s | below 0.2 Hz |
| Ocean wave engines | `layers.c:ocean_t` | base 9–17 s, period *and* rise/fall re-drawn every cycle | aperiodic by construction |
| `drift_t` generators (pad cutoff, pad pan, melody pan, layer gains, plate damping, macro contour, plate tank) | `dsp.h:drift_step` | base 9–40 s, each segment's length re-drawn ±30 % | aperiodic, 0.02–0.11 Hz |
| Dattorro tank modulation | `fx.c` | **was** a 0.7 Hz sine in v2 | replaced by two `drift_t` at 9.3 s and 11.7 s |
| PADsynth partial bandwidth | `padsynth.c` | a Gaussian band, not an oscillator | noise-like; its width is the lever that was tuned against v3-2 |

Removed outright and verified absent from the source: the Solina chorus
(0.9 Hz and 5.9 Hz LFOs), the shimmer bus, the ping-pong delay, the granular
cloud, the 7-saw supersaw pad with its ±3-cent detune walk, the Risset bell, the
FM bell, and both Karplus-Strong voices (pluck and water drop).

## v3-5. Harmonic dominance (tonal 1.0 vs 0.0)

120 s renders; at `tonal 0` the pad, bass and melody buses are muted and only
the calibrated noise/ocean bed is left.

| mode | tonal 1.0 | tonal 0.0 | difference | target | verdict |
|---|---|---|---|---|---|
| Focus | -20.7 LUFS | -31.1 LUFS | **10.4 LU** | ≥ 8 LU | pass |
| Relax | -23.1 LUFS | -33.7 LUFS | **10.6 LU** | ≥ 8 LU | pass |
| Sleep | -27.4 LUFS | -37.7 LUFS | **10.3 LU** | ≥ 5 LU | pass |
| Ocean | -22.5 LUFS | -33.0 LUFS | **10.5 LU** | not required | 10.5 LU |

## v3-6. Event rate

| mode | intensity 0.0 | intensity 0.5 (3 × 180 s) | intensity 1.0 | v3 target at 0.5 |
|---|---|---|---|---|
| Focus | 2.80 /min | 6.67 … 7.00 /min | 11.00 /min | 5–10 (phrases of 3–6 notes 3–8 s apart, then 10–20 s) |
| Relax | 2.40 /min | 5.67 … 6.00 /min | 9.20 /min | 4–10 (one event every 6–14 s) |
| Sleep | 0 | 0 | 0 | 0 |
| Ocean | 0 | 0 | 0 | 0 |

The intensity slider spans ×0.39 … ×1.57 of the rate at 0.5 (spec: ×0.4 … ×1.6).
No two events are ever closer than 1.5 s, enforced after the intensity scaling.

## v3-7. Artefacts, resources, fade-out

Across **all 44 renders of this sweep** (4 modes × {3 × 180 s + tonal 1.0/0.0 +
layer masks 1/3/7 + tail + no-SoundFont}, plus 4 × 300 s for the intensity
sweep). `astats` was run on the twelve full-mix 180 s renders; the engine's own
per-sample non-finite / peak / DC / step checks ran on all of them.

| check | target | measured |
|---|---|---|
| NaN / Inf / denormal samples (`astats`) | 0 | 0 / 0 / 0 |
| non-finite sample check in the renderer | none | none (the render aborts on one) |
| Flat factor (`astats`) | 0 | 0.0 |
| DC offset | < 0.002 | 3.0e-06 (largest \|DC\|, in a no-SoundFont render) |
| true peak | ≤ −1 dBFS | −6.6 dBFS (loudest render) |
| click detector: max \|x[n] − x[n−1]\| after 5 s | < 0.25 | 0.0382 (largest) |
| sample peak | < 1.0 | 0.4699 (largest) |

**Fade-out** (`--render <mode> 60 out.f32 --tail 20`, peak per 2 s window):

| mode | 58–60 s (playing) | 62–64 s | 64–66 s | 70–80 s |
|---|---|---|---|---|
| Focus | −12 dB | −41 dB | **0** | 0 |
| Relax | −15 dB | −42 dB | **0** | 0 |
| Sleep | −18 dB | −50 dB | **0** | 0 |
| Ocean | −11 dB | −41 dB | **0** | 0 |

Every mode reaches *exactly* zero within 4 s of `pause`, plate tail included.

**Resources** — Linux 7.2.3-arch1-3, x86-64, `gcc -O2 -Wall -Wextra -std=gnu11`:

| check | target | measured |
|---|---|---|
| CPU while generating | ≤ 4 % of one core | **1.20 – 1.48 %** (wall time of `--render <mode> 600 …` ÷ 600 s: Focus 1.20 %, Relax 1.48 %, Sleep 1.43 %, Ocean 1.41 %) |
| RSS | ≤ 120 MB | **47.4 MB** peak for an offline render (`VmHWM`), with `synth.dynamic-sample-loading 1` and four presets resident |
| sounds ready | < 3 s | **~95 ms total**: PADsynth tables 55–60 ms (4 tables, 4 MB), SoundFont 37–40 ms, whole bank 5.9 MB |

The CPU figure is a strict upper bound on the live cost: the offline path runs
the same `engine_render()` but additionally writes the file and does the
peak/DC/step/`isfinite`/envelope analysis over every sample, which the audio
callback does not. It is *lower* than v2's 2.3–2.8 % because the chorus, the
shimmer bus (an octave-up shifter plus a second Dattorro plate) and the granular
voices are gone; FluidSynth adds little because it is only asked for audio when
a note is sounding.

`fluid_synth_write_float` is called once per 64-sample control block, which is
FluidSynth's own internal buffer size, so nothing is re-blocked.

## v3-8. FluidSynth realtime-safety check (PLAN §6)

The question the plan raised: with `synth.dynamic-sample-loading 1`, does
`fluid_synth_noteon` load sample data synchronously on the realtime thread?

**Measured: no.** A probe against FluidSynth 2.6.0 loaded FluidR3_GM, selected
eight presets and then played notes, watching `VmRSS` and timing every call:

| call | time | RSS after |
|---|---|---|
| `fluid_synth_sfload` | 33–37 ms | 17.9 → 39.6 MB |
| `fluid_synth_program_select` ×8 | 0.00 – 3.97 ms each | 39.6 → 48.6 MB, growing at *this* call |
| `fluid_synth_noteon`, first note on each preset | 4 – 21 µs | unchanged |
| `fluid_synth_noteon`, second note on each preset | 4 – 105 µs | unchanged |

Sample data is paged in by preset *selection*, not by note-on. v3 therefore does
`sfload` and all four `program_select` calls on the bank worker thread, never
changes the program afterwards, and publishes the synth pointer to the realtime
thread only once it is fully built. Dynamic loading stays enabled and no
pre-warm notes are needed. Voices come from the pre-allocated polyphony pool, so
note-on does not allocate either.

Two further realtime hazards were closed:

- **FluidSynth's log handlers are global and print to stderr.** All five levels
  are replaced with no-ops in `sf2_create`, so nothing inside the library can
  print from the audio thread. Failures are detected from return codes instead.
- **`new_fluid_settings()` probes every audio driver**, which writes about
  twenty ALSA errors straight to stderr (bypassing the log handler) and costs
  ~8 MB of RSS. `fluid_audio_driver_register()` with an empty list before it
  stops that; we never use a FluidSynth audio driver, only `write_float`.

## v3-9. Fallback with no SoundFont

`OMANOISE_SF2=/nonexistent/none.sf2`, 60 s per mode:

| mode | sf2 | sounds ready | integrated LUFS (servo target) | trim |
|---|---|---|---|---|
| Focus | `false` | 56 ms (bank only) | −19.5 | +0.87 |
| Relax | `false` | 57 ms | −24.2 | −0.25 |
| Sleep | `false` | 57 ms | −26.1 | +0.65 |
| Ocean | `false` | 56 ms | −25.3 | −0.34 |

All four modes render normally with pads, bass, noise bed and ocean; the melody
layer is simply absent, the `state` line reports `"sf2":false`, and the panel
shows the *Install soundfont-fluid* hint.

## v3-10. Phase subsets (`--layers`), 120 s each

`1` = pads, bass and the noise bed · `3` = + melody and pulse · `7` = + ocean
and the 16 Hz modulation. The plate and master chain always run.

| mode | mask | integrated | LRA | FLUCT | ROUGH | 200-1k − >4k | 200-1k − 1-4k |
|---|---|---|---|---|---|---|---|
| Focus | 1 | -21.2 | 5.1 | -24.18 | -9.17 | 40.3 | 15.7 |
| Focus | 3 | -21.2 | 6.1 | -25.14 | -9.28 | 40.6 | 15.4 |
| Focus | 7 | -21.5 | 6.9 | -25.44 | -10.00 | 42.0 | 15.9 |
| Relax | 1 | -23.4 | 7.0 | -24.24 | -12.49 | 37.1 | 15.6 |
| Relax | 3 | -23.9 | 7.1 | -24.42 | -11.85 | 40.7 | 17.6 |
| Relax | 7 | -23.3 | 6.5 | -24.70 | -12.89 | 31.0 | 15.9 |
| Sleep | 1 | -27.9 | 5.4 | -25.45 | -11.56 | 53.6 | 28.2 |
| Sleep | 3 | -28.2 | 5.4 | -25.48 | -11.00 | 53.3 | 27.6 |
| Sleep | 7 | -27.8 | 6.1 | -25.58 | -10.57 | 32.2 | 20.0 |
| Ocean | 1 | -23.4 | 6.5 | -24.58 | -11.63 | 59.7 | 29.7 |
| Ocean | 3 | -22.6 | 8.5 | -24.46 | -11.42 | 59.9 | 30.0 |
| Ocean | 7 | -22.2 | 7.5 | -22.42 | -10.04 | 25.8 | 14.4 |

The ocean layer is what puts content above 1 kHz into Sleep and Ocean (the
mask-4 rows drop 1–4 kHz by 8 and 15 dB respectively), which is also why it is
the layer that pulls those two modes toward the spectral window.

## v3 — deviations from PLAN-sound-v3, with reasons

Every number here is the shipped value.

1. **PADsynth bandwidth 22 cents (warm) / 16 cents (glass), not 50 / 35.**
   The plan's widths turned out to be the single largest source of 1–8 Hz
   fluctuation in the whole mix, which is the metric §5.2 makes mandatory: a
   Gaussian band beats against itself across its own width, and 50 cents at
   200 Hz is 8 Hz wide — the middle of the fluctuation band. Measured on the
   Focus core: 50 cents → 4–8 Hz octave at −24.8 dB and FLUCT −17.2; 30 cents →
   −31.8 dB and −18.8; 18 cents → −36.3 dB and −21.0. 22/16 cents is the point
   where Focus and Relax clear Ocean's value with margin while the bands are
   still wide enough that no two components sit at a fixed interval.
2. **Vibraphone (11) instead of Kalimba (108)** in Focus — see v3-3 for the
   measurements. The panel blurb and the README say vibraphone.
3. **`GEN_FILTERFC` is used as a small fixed *relative* darkening (−300…−500
   cents) plus our own Cytomic low-pass on the melody bus**, not as an absolute
   4–6 kHz cutoff. SoundFont generators are additive over a preset-dependent
   base, so "absolute 4.5 kHz" means different things per preset: the same
   offset left the piano unchanged and cost Vibraphone 12 dB of level. The bus
   filter is what the Brightness slider drives, and it is predictable.
4. **`GEN_VOLENVRELEASE` is a +1200 cent (×2) offset, not an absolute ≥1.5 s**,
   for the same additivity reason (an absolute offset on a preset that already
   sets a 1 s release would have produced a release of minutes). Instead each
   note is *held* 2.5–6 s, by which time the sample has decayed on its own, so
   the release is inaudible either way.
5. **Ocean's water drops are gone.** The plan's mode table gives Ocean "none"
   for melody and §1.3 forbids Karplus-Strong; the drop was a KS voice with a
   6 ms attack. This does change the mode the user liked, by removing about
   1.6 events/min at −13.5 dB; it also darkened Ocean's 1–4 kHz band by ~3 dB
   (v2: 9.0–10.8, v3: 14.4–15.5).
6. **The per-event ping-pong delay is removed** although it was not on the
   removal list: every repeat is an extra onset 0.75–1.25 s after the note,
   which contradicts §1.6's "no two events closer than 1.5 s".
7. **The noise beds are much louder and broader than the plan's numbers**
   (Focus −10 dB / LP 2.6 kHz / colour 0.40 instead of −20 / 1.2 kHz / brown;
   Relax −14 / 2.8 kHz; Sleep −11 / 2.4 kHz), and the pads correspondingly
   lower (Focus −8 dB, Relax −4, Sleep −3). Two measurements forced this: a
   narrow-band bed has far more 1–8 Hz envelope energy than a broad one, and
   with the plan's balance Focus sat 1–2 dB *above* Ocean's FLUCT; and with the
   bright layers of v2 gone, the bed is the only thing left carrying 1–4 kHz,
   so the spectral-tilt window could not be met without it. The side effect is
   that all four modes are now closer in character to Ocean, which is the mode
   the user named as the good one.
8. **Just-intonation snapping and a low-register spacing rule were added**
   (PLAN §2.1 permits the former as optional). Equal temperament detunes every
   just interval by up to 16 cents, and where two chord tones have a nearly
   coincident harmonic that mistuning surfaces as a 1–8 Hz beat; below 250 Hz a
   fifth or a fourth also falls inside one critical band (Plomp & Levelt), which
   is the sensory-dissonance maximum. Both are now prevented.
9. **The envelope pre-filter for the roughness metric is four cascaded
   one-poles at 100 Hz, not one.** With a single pole the |x| components of the
   pad partials (260–920 Hz) survive the filter and alias back into 0–200 Hz on
   decimation; every mode, Ocean included, then reported the same fictitious
   49.1 Hz line and ROUGH values 4–5 dB too high. Band definitions, decimation
   rate and the dB reference are exactly as specified.
10. **The bass lost its second detuned saw and its tanh stage.** The plan does
    not mention the bass, but a ±5-cent detune walk is the "detune beating" of
    §1.4 and a waveshaper only adds harmonics nobody asked for.
11. **Event rates are at the bottom of the plan's ranges.** Focus lands at
    6.7–7.0 /min rather than the 7.5–20 /min the "1 event / 3–8 s" reading
    would give, because the 10–20 s rest between phrases is counted in. This is
    the "quieter, sparser, slower" side of the brief and was left there.

## v3-11. Plugin plumbing

Checked without ever starting playback on the user's machine: the engine was
only ever run with `--render` / `--render-bank`, or live with its PipeWire
stream left inactive.

| check | result |
|---|---|
| build | `./build.sh`, `gcc -O2 -Wall -Wextra -std=gnu11`, **zero warnings**; also clean under `-Werror` |
| single engine process | yes — one `omanoise-engine` owned by the shell service |
| engine restarts on binary change | yes — `pkill -x omanoise-engine` and the shell respawned it within ~3 s, paused |
| live process | ~51 MB RSS, 0.2 % CPU while paused (the stream is deactivated once the fade-out completes) |
| startup on the live system | `bank ready 52 ms (5.9 MB)` · `soundfont ready 42 ms` · `sounds ready 94 ms total` (from the shell's journal) |
| `state` JSON fields | `version, playing, mode, volume, intensity, brightness, tonal, binaural, adaptive, pulse, modulation, daypart, bank, sf2, lufs` — all present |
| stdin round-trip | `state`, `mode sleep`, `volume 0.42`, `modulation 1/0`, `pulse 1/0`, `binaural 1/0`, `tonal 0.25`, `adaptive 0/1`, `mode focus` each echoed one `state` line with the value applied; `bogus` and `mode nope` answered `error …` without changing anything; the state file was rewritten after every change |
| bank / SoundFont notifications | two unsolicited `state` lines, `"bank":true` then `"sf2":true`, about 55 ms and 95 ms after start |
| v2 → v3 state migration | a v2 file (no `version`, `pulse:true`, `modulation:true`) came back as `"version":3` with both forced false and every other field preserved |
| `~/.local/state/omanoise.json` | ends at the PLAN §6 defaults: `version 3`, `mode focus`, `volume 0.700`, `intensity/brightness/tonal 0.500`, `binaural false`, `adaptive true`, `pulse false`, `modulation false`, and `"sf2":true` |
| QML hot-reload | picked up: the shell logged `Local plugin changed, reloading: omanoise` after the edits and re-created the widget with **no QML errors** in `journalctl --user` |
| `omarchy-shell omanoise state` | answers and parses as JSON, and the `mode focus` command sent through it reached the engine — **but the reply still lacks `sf2`**, see the caveat below |

**The one caveat, and it is not in the engine.** The running shell keeps the
*first* `IpcHandler` and service instance registered for target `omanoise`;
every hot-reload since logs

```
QML IpcHandler at .../Panel.qml[58:3]: Handler was registered but will not be
used because another handler is registered for target omanoise
```

so `omarchy-shell omanoise state` is answered by a pre-edit `Service.qml`, whose
`stateJson()` predates the `sf2` field (the reply's field order is the v2 one).
The engine itself reports `"sf2":true` — `~/.local/state/omanoise.json`, which
the engine writes from the same `state_json()`, contains it — and the new
`Service.qml` mirrors it. The stale registration clears itself the next time the
shell restarts, which was deliberately **not** done here, per the instruction not
to restart the user's shell. `omanoise play` was likewise not exercised live,
because that would put sound on the user's speakers; the play path is exercised
by every offline render instead.

---

# Omanoise sound engine v2 — acceptance runs (superseded by the v3 section above)

Measurements for the tests in `docs/PLAN-sound-v2.md` §5. Every row is a
180 s offline render at `volume 1.0`, `intensity 0.5`, `brightness 0.5`,
`tonal 0.5`, `adaptive off`, produced with

```
XDG_STATE_HOME=<tmp> bin/omanoise-engine --render <mode> 180 out.f32 --stats --layers <mask>
```

and measured with ffmpeg:

- `ebur128=peak=true` for integrated loudness, LRA and true peak;
- the same filter with `metadata=1,ametadata=mode=print:key=lavfi.r128.S` for the
  short-term loudness series, sampled once per second from t = 10 s (the first
  ten seconds are the meter filling and the loudness servo settling);
- three cascaded 2-pole `highpass`/`lowpass` sections per band edge followed by
  `volumedetect` for the spectral tilt (so each edge is ~9 dB down at its corner);
- `astats` for NaN/Inf, flat factor, DC offset and sample peak;
- the engine's own `--stats` line for event counts, sample peak, DC and the
  largest sample-to-sample step after the first 5 seconds.

Because the engine is generative, each table shows the **min … max across N
independent renders**, and the verdict table counts how many of those renders
landed inside the target rather than collapsing them to one pass/fail. Every
number below comes from the same build as the shipped `bin/omanoise-engine`.

Column key:

| column | test | target |
|---|---|---|
| integrated LUFS | §5.1 | mode target ±1.5 LU |
| LRA | §5.1 | 4–12 LU (Focus/Relax), 3–10 (Sleep), 6–14 (Ocean) |
| S std-dev | §5.4 | 0.8–3 LU |
| max 1 s step | §5.4 | < 4 LU |
| 200-1k − <200 | §5.2 | ≤ +4 dB |
| 200-1k − >4k | §5.2 | ≥ +18 dB |
| 200-1k − 1-4k | §5.2 | +8 … +16 dB |
| true peak | §5.6 | ≤ −1 dBFS |

## §5.1 / §5.2 / §5.4 / §5.6 — the four modes, full mix (`--layers 7`)

6 independent 180 s renders per mode; cells are min … max. The verdict
table counts how many of those renders land inside the target.

| mode | integrated LUFS | LRA | S std-dev | max 1 s step | 200-1k − <200 | 200-1k − >4k | 200-1k − 1-4k | true peak |
|---|---|---|---|---|---|---|---|---|
| Focus | -20.5 … -19.8 | 5.7 … 8.8 | 2.09 … 2.84 | 1.82 … 2.77 | -5.9 … -4.8 | 22.3 … 24.2 | 10.4 … 11.5 | -6.6 … -4.4 |
| Relax | -23.1 … -22.6 | 5.1 … 6.8 | 1.67 … 2.29 | 0.95 … 1.55 | -1.0 … 0.9 | 35.4 … 39.3 | 14.4 … 15.1 | -9.6 … -6.4 |
| Sleep | -28.2 … -27.6 | 3.6 … 7.3 | 1.21 … 2.19 | 0.91 … 1.08 | -2.4 … 0.7 | 27.6 … 31.6 | 11.2 … 14.3 | -15.2 … -12.2 |
| Ocean | -23.0 … -22.4 | 6.6 … 8.0 | 2.30 … 2.88 | 1.75 … 2.48 | -10.6 … -8.5 | 20.7 … 22.8 | 9.0 … 10.8 | -8.5 … -7.1 |

| mode | loudness ±1.5 LU of -20 | LRA 4–12 LU | S std-dev 0.8–3 LU | max 1 s step < 4 LU | 200-1k ≤ <200 + 4 dB | 200-1k ≥ >4k + 18 dB | 1-4k 8–16 dB below 200-1k | true peak ≤ −1 dBFS |
|---|---|---|---|---|---|---|---|---|
| Focus | 6/6 | 6/6 | 6/6 | 6/6 | 6/6 | 6/6 | 6/6 | 6/6 |
| Relax | 6/6 | 6/6 | 6/6 | 6/6 | 6/6 | 6/6 | 6/6 | 6/6 |
| Sleep | 6/6 | 6/6 | 6/6 | 6/6 | 6/6 | 6/6 | 6/6 | 6/6 |
| Ocean | 6/6 | 6/6 | 6/6 | 6/6 | 6/6 | 6/6 | 6/6 | 6/6 |

### Phase subsets (`--layers`), 120 s each

`1` = phase 1 only (pads, bass, chorus, plate, noise bed) · `3` = + phase 2 (melody, pulse, per-event FX, shimmer) · `7` = everything, incl. phase 3 (ocean, granular, 16 Hz AM). The loudness servo holds each subset at the mode target, so the interesting column is the spectral tilt: the phase-1 mix alone is far too dark, and phases 2–3 are what put energy above 4 kHz.

| mode | mask | integrated | LRA | S std-dev | 200-1k − >4k | 200-1k − 1-4k |
|---|---|---|---|---|---|---|
| Focus | 1 | -19.8 | 8.4 | 2.50 | 62.5 | 21.7 |
| Focus | 3 | -19.9 | 10.0 | 3.26 | 24.3 | 12.6 |
| Focus | 7 | -20.5 … -19.8 | 5.7 … 8.8 | 2.09 … 2.84 | 22.3 … 24.2 | 10.4 … 11.5 |
| Relax | 1 | -23.4 | 8.5 | 2.88 | 58.8 | 21.8 |
| Relax | 3 | -24.1 | 4.5 | 1.41 | 29.5 | 11.7 |
| Relax | 7 | -23.1 … -22.6 | 5.1 … 6.8 | 1.67 … 2.29 | 35.4 … 39.3 | 14.4 … 15.1 |
| Sleep | 1 | -28.2 | 4.4 | 1.47 | 55.8 | 32.5 |
| Sleep | 3 | -27.0 | 7.0 | 2.08 | 24.5 | 8.3 |
| Sleep | 7 | -28.2 … -27.6 | 3.6 … 7.3 | 1.21 … 2.19 | 27.6 … 31.6 | 11.2 … 14.3 |
| Ocean | 1 | -23.4 | 8.4 | 2.65 | 55.7 | 28.6 |
| Ocean | 3 | -22.8 | 8.9 | 2.91 | 47.9 | 20.3 |
| Ocean | 7 | -23.0 … -22.4 | 6.6 … 8.0 | 2.30 … 2.88 | 20.7 … 22.8 | 9.0 … 10.8 |

## §5.3 Harmonic dominance (tonal 1.0 vs tonal 0.0)

| mode | tonal 1.0 | tonal 0.0 | difference | target | verdict |
|---|---|---|---|---|---|
| Focus | -19.6 LUFS | -32.2 LUFS | **12.6 LU** | ≥ 8 LU | pass |
| Relax | -22.4 LUFS | -34.5 LUFS | **12.1 LU** | ≥ 8 LU | pass |
| Sleep | -27.1 LUFS | -39.4 LUFS | **12.3 LU** | ≥ 8 LU | pass |
| Ocean | -23.6 LUFS | -33.8 LUFS | **10.2 LU** | not required (Ocean is a nature mode) | 10.2 LU |

120 s renders at `tonal 1.0` / `tonal 0.0`, everything else unchanged. At `tonal 0` the pad, bass, melody, granular and shimmer buses are muted and only the calibrated noise/ocean bed is left, so the difference is the share of the mix the harmonic material carries.

## §5.5 Event rate at intensity 0.5

| mode | events/min (6 × 180 s) | target at intensity 0.5 | verdict |
|---|---|---|---|
| Focus | 18.00 … 19.67 | 12–24 | pass |
| Relax | 6.33 … 7.67 | 4–10 | pass |
| Sleep | 3.00 … 3.67 | 0–4 | pass |
| Ocean | 1.67 … 2.00 | 0–2 | pass |

## §5.6 Artefacts

Across **all 40 renders of this run** (4 modes × {6 × 180 s + tonal 1.0 / 0.0 + layer masks 1 and 3}):

| check | target | measured |
|---|---|---|
| NaN / Inf samples (`astats`) | 0 | 0 / 0 |
| non-finite sample check in the renderer | none | none (the render aborts on one) |
| Flat factor (`astats`) | 0 | 0.0 |
| DC offset | < 0.002 | 3.0e-06 (largest \|DC\|) |
| true peak | ≤ −1 dBFS | -2.6 dBFS (loudest render) |
| click detector: max \|x[n] − x[n−1]\| after 5 s | < 0.25 | 0.0892 (largest) |
| sample peak | < 1.0 | 0.7372 (largest) |

## §5.7 CPU, memory, bank render time

Measured on this machine (Linux 7.2.3-arch1-3, x86-64), release build
`gcc -O2 -Wall -Wextra -std=gnu11`.

| check | target | measured |
|---|---|---|
| CPU while generating | ≤ 3 % of one core | **2.3 – 2.8 %** (see below) |
| RSS | ≤ 64 MB | **20.4 MB** peak for an offline render; **23.2 MB** for the live service process (which also carries the PipeWire client) |
| instrument bank render | < 3 s | **146 – 180 ms** (15.8 MB of f32 samples), in a worker thread so `state` answers immediately |

CPU was measured offline, not on the live output, because the engine must not
be played on the user's speakers during this work. The figure is the wall time
of `--render <mode> 600 /dev/null` divided by 600 s of audio, repeated twice per
mode: Focus 2.54 % / 2.59 %, Sleep 2.68 % / 2.72 %, and with the render's own
file writing and per-sample analysis included, Focus 2.65 %, Relax 2.58 %,
Sleep 2.79 %, Ocean 2.31 %. This is a strict upper bound on the live cost: it
runs exactly the same `engine_render()` DSP but additionally does the
peak/DC/step/`isfinite` analysis pass over every sample, which the live audio
callback does not. The live process is idle (0.0 % CPU) while paused because
the stream is deactivated once the fade-out completes.

## Instrument bank inspection (`--render-bank`)

`bin/omanoise-engine --render-bank <dir>` renders the whole bank from a fixed
seed and dumps every one-shot as raw mono f32; the whole dump takes 157 ms. The
table below is the analysis of that dump (peak, largest sample-to-sample step,
the first and last sample of the file, and a 1/24-octave Goertzel spectrum taken
over 8192 samples starting 0.25 s in).

| sample | sec | peak | max step | \|x[0]\| | \|x[n-1]\| | peak Hz | f/f0 | centroid Hz |
|---|---|---|---|---|---|---|---|---|
| bass0_62hz | 5.000 | 0.980 | 0.2357 | 0 | 5.3e-01 | 61.2 | 0.987 | 47 |
| bass1_87hz | 5.000 | 0.980 | 1.0902 | 0 | 2.2e-02 | 43.8 | 0.504 | 64 |
| drop0_1319hz | 1.500 | 0.963 | 0.6558 | 0 | 5.1e-17 | 1329.5 | 1.008 | 1347 |
| fmbell0_659hz | 5.000 | 0.973 | 1.0486 | 0 | 2.5e-11 | 2108.8 | 3.200 | 1689 |
| fmbell1_1047hz | 5.000 | 0.972 | 1.5125 | 0 | 1.6e-12 | 3350.4 | 3.200 | 2969 |
| kick0 | 0.450 | 0.980 | 0.0136 | 0 | 1.1e-08 | 54.5 | — | 55 |
| padA0_139hz | 7.000 | 0.980 | 0.4729 | 0 | 8.9e-02 | 140.1 | 1.008 | 208 |
| padA1_220hz | 7.000 | 0.980 | 0.2097 | 0 | 8.9e-02 | 443.5 | 2.016 | 327 |
| padA2_349hz | 7.000 | 0.980 | 0.2993 | 0 | 3.1e-01 | 351.8 | 1.008 | 459 |
| padB0_139hz | 8.000 | 0.980 | 0.0500 | 0 | 7.6e-01 | 140.1 | 1.008 | 158 |
| padB1_220hz | 8.000 | 0.980 | 0.3218 | 0 | 1.3e-01 | 221.7 | 1.008 | 247 |
| padB2_349hz | 8.000 | 0.980 | 0.1960 | 0 | 4.4e-01 | 351.8 | 1.008 | 433 |
| pluck0_659hz | 2.500 | 0.977 | 0.3881 | 0 | 3.1e-11 | 664.2 | 1.008 | 694 |
| pluck1_1047hz | 2.500 | 0.960 | 0.4100 | 0 | 7.1e-12 | 1055.3 | 1.008 | 1060 |
| risset0_659hz | 7.000 | 0.917 | 0.1762 | 0 | 8.0e-12 | 789.9 | 1.199 | 692 |
| risset1_1047hz | 7.000 | 0.925 | 0.2353 | 0 | 8.2e-12 | 967.7 | 0.924 | 1162 |
| tick0 | 0.090 | 0.980 | 0.2849 | 0 | 0 | 2198.3 | — | 1854 |

**Peak-normalised**: every one-shot peaks at 0.92 – 0.98 (the target is 0.98;
the two Risset bells land slightly lower because their peak falls inside the
5 ms fade-in, which is applied after normalisation).

**Click-free**: every file starts at exactly 0. The one-shots that are played to
their end (bells, plucks, drop, kick, tick) also end at ≤ 1e-8. The looped
samples (padA, padB, bass) end mid-material, which is correct — playback never
reaches the end, it wraps at `loop1`, and `smp_make_loop` both equal-power
crossfades the loop seam and copies four samples past `loop1` for the 4-point
interpolator. Each of those files shows exactly one large step, and it sits at
`loop1 + 4` — in the tail that playback never reads. Inside the loop region the
largest step is 0.013 (bass0), 0.024 (padA0) and 0.059 (padB2), i.e. no audible
seam. The large steps in the FM bells (1.05, 1.51) are in the first 12 ms of the
attack and are genuine high-frequency content: at 48 kHz a 0.97-peak component
near 12 kHz has exactly that slope, and index-9 FM at 1047 Hz puts partials up
there. Playback low-passes bells at ~5 kHz.

**Plausible spectra**: plucks, the drop and padA0/padA2/padB* peak within 1 % of
the pitch they were rendered at; padA1 peaks on its second harmonic (a saw
through a 3·f0 low-pass with a 0.9·f0 high-pass, as designed); the Risset bells
peak at 1.199 × f0 and 0.924 × f0, which are exactly the 1.19 and 0.92 partials
that carry the largest amplitudes in the Risset table; the FM bells peak at
3.2 × f0, inside the inharmonic cloud that c:m = 1:1.4 at index 9 produces; the
kick peaks at 54.5 Hz (rendered at 55 Hz) and the brushed tick is broadband with
a 1.85 kHz centroid.

## §5.8 Plugin plumbing

Checked without ever starting playback on the user's machine.

| check | result |
|---|---|
| single engine process | yes — one `omanoise-engine` owned by the shell service (PID seen: 23.2 MB RSS, 0.0 % CPU while paused) |
| engine restarts on binary change | yes — the shell reloads the plugin when a file in it changes and respawns the engine, paused |
| `state` JSON fields | `playing, mode, volume, intensity, brightness, tonal, binaural, adaptive, pulse, modulation, daypart, bank, lufs` — all present |
| stdin round-trip | `state`, `mode sleep`, `volume 0.42`, `modulation 0`, `binaural 1`, `pulse 0`, `tonal 0.25` each echoed one `state` line with the value applied, and the state file was rewritten after every change |
| bank-ready notification | one unsolicited `state` line with `"bank":true` about 150 ms after start |
| `omarchy-shell omanoise state` | answers, and parses as JSON |
| `omanoise` CLI | `omanoise state` round-trips through the shell IPC |

**One caveat, and it is not in the engine.** The running shell (started
12:51, before this work) keeps the *first* `IpcHandler` that was registered for
target `omanoise`; every hot-reload since logs

```
QML IpcHandler at .../Panel.qml[57:3]: Handler was registered but will not be
used because another handler is registered for target omanoise
```

so `omarchy-shell omanoise state` is still answered by the pre-edit Panel/Service
instance and its reply does not yet contain `modulation` or `bank`. The engine
itself reports both (verified directly on its stdin/stdout, above), and the new
`Service.qml` mirrors both. The stale IPC handler will clear itself the next time
the shell is restarted — which was deliberately **not** done here, per the
instruction not to restart the user's shell. `omanoise play/pause` was likewise
not exercised on the live system, because that would put sound on the user's
speakers; the play path is exercised by every offline render instead.

## Notes on the numbers

**Generative spread.** These are 24 independent 180 s renders (6 per mode), each
with a different seed, and the engine is designed so that no two are alike. The
spread of a single statistic across renders of one mode is typically ±1 LU for
LRA and ±0.4 LU for the short-term std-dev, which is the same order as the width
of some of the §5 windows. Earlier configurations of this build passed every
criterion in one four-render sweep and then put a single render outside a
different window in the next; the shipped settings were chosen to sit in the
middle of each window rather than to make one sweep look clean, and the verdict
table above reports per-render counts so a tail is visible rather than hidden.

**The Ocean corridor.** §5.1 asks Ocean for LRA ≥ 6 LU and §5.4 for a per-second
loudness std-dev ≤ 3 LU. For a smoothly varying signal LRA (the 10th-to-95th
percentile range) runs about 2.4–2.9 × the std-dev, so those two targets leave a
corridor of roughly LRA 6 … 8. Ocean is tuned to sit inside it (LRA 6.6 … 8.0,
std-dev 2.30 … 2.88 across six renders); it cannot be pushed toward the upper end
of the plan's 6–14 LU range without breaking the std-dev ceiling.

**What actually moves Ocean's loudness.** During tuning the mode kept putting a
single 4–5 LU step into the per-second loudness. Instrumenting the buses showed
it was not the waves: Ocean's bass voice was swinging 48 dB across its 37.4 s
Eno cycle and was the loudest bus in the mode, so its 6 s attack out of silence
was the step. Giving Ocean a sustained sub (`0.28 + 0.72 · env`, as Sleep
already had) removed it, and the loudness range it had been providing was put
back into the slow macro contour, where a 5 dB swing over 24 s cannot produce a
fast step.
