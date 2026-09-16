#!/bin/bash
# Build the Omanoise audio engine and prepare the bundled recordings.
# Needs gcc, the pipewire headers and fluidsynth (present on a stock Omarchy
# install plus the `fluidsynth` and `soundfont-fluid` packages) and ffmpeg
# (stock) for the one-time ogg -> wav conversion. Nothing is downloaded.
set -euo pipefail
cd "$(dirname "$0")"
mkdir -p bin
gcc -O2 -Wall -Wextra -std=gnu11 \
  $(pkg-config --cflags libpipewire-0.3 fluidsynth) \
  -o bin/omanoise-engine \
  engine/main.c engine/dsp.c engine/padsynth.c engine/bank.c engine/brain.c \
  engine/sf2.c engine/fx.c engine/master.c engine/wav.c engine/env.c engine/layers.c \
  $(pkg-config --libs libpipewire-0.3 fluidsynth) -lpthread -lm
echo "built bin/omanoise-engine"
# Environment recordings ship as small .ogg files (see sounds/LICENSES.md); the
# engine reads 48 kHz 16-bit WAV, so convert any that are missing or stale.
if command -v ffmpeg >/dev/null; then
  for ogg in sounds/src/*.ogg; do
    [[ -f $ogg ]] || continue
    wav="sounds/$(basename "${ogg%.ogg}").wav"
    if [[ ! -f $wav || $ogg -nt $wav ]]; then
      ffmpeg -hide_banner -loglevel error -y -i "$ogg" -ar 48000 -ac 2 -c:a pcm_s16le "$wav"
      echo "converted $wav"
    fi
  done
else
  echo "ffmpeg not found: environment recordings will be unavailable until sounds/*.wav exist" >&2
fi
