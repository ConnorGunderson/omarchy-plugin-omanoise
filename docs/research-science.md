# Research track 2: What the evidence says about sound for concentration, relaxation and sleep

(Research agent report, 2026-09-16. Primary sources prioritised; unverifiable folklore numbers flagged.)

## 1. Brain.fm: patents, papers, what they actually do

### Papers
**Woods, Hewett et al. 2024, Communications Biology** ("Rapid modulation in music supports attention in listeners with attentional difficulties") — https://pmc.ncbi.nlm.nih.gov/articles/PMC11499863/
- SART task. Exp 1 (N=83): AM+Music vs control music vs pink noise. Exp 2 (fMRI, N=34): AM+Music raised salience/executive/sensorimotor network activity. Exp 3 (EEG, N=40): stimulus–brain phase-locking at 8, 12, 14, 16, 24, 32 Hz; 16 Hz PLV increased over time.
- Exp 4 (N=175): **rates tested 8, 16, 32 Hz** = 16th/32nd/64th notes at 120 BPM, **aligned to the metrical grid**. **Only rate mattered, and only as an interaction with ADHD-symptom (ASRS) score**: high-ASRS listeners did best with 16 Hz. **Depth had no significant effect.**
- **How applied**: envelope multiplier restricted to the **200 Hz–1 kHz band** of the mix (to avoid salience at higher frequencies), not per instrument.
- Verdict: moderate for a real neural effect (strong EEG phase-locking), weak-to-moderate for behavioural benefit, concentrated in high-ADHD-symptom listeners.

**Woods et al. 2019 arXiv** (N=677): 16 Hz at higher depth best; neighbouring settings did not help. Depth finding did not survive peer review.

### Patents (Brainfm Inc)
- **US 7,674,224** (Hewett): split mix with band-pass/notch, modulate selected components (AM, FM, panning, filter cutoff via LFO), remix; disguise as tremolo/vibrato/reverb. Bands: beta 13–25 Hz (waking), alpha 8–12, theta 4–7, delta 0.2–4.
- **US 2020/0265827 A1**: rates tied to tempo (120 BPM → 2, 4, 8, 16 Hz). **Depth 50–75%; envelope ramps 25% → 75% for main period → 50% over the last 15 min.** Modulate cochlear regions where AM is less salient (**0–1.5 kHz and 8–20 kHz**), leave mids unmodulated; **phase-align the modulator to the rhythm**.
- **US 10,653,857** (sleep): **0.1–3 Hz AM** on the whole mix; "salience protocol": limit highs, loudness balancing, consistent melodic structure, smooth transitions, **no lyrics**; "variance protocol": slowly introduce novel elements to prevent habituation; optional spatial rotation 1–10 cycles/min; start at a higher modulation rate and descend to delta over the session.
- **US 11,957,467**: sensor-driven depth, beat detection to align modulator phase, modulation on broadband / sub-bands / stems via Hilbert envelope.

### Public design guidance (Kevin Woods, Engadget 2018)
Good focus music: "no vocals, no strong melodies, 'dark' spectrum, dense texture, minimal salient events, heavy spatialization, a steady pulse", modulation 10–20 Hz; production = "reduce the treble and minimize any other distractions." Brain.fm states modulation directly in each stereo channel is stronger than binaural beats.

## 2. Binaural beats (BB) vs monaural/AM

- Garcia-Argibay 2019 meta-analysis (22 studies): g=0.45 overall; anxiety g≈0.69; longer exposure better; noise masking unnecessary.
- Basu & Banerjee 2023: g=0.40 memory/attention but conflicting results for theta and beta.
- **Ingendoh 2023 PLOS ONE** (EEG review, 14 studies): 5 support entrainment, 8 contradict. Carriers 100–900 Hz. **All studies embedding BB in pink noise found no entrainment.**
- **Orozco Perez 2020 eNeuro**: both BB and monaural beats produce ASSRs; **monaural beats entrain more strongly**; neither changed mood.
- Melnichuk 2025 Sci Rep (N=80): only gamma + 340 Hz + white noise improved attention; no reduction of vigilance decrement.
- Mallik & Russo 2022 RCT (N=163): music alone reduced anxiety (d≈0.85); **BB alone did not beat pink noise**.

**Verdict: weak/mixed.** If you want a neural rhythm, **use amplitude modulation identical in both channels**, not binaural beats. If BB is offered: 340–400 Hz carrier, don't bury in noise, optional toggle, not a default.

## 3. Noise colour, level, masking

