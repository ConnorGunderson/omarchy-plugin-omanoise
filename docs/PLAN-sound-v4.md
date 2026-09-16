# Omanoise v4 — three modes (Focus / Relax / Environment) and a blendable environment bank

Status: approved by Josh. Implementer: Claude Opus. Builds on v3 (`PLAN-sound-v3.md`, `ACCEPTANCE.md` v3 section, `PROGRESS.md`). All v3 psychoacoustic rules (§1 of the v3 plan) remain in force for every new sound.

## 0. User verdict on v3 (design against this)
- "The whole system sounds so much better."
- **Focus: really good — do not change its sound.**
- **Relax: far better, decent.** Merge Sleep into Relax (keep the name Relax); noise "halfway between both". Sleep mode is removed — this runs on a computer, nobody sleeps to it.
- **Ocean: the weakest now (still not bad).** Tonal material should be a fainter background by default. Ocean becomes one sound inside a new mode category called **Environment**, containing ocean, fireplace, rain, wind, blendable with sliders (one at a time or mixed).

## 1. Modes

Enum becomes `M_FOCUS, M_RELAX, M_ENV` (names `focus`, `relax`, `environment`). Backward compatibility: `mode sleep` → relax, `mode ocean` → environment (accepted silently, state reports the new name). State file migration to `"version":4`: mode sleep→relax, ocean→environment; add `"env"` object with defaults below.

### 1.1 Focus — unchanged
Do not touch `MODES[M_FOCUS]` or anything that only Focus uses. Re-run its acceptance renders to prove nothing drifted (numbers must match v3 within run-to-run noise).

### 1.2 Relax — merged Relax + Sleep
Start from v3 Relax and move these toward v3 Sleep, roughly halfway:
- `lufs_target` −25 (between −24 and −28, biased to Relax so it stays audible at desk level).
- Pads: `pad_warm 0.45 / pad_glass 0.55`, `pad_cut 1.9`, `pad_db −3.5`, `bass_db −4`.
- `chord_min/max` 95 / 150 s; `drift_depth 0.68`; `macro 0.75 / 1.13`.
- Instruments as Relax (harp, low piano, very soft music box) but sparser: `gap 7–16 s`, `vel 26–48`, `dyad_p 0.25`.
- **Noise "halfway"**: `noise_color`, `noise_db`, `noise_lp`, `nature_boost` and `ocean_db` each set to the arithmetic midpoint of the v3 Relax and Sleep values (read them from brain.c; dB values averaged in dB, Hz values averaged geometrically).
- Plate: decay 0.90, damp 2750 Hz, return −8 dB.
- Remove Sleep's 20-minute phasing timeline and any Sleep-only code.

### 1.3 Environment — new category
- Tonal layer is a faint background: `pad_db −18`, `bass_db −20`, pads warm 1.0 / glass 0.15, `pad_cut 2.0`, chord change 120–160 s, no instruments, plate return −10 dB (pad only). The `tonal` slider still scales this (0 = pure environment).
- `lufs_target` −23 (as Ocean today). `noise` bed off (the environment sounds are the bed).
- **Environment bank**: four generators, each with a user level 0..1, all running simultaneously, mixed into an environment bus → soft-knee sum → small plate send (−14 dB) → master.
  - `ocean` — the existing v3 ocean engine, unchanged in sound.
  - `rain` — Farnell-style: (a) patter bed: Poisson impulses 800–1600/s (rate follows level and Intensity) through a 2-pole band-pass whose centre wanders 350–800 Hz by value noise ≤ 0.05 Hz, then LP 3 kHz; (b) hiss: white → HP 1.8 kHz → LP 6.5 kHz at −16 dB relative to the patter, with a slow random "shower" envelope (value noise ≤ 0.08 Hz, ±3 dB); (c) sparse soft droplets: Poisson 1–2.5/s, 12–20 ms sine chirp gliding 2.4 → 1.6 kHz with a 4 ms raised-cosine attack, HP 1 kHz, −22 dB relative, random pan drifting slowly. No resonant whistles.
  - `fire` — (a) glow: pink/brown noise → LP 350 Hz with a slow random flicker envelope (value noise ≤ 0.3 Hz, ±4 dB); (b) mid hiss: noise → BP 1.2–2.8 kHz Q 0.7 at −12 dB rel glow; (c) crackles: Poisson 3–7/s, each a 4–18 ms noise burst → BP 1.5–4.5 kHz, exponential decay, amplitude log-uniform over 18 dB so most are quiet; overall crackle bus ≤ −18 dB below the glow; (d) rare pops: 0.15/s, LP'd click 150–300 Hz, 60 ms decay, −20 dB rel glow. Crackles are the one place sub-12 ms rises are allowed; they must stay quiet (this is the level cap above) — the calm reference is a fireplace across the room, not a campfire at your feet.
  - `wind` — brown noise → 2× cascaded SVF LP swept 180–800 Hz by value noise ≤ 0.04 Hz with slow gust envelope (value noise ≤ 0.06 Hz, ±5 dB); a second decorrelated copy for the other channel; **no** band-pass whistle component (that was the v1 "wind in a mic"); think wind through trees at a distance.
