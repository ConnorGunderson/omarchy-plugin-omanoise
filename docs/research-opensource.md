# Research track 4: open-source engines / instrument libraries for omanoise (2026-09-16)

User verdict on v2: Ocean (almost no tonal content) best, Relax (loudest bells, strongest shimmer, minor pentatonic, Risset inharmonic partials) worst → the hand-synthesized *musical* layer is the anxiety source, not the noise/nature beds.

## Complete generative engines — none is a drop-in
- **Generative.fm / pieces-alex-bainter** (MIT, JS, Tone.js): best-sounding FOSS generative ambient, but its calm comes from *sample choice* (VSCO2 CE, VCSL, Sonatina, singing bowls) + sparse scheduling. Headless only via node-web-audio-api; unmaintained; 2–4 days; heavy. https://github.com/generative-music/pieces-alex-bainter
- **musicbox** (Rust, CC BY-SA): pentatonic drones + KS + Dattorro — a twin of our current engine, same "synthy" problem. https://github.com/benaskins/musicbox
- **SuperCollider** (GPL-3, Arch extra/supercollider): headless scsynth over OSC, ~1.4 % CPU; you still write the sound design. Music for Airports snippet: https://sccode.org/1-5fq
- **Csound** (LGPL, Arch extra/csound): C API, has GENpadsynth. No ready ambient generator.
- **Pure Data / Farnell** patches (license unstated on aspress.co.uk/sd): libpd (BSD) or hvcc → C. https://github.com/Wasted-Audio/hvcc
- Sonic Pi / Strudel / Tidal / Orca: wrong shape (Ruby+Erlang, browser, or no sound).
- **Blanket** (GPL-3): CC0-licensed rain/waves/fire loops usable as nature material. https://github.com/rafaelmardojai/blanket/blob/master/SOUNDS_LICENSING.md

## Instrument engines to drive from our sequencer brain
- **libfluidsynth** (LGPL-2.1, C, Arch extra/fluidsynth): headless, `fluid_synth_noteon`/`write_float`, built-in reverb/chorus, per-channel CC. ~1 day to replace the voice layer. SoundFonts: **FluidR3_GM** (MIT, Arch extra/soundfont-fluid, ~140 MB, warm pianos/strings), **GeneralUser GS** (permissive, 31 MB, best-programmed small bank), **Salamander piano** (CC BY 3.0, AUR, ~1 GB), Splendid Grand (BSD-2, SFZ), SGM (CC BY 4.0). Avoid Arachno, Nice-Keys (license). GM "Pad" presets are weak → use PADsynth for pads.
- **sfizz** (BSD-2, C API, Arch extra/sfizz-lib): disk streaming, sinc resampling. Libraries: **VSCO2 CE** (CC0, harp/strings/winds), **VCSL** (CC0, bells/mallets/keys — closest to Endel's palette), Karoryfer (CC0), Iowa MIS (unrestricted), Sonatina (CC Sampling+). Gigabytes on disk; 1–2 days.
- **PADsynth** (Paul Nasca, public domain): harmonic profile → Gaussian bandwidth per harmonic → random phases → one IFFT → looped wavetable; near-zero runtime CPU; lush beating-free pads. C entry points: Soundpipe `gen_padsynth` (MIT, needs FFTW), Csound GENpadsynth, Zyn/Yoshimi headless. ~half a day to embed. https://en.wikibooks.org/wiki/ZynAddSubFX/PADsynth
- Surge XT (GPL, `surge-xt-cli` + OSC, CPU-heavy), Vital (headless undocumented), Dexed (FM — wrong palette): not recommended.
- **Faust** libraries (generated C under own license): zita_rev1, dattorro, pm.lib — good for effects/one-offs.
- **Soundpipe / sndkit** (MIT): best C "parts bin" (padsynth, zitarev, filters).

## Nature/noise
No calibrated open ocean/rain C library. Options: Farnell via hvcc; synthesized noise + slow AM (cheap-sounding); **CC0 recordings (Blanket/Freesound) with crossfades + gentle randomized filtering — least effort, most convincing.**

## Ranked recommendation
1. **Keep the sequencer; replace the voice layer with libfluidsynth + FluidR3/GeneralUser (piano, harp, bells) and add a PADsynth wavetable voice for pads.** ~2–3 days, one Arch dependency, <5 % CPU, stdin protocol unchanged.
2. Same with sfizz + VCSL/VSCO2 CE (better samples, cleaner licenses, GBs on disk) as a phase-2 upgrade.
3. scsynth over OSC — flexible but large runtime and does not solve the sample palette.

## What makes generated ambient anxious vs calming (evidence)
- **Roughness**: AM in 15–300 Hz (peak ~70 Hz) and **fluctuation strength peaking at ~4 Hz** are inputs to Zwicker's annoyance model → no tremolo/chorus/LFO near 4 Hz; no detune beats above ~15 Hz; randomize modulation so no periodicity forms.
- **Beating detune = sensory dissonance** (Plomp & Levelt): supersaw detune is the wrong tool; PADsynth's Gaussian partials avoid discrete beats.
- **Sharp transients**: startle from rise times <12 ms; 141–220 ms rise fully mitigates → attacks ≥150 ms on plucks/bells.
- **Texture**: relaxation correlates with thin texture, legato, no accentuation, soft dynamics, smooth melodic shape, piano/string timbres; exclude percussion (Grocke & Wigram).
- **Spectrum**: excess 4–20 kHz = harsh; low-mid mud and sub content raise unease; Endel uses *less* reverb than expected — long tails on everything smear into mud.
Translation: sampled acoustic voices, PADsynth beds, attack ≥150 ms everywhere, no LFO 1–300 Hz, ~one event per several seconds, wide consonant pentatonic voicing, high-passed reverb, nature layer from CC0 recordings.