**Attention/ADHD**
- Söderlund 2007/2010: white noise (78 dB) helped inattentive children, hurt attentive ones (inverted-U, Moderate Brain Arousal model).
- **Nigg 2024 JAACAP meta-analysis** (13 studies): ADHD groups **g=0.249**; non-ADHD **g=−0.212**; no brown-noise studies exist.
- Verdict: small positive only for high-ADHD-symptom listeners; small negative for others. → Broadband noise bed is a user option, default low.

**Sleep (continuous noise)**
- Zhou 2012: steady pink noise increased "stable sleep" (no dB level in abstract; the ~60 dB figure online is unverified).
- **Riedy 2021 Sleep Med Rev** (38 studies): GRADE quality for continuous noise improving sleep **"very low, which contradicts its widespread use"**; may harm sleep and hearing.
- Closed-loop pink bursts (Ngo 2013: **50 ms pink, 5 ms ramps, 55 dB SPL** timed to slow-oscillation up-states; Papalambros 2017) boost SO/spindles — requires EEG, not implementable open-loop; open-loop overnight pink noise may impair sleep-dependent insight (Frontiers 2023).
- Verdict: weak for continuous noise as a sleep aid.

**Speech masking (offices)**
- Veitch 2002 (NRC): −5 dB/oct slope 125–8000 Hz; too loud above **48 dBA**; recommend **≈45 dBA**.
- Haapakangas 2011: at 45 dBA, pouring water beat music/noise in the lab; Hongisto 2017 18-week field study: **water maskers rated worse** than filtered noise ("rain on tin roof"). Renz 2018: speech-shaped stationary noise masks best.
- Verdict: moderate/strong for level ceiling and slope; make texture user-selectable.

## 4. Music features for concentration and low arousal

- **Mozart effect**: myth; what matters is arousal and mood (Pietschnig 2010, Thompson 2001).
- **Background music**: Kämpfe 2011 meta-analysis overall null; **lyrics are the reliable harm** (Perham & Currie 2014). Verdict: strong — no vocals.
- **Changing-state / irrelevant-sound effect** (Jones & Macken): streams whose successive elements differ acoustically disrupt serial memory; **steady-state or repetitive streams cause no impairment**. Strongest basis for "minimal salient events / no strong melody". Verdict: strong.
- **Tempo & heart rate**: Bernardi 2006: faster tempo raises HR/BP regardless of style; **pauses lowered HR below baseline**. "60–80 BPM matches resting HR" is a heuristic; no evidence of HR entrainment. Verdict: moderate that slow tempo lowers arousal; weak for any specific BPM.
- **Nature sounds**: Van Hedger 2019 (cognition, small), Alvarsson 2010 (faster stress recovery), Buxton 2021 PNAS (stress g=−0.60; water and birds most effective), 2024 meta-analysis (HR/BP/respiration better than quiet). Verdict: moderate for physiological relaxation.
- **ASMR**: responder-specific; not a default.

## 5. Sleep-specific
- Jespersen 2022 Cochrane (13 RCTs): moderate-certainty improvement in sleep quality.
- Dickson & Schubert: 60–80 BPM, soft, smooth, instrumental, simple; low frequencies, legato.
- **Scarratt 2023 PLOS ONE** (225,626 Spotify sleep tracks): vs general music **energy d=−1.46, loudness d=−1.25, acousticness +1.20, instrumentalness +1.10, tempo only −0.47**; biggest cluster is "ambient" drones.
- 2025 review: 50–60 dB playback, 30–45 min sessions.
- **Costa 2025** (N=22, PSG): very slow pentatonic sequences at **0.2 Hz (one note per 5 s)** shortened sleep-onset/N2/SWS latencies vs silence; 1 Hz less effective.
- Verdict: loudness and energy, not tempo, separate sleep music from other music; emerging support for ultra-slow tonal sequences; very low for continuous noise.

## 6. Hearing safety
- WHO/ITU H.870: **80 dBA for 40 h/week**; NIOSH 85 dBA 8 h. WHO night noise: bedroom ≤30 dB LAeq. Infant sound machines exceeded 85 dBA at 30 cm (Hugh 2014).
- Implication: target **45–60 dBA** for hours-long use, with output limiting and duration-aware auto-fade.

## Evidence-based design parameter table (prioritised)

