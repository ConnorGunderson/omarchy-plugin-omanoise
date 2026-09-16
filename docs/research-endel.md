# Research track 1: How Endel actually produces its sound

(Research agent report, 2026-09-16. [P] = primary source (Endel staff/docs/patents/paper), [S] = secondary reporting, [I] = inference.)

## 1. Real-time synthesis vs recombined stems

**Verdict [P]: Endel is a real-time, on-device sample sequencer/mixer, not a synthesizer.**
- CEO Oleg Stavitsky (TechCrunch 2022): "The soundscapes are stem-based… snippets of sounds, think of them as samples." The app "procedurally generates a soundscape in real time on the device"; "a few AI systems on top of that sequencer… generate melodies." https://techcrunch.com/2022/05/20/endel-sony-partnership/
- Stavitsky (vc.ru 2019): "a huge library of samples — small sounds created by our composer… labels them… The algorithm selects the samples that suit the person… everything is glued together and audio effects are applied."
- Help center: "Endel Pacific consists of sound layers, modulations, and effects… An algorithm… generates an appropriate soundscape by constructing unique combinations of Endel Pacific's components." https://endel.zendesk.com/hc/en-us/articles/360011955640-Personal-Inputs
- Evgrafov (Colta): "Endel generates sound by working with loops, phasing, lag/offset, and randomization… references: Steve Reich, Terry Riley, Laurie Spiegel and, of course, Brian Eno."

**Patents (US 11,275,350 → US 2022/0365504 → US 12,248,289 B2, Endel Sound GmbH)** define four sound sources feeding a real-time mixer:
1. **Note-sequence files** — pre-composed scores of 10–20 s, up to 40 notes, chosen by intensity level.
2. **Note generator** — Markov chain (next note depends on previous) or custom stochastic generator with "which notes can be played after each particular note" rules.
3. **Single-note library** — audio files chromatically mapped to instruments (a sampler); "notes at the lower end… more soothing."
4. **Sample library** — nature, white noise, vocals, instrument sounds, up to minutes long.
Hierarchy: **layers → sections (multiples of 16 beats) → phases (minutes–hours) → soundscape**. Fig. 9: six tracks — melody (piano-roll notes), chords (Markov), bass (custom generator), voice notes, FX notes, percussion/ambient. Mixer rules keep layers from clashing in tempo and intensity. https://patents.google.com/patent/US12248289B2/en

**Stem multiplier patent (US 2025/0210017 A1)** — the offline sound-pack pipeline: stems (5–15 s or full-length) tagged with intensity (low/mod/high), type (pad, percussion, voice, beat), mood, BPM, duration. Engine generates 10–15 variants per stem via: **octave shift (wet), granular synthesis (grain duration, spread, attack, release, density, count), stereo delay (L/R, feedback, wet, probability), reverb (decay, size, high-cut, wet, probability)**, plus a tone-shaping ML model; designers rate variants. https://patents.google.com/patent/US20250210017A1/en

**Artist collaborations confirm** stems of chords/vocals (Grimes), "sounds and effect chains… attributes in how they might play, which things worked with what" (Hawtin), "isolated, almost static sounds… the most minimal stems" (Leaving Records). Melody "AI" is offline: Evgrafov fed 100 hand-picked melodies to a network for 100 more and threw half away as "unmusical garbage".

**Instrument palette [P]** (Evgrafov, Recovery mode): guzheng and charango base, mixed with harp, reeds, and piano "broken into many pieces… a sound bath", plus vocals; inspired by Harold Budd. The Sleep bell trigger "is the only static sound in Endel."

## 2. Musical rules

- **Scale [P]**: "The melody is played only on black keys. Adjacent semitones that could produce dissonance are not used." "We use pentatonic everywhere." Favors octave 2:1 and fifth 3:2 ratios, "mellow tones, slow chord changes, simple structures." Tuning A=440. Patent: "Notes of extremely differing pitches… are not sequenced together to provide relaxing sounds; such contrasts can be useful to energize."
- **Harmony [S]**: Focus = "shimmering synth pads and subdued drum machines swell and fade as the harmony gently meanders from chord to chord in entirely diatonic movements." Chords from a Markov timeline. [I] slow diatonic/pentatonic chord drift, no functional cadences.
- **Tempo [P]**: baseline **55–70 BPM**, up to 100 to energize; small HR changes subtly change tempo; Move spans 60–170 BPM; "people like stability and to be able to expect the changes"; new elements only after the listener has been in a state for a while.
- **Circadian/environment [P]**: energy phases change every 20 min; ~110-min ultradian loops; light → intensity; weather → layer variations; steps → intensity/complexity. During circadian lows Focus is "not as intense and driving."
- **Mode differences [P]**:
  - **Relax**: "You won't hear beats or complex sound textures — simple sounds that are easy to process." Long reverb.
  - **Focus**: "percussions purposely harmonized with the heartbeat"; "more active, less reverb, more nuanced… the brain starts to block out rhythmic sounds after a time." Brown noise bed under the music. "A mixture of noise and musical properties."
  - **Sleep**: four phases: (1) static bell trigger; (2) ocean waves at ~12 cycles/min resembling sleep breathing; (3) waves cross-fade to pink noise; (4) after 45 min the main soundscape; "delta waves modulating noise". Patent template: Onset I 20 min, Onset II 20 min, body, outro 8 min; fewer layers in onset/outro.

