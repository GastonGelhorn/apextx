#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-only
set -u

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
PYTHON="$ROOT/.venv/bin/python"
SDL2_DIR="/opt/homebrew/lib/cmake/SDL2"

report() { printf '  %s\n' "$*"; }
fail() { printf '\nError: %s\n' "$*" >&2; exit 1; }

[[ -x "$PYTHON" ]] || fail "missing project virtual environment: $PYTHON (see docs/nb4/BUILD.md)"

printf 'Noble NB4 orientation compatibility\n'
status=0
for orientation in portrait landscape; do
  if [[ "$orientation" == landscape ]]; then
    directory="$ROOT/build/nb4-land"
    landscape=ON
    geometry="480x320"
  else
    directory="$ROOT/build/nb4"
    landscape=OFF
    geometry="320x480"
  fi

  report "$orientation ($geometry)"
  if ! cmake -S "$ROOT" -B "$directory" \
      -DPCB=PL18 -DPCBREV=NB4 -DNB4_LANDSCAPE="$landscape" \
      -DCMAKE_BUILD_TYPE=Release -DPython3_EXECUTABLE="$PYTHON" \
      -DSDL2_DIR="$SDL2_DIR" -DDISABLE_COMPANION=ON -Wno-dev \
      >/dev/null 2>&1; then
    report "configuration failed"
    status=1
    continue
  fi

  cmake --build "$directory" --target native-configure >/dev/null 2>&1
  output=$(cmake --build "$directory/native" --target yaml_data 2>&1)
  if printf '%s' "$output" | grep -q 'struct size changed'; then
    report "persistent structure size differs between orientations"
    printf '%s\n' "$output" | grep 'struct size changed' | sed 's/^/    /'
    status=1
  elif printf '%s' "$output" | grep -qi 'error'; then
    report "build failed"
    printf '%s\n' "$output" | grep -i 'error' | head -3 | sed 's/^/    /'
    status=1
  else
    report "persistent structures match"
  fi
done

if (( status == 0 )); then
  report "both orientations compile with compatible persistent structures"
else
  report "orientation compatibility check failed"
fi
exit "$status"