- Defaults: `ocean 0.8, rain 0, fire 0, wind 0`. Levels are perceptual: level² gain mapping, and each generator is pre-normalised so level 1.0 alone lands at the mode's LUFS target before the auto-trim (calibrate with renders).
- All environment generators use only value-noise drifts (≤ 0.3 Hz) and Poisson events — nothing periodic (v3 rule 1).

## 2. Protocol
- `env <ocean|rain|fire|wind> <0..1>` sets one level; persisted in the state file under `"env":{"ocean":..,"rain":..,"fire":..,"wind":..}`; echoed in every `state` line.
- `mode environment` (aliases `ocean`, `env`); `mode sleep` alias → relax.
- `--render environment …` uses the persisted env levels; add `--env ocean=1,rain=0.5,…` render override for tests.
- Everything else unchanged.

## 3. UI (Service.qml / Panel.qml) — this time the QML change is real, keep it clean and in the existing style
- `modes` list: Focus (unchanged), Relax `{ icon "󰈸"→ keep current Relax icon, blurb "Harp, low piano, glass pad, distant waves" }`, Environment `{ id "environment", label "Environment", icon "󰞍" (nf-md-waves), blurb "Blend ocean, rain, fire and wind" }`. Sleep entry removed. Mode row is now 3 buttons (cellWidth math already generic).
- New section **ENVIRONMENT**, visible only when `mode === "environment"`, placed between MODE and SOUND, with four `ParamRow`s: Ocean 󰞍, Rain 󰖗, Fire 󰈸, Wind 󰖝. Each slider drives `svc.setEnv(name, v)` → `env <name> <v>`; values mirrored from state `env` object (Service: `property var env: ({ocean:0.8,rain:0,fire:0,wind:0})`). Give `ParamRow` an optional `onValue` callback or add a sibling `EnvRow` component — do not duplicate 60 lines.
- In Environment mode, hide the Neural-modulation/Pulse toggles (already Focus-only) and keep Binaural hidden too (no pads worth beating).
- Hero subtitle and IPC unchanged. Manifest version 4.0.0. README: modes, environment bank, protocol.
- `~/.local/bin/omanoise` CLI: add `env <name> <0-1>`; `mode` accepts the new names (it just forwards).

## 4. Acceptance (append a "v4" section to `docs/ACCEPTANCE.md`; 3 × 180 s renders unless noted)
1. **Focus regression**: all v3 metrics within ±0.7 LU / ±1 dB of the v3 table.
2. **Relax**: v3 test set with `lufs_target −25`; FLUCT/ROUGH ≤ the v3 Ocean reference + 2 dB (use the environment-ocean-only render from this run as the reference, level 0.8, tonal 0.5).
3. **Environment defaults** (ocean 0.8): LUFS −23 ±1.5; tonal-1.0 vs tonal-0.0 difference **≤ 4 LU** (proves the tonal layer is now faint) — note this inverts the v2/v3 "harmonic dominance" test for this mode, by design.
4. **Each environment sound solo** (level 1.0, others 0, tonal 0, 120 s × 2): auto-trim lands within ±1.5 LU of −23; FLUCT ≤ ocean-solo + 3 dB; ROUGH ≤ ocean-solo + 3 dB for rain and wind, ≤ +5 dB for fire (crackles); true peak ≤ −1 dBTP; click detector (max sample step) ≤ 0.25 — fire crackles must pass this too, which bounds their level; spectral: energy > 4 kHz at least 10 dB below 200 Hz–1 kHz for ocean/wind/fire, at least 4 dB below for rain (hiss is part of rain).
5. **Blend** (all four at 0.7, 120 s): no clipping (true peak ≤ −1), LUFS within ±2 of target, ROUGH ≤ ocean-solo + 5 dB.
6. **Modulation-line audit** as v3 §5.4 for every new generator: enumerate every periodic oscillator; none between 0.2 and 300 Hz reaching the audio.
7. **Migration**: a v3 state file with `"mode":"sleep"` loads as relax, `"mode":"ocean"` as environment, gains `"version":4` and the default `env` object; `mode sleep` / `mode ocean` commands are accepted.
8. Resources: CPU ≤ 4 %, RSS ≤ 120 MB, ready < 3 s. Plumbing: `omarchy-shell omanoise state` shows `env`; `omanoise env rain 0.5` round-trips.

## 5. Constraints (as v3 §6)
No live playback; no `omarchy restart shell` (hot-reload; report if the IPC handler is stale — that is expected and will be cleared by a restart afterwards); temp `XDG_STATE_HOME` for renders; real state file ends with v4 defaults (`version 4`, mode focus, volume 0.7, sliders 0.5, binaural/modulation/pulse false, adaptive true, env defaults); C11 `-Wall -Wextra` clean; RT thread never allocates/locks/prints; `pkill -x omanoise-engine` after a good build; keep every existing stdin command working.