## 3. Sound-design character

- **Listeners [S]**: "Warm pads swirled around me while subtle motifs weaved over the top like shooting stars"; Deep Work: "a slightly odd sounding kick drum thumping consistently underneath… five to six elements, heavily reverb-coated… water droplets or pentatonic piano"; elements "melt into a semi-solid liquid"; "a bassy beat at the core, synthy sounds around it… like a sound bath"; Sleep: "electronic tones over brown noise… faded to deep, slow bass". Genre: "intersection of New Age and ambient electronic." Some reviewers find modes "thin over long stretches" or "repetitive".
- **Effects [P]**: reverb (decay, size, high-cut, wet), stereo delay, granular, octave shift — each with a probability. Focus has *less* reverb than Relax/Sleep. [I] Multi-second low-passed reverb tails, granular smear on Relax/Sleep.
- **Spatial [P]**: CTO on Spatial Orbit: "where the sound would be and how it would move around you," non-intrusively.
- **Anti-repetition [P/I]**: loops + phasing + offset + randomization; Markov; 10–15 variants per stem; probabilistic FX.
- **Transitions [P]**: phase changes are condition-triggered; sections align to 16 beats; onset/outro fade; explicit cross-fades (waves→noise); small tempo steps; mic-triggered ducking on human voice.

## 4. Science claims — reality check

- **Binaural beats: not used in main soundscapes [P]**; only a separate "Binaural Beats" 40 Hz scenario.
- **No amplitude modulation** (Brain.fm's comparison confirms Endel does not use AM/entrainment). Sleep's "delta waves modulating noise" has no Hz figures.
- Pentatonic/440 Hz claims cite one 2018 CTM study and Lee Salk's 72-bpm heartbeat study; "pink noise 20% faster sleep" uncited.
- **Arctop EEG study (Frontiers 2021)**: N=51, Endel Focus > silence (p=0.008), Spotify/Apple playlists not significant overall; funded/co-designed by Endel; "7x" marketing figure not in the paper. https://www.frontiersin.org/journals/computational-neuroscience/articles/10.3389/fncom.2021.760561/full

## 5. Comparators

- **Brain.fm**: AM on each stereo channel; 16 Hz best (Woods 2019/2024). 
- **myNoise (Stéphane Pigeon) [P]**: 10 octave sliders as EQ, offline-processed so aligned sliders = pink; personal "grey noise" via hearing calibration; loops with incommensurate lengths ("repeats after 188,027,101 years"); Animate mode interpolates gains between Low (50%) and High (125%) profiles; all generators share tempo and key; "Floating" at 60 BPM "avoids rhythm and melody, using slowly evolving textures and warm low-frequency tones… independent sound layers that gradually come together and drift apart"; never uses generative AI. https://mynoise.net/Interviews/interview_reform.php

## Implications for a from-scratch C engine (10 things to imitate)

1. **Build a sample sequencer, not a synth**: short one-shots (single notes per instrument, chromatically mapped) plus long beds; "generation" = choosing, timing, pitching, mixing. [P]
2. **Pentatonic only, one key, A=440**; never adjacent semitones; prefer octaves/fifths; low-mid register for calm modes. [P]
3. **Markov chains / allowed-next-note tables**; 10–20 s phrases (≤40 notes) repeating with stochastic variation. [P]
4. **5–6 concurrent layers**: pad/drone, chords, bass, melody (piano/bell/plucked: guzheng/charango/harp-like), FX/texture (water droplets), optional light percussion, colored-noise bed (brown Focus, pink Sleep). [P/S]
5. **Tempo 55–70 BPM baseline**, small steps, 16-beat sections. [P]
6. **Mode profiles**: Relax = no beats, few layers, long reverb; Focus = quiet steady soft kick/pulse, less reverb, light percussion, audible noise bed; Sleep = phased (bell → 12-cpm wave breathing → crossfade to pink → sparse), thinning layers over 45 min. [P]
7. **Multiply stems**: 10–15 variants via octave shift, granular smear, stereo delay, big reverb, each with probability. [P]
8. **Defeat repetition the Eno way**: incommensurate loop lengths per layer, phase-offset restarts, randomized entry probability, pan drift; slow layer gain moves between low/high profiles (myNoise Animate). [P]
9. **Spectrum**: summed bed close to pink/brown, low-passed reverb tails, no sharp transients except a soft kick; limiter. [P/I]
10. **Macro structure by phases**: intro (few layers) → body → outro (fades), condition-triggered, section-aligned multi-second crossfades; one "intensity/complexity" knob. [P]
