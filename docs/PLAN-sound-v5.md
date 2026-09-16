# Omanoise v5 — recorded environment sounds (fire, wind, stream, birds)

Status: approved by Josh. Implementer: Claude Opus. Builds on v4/v4.1 (`PLAN-sound-v4.md`, `ACCEPTANCE.md`, `PROGRESS.md`).

## 0. Why
User verdict on the synthesized fire (two attempts): "still doesn't sound like fire… like being on the phone with someone with bad internet in wind and rain. Can't we just take the audio of a fire?" Yes. Synthesizing fire convincingly is a known hard problem; a public-domain recording is the right tool. The user chose: fire + the CC0 extras (wind, stream, birds).

## 1. Assets (already downloaded and converted — do not re-download)
`sounds/*.wav` — 48 kHz, stereo, 16-bit PCM, converted with ffmpeg from Blanket's set; originals in `sounds/src/*.ogg`; licenses in `sounds/LICENSES.md` (fireplace = Public Domain; wind, stream, birds = CC0).

| file | length | integrated loudness of file |
|---|---|---|
| fireplace.wav | 25.5 s | −38.8 LUFS |
| wind.wav | 14.8 s | −27.6 LUFS |
| stream.wav | 145.5 s | −39.6 LUFS |
| birds.wav | 129.8 s | −28.0 LUFS |

## 2. Engine

### 2.1 Environment slots
Six slots: `ocean` (synth, unchanged), `rain` (synth v4.1, unchanged), `fire`, `wind`, `stream`, `birds` (recorded). **Delete the synthesized fire and wind generators** (env.c fire_t/wind_t and their control/audio code); rain keeps its struct. `ENV_N` = 6; `ENV_NAME` = ocean, rain, fire, wind, stream, birds. Defaults: ocean 0.8, all others 0.

### 2.2 Sample loader (bank worker thread, never the RT thread)
Minimal RIFF/WAVE parser: accept PCM 16-bit, 48 kHz, 1 or 2 channels (mono → both channels); reject anything else with a stderr warning and mark the slot unavailable. Path: `<pluginDir>/sounds/<slot>.wav` (slot names above; **if `ocean.wav` or `rain.wav` exist they replace the synthesized generator for that slot** — document this as "drop your own recording in"). Store as int16 interleaved (≈ 4.9 + 2.8 + 27.9 + 24.9 MB ≈ 60 MB → fits the 120 MB RSS budget; convert to float on read). Compute each file's RMS (or a K-weighted approximation) at load and derive a per-slot calibration gain so **level 1.0 alone sits at the mode's −23 LUFS target before the auto-trim** (replace the hard-coded `ENV_CAL_DB` entries for recorded slots with measured gains). Publish loaded slots to the RT thread via one atomic pointer swap; state JSON gains `"sounds":{"fire":true,…}` for the six slots (synth slots always true).

### 2.3 Playback: random-segment scheduling with equal-power crossfades
A plain loop of a 25 s fire would be recognisably periodic. Per recorded slot, run two read heads. Each head plays a **randomly chosen segment** of the file (length 6–14 s for files < 40 s, 20–45 s for long files; start uniformly random, never within 1 s of the file ends), then the other head starts a new random segment while the finishing one fades out: **equal-power (sin/cos) crossfade of 2.5 s** for fire/wind, 4 s for stream/birds. Consecutive segments must not overlap in file position by more than 30 %. Heads read at speed 1.0 only (no pitch change: birds and water at altered speed sound wrong). Per slot: HP 40 Hz, gentle LP with Brightness (centre 9 kHz, ±0.7 oct), and a very slow level drift (value noise ±1.5 dB, ≤ 0.05 Hz) so long sessions don't feel static. Nothing periodic.

### 2.4 Bus
Recorded slots join the environment bus exactly like the synth slots: level² gain × calibration × soft-knee sum × plate send (−14 dB; for birds and stream use −18 dB — recordings already contain room). The v4 loudness servo and master chain are unchanged.

## 3. Protocol / UI / docs
- `env <ocean|rain|fire|wind|stream|birds> <0..1>`; state `env` object and `sounds` object carry six keys; state file migrates to `"version":5` (add missing env keys at 0).
- Panel.qml ENVIRONMENT section shows six rows: Ocean, Rain, Fire, Wind, Stream, Birds. Rows whose slot is unavailable (`sounds[name] === false`) render at 40 % opacity with the description "sounds/<name>.wav missing". Icons: verify each glyph exists in the bar font before using it (`fc-query --format='%{charset}\n' <font file>` contains the codepoint range) — fall back to the existing 󰖝/󰈸 style choices or to no icon rather than a tofu box. Service.qml mirrors `sounds`.
- `~/.local/bin/omanoise` help text lists the six names. README: the recorded sounds, `sounds/LICENSES.md`, "drop in your own 48 kHz 16-bit WAV named after a slot", and the segment-scheduling explanation. Manifest 5.0.0.

## 4. Acceptance (append "v5" to ACCEPTANCE.md; temp XDG_STATE_HOME; `--env` overrides)
1. Each recorded slot solo (level 1.0, tonal 0, 2 × 150 s): integrated −23 ± 1.5 LUFS with trim within ±2 dB (calibration works); true peak ≤ −1 dBTP; **no click at any crossfade: max sample step ≤ 0.25 and no 1-second short-term loudness step > 4 LU**; a segment log on stderr (`--stats` mode) listing every segment start/length proves randomness (no two consecutive equal starts, no fixed period).
2. Blend (ocean 0.7, fire 0.7, stream 0.5, birds 0.3, 150 s): no clipping, LUFS ±2, max step ≤ 0.25.
3. Ocean, rain, Focus, Relax: unchanged (one regression render each within run-to-run noise of v4).
4. Missing-file fallback: rename `sounds/birds.wav` temporarily in a copy of the plugin dir (or point `OMANOISE_SOUNDS` env override at an empty dir): engine starts, `sounds.birds=false`, slot silent, no crash.
5. Resources: RSS ≤ 120 MB with all four files loaded, sounds ready < 3 s, CPU ≤ 4 %.
6. Plumbing: `omanoise env stream 0.5` round-trips (the shell's IPC handler will be stale until a later restart — report it).

## 5. Constraints
As v4 §5: no live playback, no `omarchy restart shell`, `pkill -x omanoise-engine` only after the final good build (the user may be listening — every kill restarts the engine paused, so do it once, at the end), RT thread never allocates/locks/prints, `-Wall -Wextra` clean, existing commands keep working, real state file ends at v5 defaults (version 5, focus, volume 0.7, sliders 0.5, toggles false except adaptive, env ocean 0.8 / others 0).