| # | Parameter | Value | Evidence |
|---|---|---|---|
| 1 | Level ceiling / defaults | focus default 45–60 dBA; sleep 30–50 dBA fading lower; hard cap | Strong |
| 2 | No vocals / speech-like formant sweeps | zero | Strong |
| 3 | Steady-state texture, minimal salient events | sustained dense pads; slow onsets (>100s of ms); no sudden pans/transients/melodic leaps | Strong (changing-state effect) |
| 4 | Focus: amplitude modulation | 12–20 Hz (16 Hz best), sinusoidal, phase-locked to a beat grid (32nd notes @120 BPM); apply to 200 Hz–1 kHz (or ≤1.5 kHz + 8–20 kHz); depth ramp 25%→75%→50%; user-adjustable | Moderate neural, weak-moderate behavioural |
| 5 | Dark spectrum | roll off from ~4–6 kHz; brown/pink-leaning energy | Moderate |
| 6 | Masking noise bed | −5 dB/oct 125–8000 Hz, never above 48 dBA; user-selectable colour and level | Moderate offices; small ADHD |
| 7 | Relax: tempo & arousal | 60–80 BPM felt pulse or none; legato; pauses; low dynamic range; modal/soft | Moderate |
| 8 | Nature layer | water/birds low level, optional | Moderate physiology, mixed acceptance |
| 9 | Sleep: energy & loudness first | lowest energy/loudness, instrumental; tempo secondary | Moderate |
| 10 | Sleep: slow modulation / event rate | whole-mix AM 0.1–3 Hz descending; tonal events ~0.2 Hz | Emerging |
| 11 | Sleep: session envelope | 30–45 min active, then gradual fade | Moderate |
| 12 | Slow variance vs habituation | new timbres/voicings every few minutes with crossfades; spatial rotation 1–10 cycles/min | Weak (design rationale) |
| 13 | Binaural beats | do not rely on; optional toggle; prefer monaural AM | Weak/mixed |

**Bottom line**: the highest-confidence wins are unglamorous — safe low level, no words, steady-state texture with dark spectrum, slow dynamics. 16 Hz beat-locked AM on the low-mid band is the one "neuro" feature with peer-reviewed EEG and modest behavioural support. Binaural beats and continuous noise for sleep are folklore; optional toggles, not defaults.

## Key sources
- Woods et al. 2024: https://pmc.ncbi.nlm.nih.gov/articles/PMC11499863/ ; 2019 preprint https://arxiv.org/abs/1907.06909
- Brain.fm patents: https://patents.google.com/patent/US7674224B2/en , https://patents.google.com/patent/US20200265827A1/en , https://patents.google.com/patent/US10653857B2/en , https://patents.google.com/patent/US11957467
- Engadget 2018: https://www.engadget.com/2018-07-23-the-science-behind-beats-to-study-to.html
- Garcia-Argibay 2019: https://link.springer.com/article/10.1007/s00426-018-1066-8 ; Ingendoh 2023: https://journals.plos.org/plosone/article?id=10.1371/journal.pone.0286023 ; Orozco Perez 2020: https://www.eneuro.org/content/7/2/ENEURO.0232-19.2020 ; Melnichuk 2025: https://www.nature.com/articles/s41598-025-88517-z ; Mallik & Russo 2022: https://pmc.ncbi.nlm.nih.gov/articles/PMC8906590/
- Nigg 2024: https://pubmed.ncbi.nlm.nih.gov/38428577/ ; Söderlund 2010: https://pmc.ncbi.nlm.nih.gov/articles/PMC2955636/
- Riedy 2021: https://www.sciencedirect.com/science/article/abs/pii/S1087079220301283 ; Ngo 2013: https://pubmed.ncbi.nlm.nih.gov/23583623/
- Veitch 2002 NRC IR-846; Hongisto 2017: https://www.frontiersin.org/journals/psychology/articles/10.3389/fpsyg.2017.01177/full
- Kämpfe 2011: https://journals.sagepub.com/doi/abs/10.1177/0305735610376261 ; Bernardi 2006: https://pubmed.ncbi.nlm.nih.gov/16199412/
- Buxton 2021 PNAS: https://www.pnas.org/doi/10.1073/pnas.2013097118
- Scarratt 2023: https://journals.plos.org/plosone/article?id=10.1371/journal.pone.0278813 ; Costa 2025: https://journals.sagepub.com/doi/10.1177/03057356251359079 ; Cochrane 2022: https://www.cochranelibrary.com/cdsr/doi/10.1002/14651858.CD010459.pub3/full
- WHO safe listening: https://www.who.int/news-room/questions-and-answers/item/deafness-and-hearing-loss-safe-listening
