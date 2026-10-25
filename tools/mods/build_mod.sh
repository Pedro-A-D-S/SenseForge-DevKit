#!/usr/bin/env bash
# build_mod.sh - SF-15: builds the ReShade add-on of ONE MOD, named and
# versioned from its mods/<slug>/mod.json, into runtime-asi/build-mod-<slug>.
#
# Usage: bash tools/mods/build_mod.sh <slug>
# Prints the Release output directory on the last line (for package_mod.py
# --build-dir). Third-party sources must already be fetched
# (tools/fetch_third_party.sh).

set -euo pipefail

slug="${1:?usage: build_mod.sh <slug>}"
root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
mod_json="$root/mods/$slug/mod.json"
[ -f "$mod_json" ] || { echo "build_mod: $mod_json not found" >&2; exit 1; }

field() {
  python -I -c "import json,sys; m=json.load(open(sys.argv[1],encoding='utf-8')); \
v=m; [v:=v[k] for k in sys.argv[2].split('.')]; print(v)" "$mod_json" "$1"
}

game_slug="$(field build.cmake_game_slug)"
game_name="$(field build.cmake_game_name)"
version="$(field version)"
build_dir="$root/runtime-asi/build-mod-$slug"

cmake -S "$root/runtime-asi" -B "$build_dir" \
  -DSENSEFORGE_GAME_SLUG="$game_slug" \
  -DSENSEFORGE_GAME_NAME="$game_name" \
  -DSENSEFORGE_MOD_VERSION="$version" >&2
cmake --build "$build_dir" --config Release --target senseforge_addon >&2

echo "$build_dir/Release"
