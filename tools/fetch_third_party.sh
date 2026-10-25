#!/usr/bin/env bash
# SF-1 — fetches the pinned open-source dependencies.
#
# The target lives OUTSIDE the repository (under LOCALAPPDATA): heavy vendored
# trees don't belong inside the git repo. Override with SENSEFORGE_THIRD_PARTY.
#
# PITFALL (learned live during SF-1): git.exe is a native Windows program —
# MSYS /c/... paths passed to it resolve to <cwd-drive>:\c\... and the clones
# silently land in an invisible C:\c\Users\... mirror tree. Every path handed
# to a native tool must be a native mixed path (C:/...), enforced below via
# cygpath -m.
#
# Reproducible: every dependency is pinned by a full commit SHA (the upstream
# tag is only a comment) and fetched by that SHA. After the fetch, and for a
# tree that already exists (local or restored from the CI cache), HEAD must
# equal the pin or the tree is fetched again; a mismatch after that fails the
# build. SF-20: a moved or SHA-named upstream tag can no longer change what is
# compiled into the published add-on. Resolved HEADs go to VERSIONS.md.

set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
TP="${SENSEFORGE_THIRD_PARTY:-$(cygpath -m "$LOCALAPPDATA")/senseforge/third_party}"
# Canonical native mixed path (C:/...) — safe for both bash and native tools.
TP="$(cygpath -m "$TP")"
mkdir -p "$TP"

# name|repo|pinned commit (40-hex SHA; the upstream tag in the comment)
DEPS=(
  "minhook|https://github.com/TsudaKageyu/minhook.git|8af6b4acae5a9388fd742b56fa79ece89d96f823"
  "portaudio|https://github.com/PortAudio/portaudio.git|147dd722548358763a8b649b3e4b41dfffbcfbb6"  # v19.7.0
  "dualsensitive|https://github.com/tpetsas/dualsensitive.git|ec87b414875ef97aab5cb05aeb2f3005456ea0ef"
  "imgui|https://github.com/ocornut/imgui.git|4be0b47ecc0edd535a40a79f6ceb6f1ac5936483"
  "vgmstream|https://github.com/vgmstream/vgmstream.git|f7f764155b227f683adbab25402fe8f2be0b2ec2"
  "wwiser|https://github.com/bnnm/wwiser.git|08e937b649bc7007c234bbd59e34a41d5eef3b38"
  "nlohmann_json|https://github.com/nlohmann/json.git|55f93686c01528224f448c19128836e7df245f72"  # v3.12.0
  # Headers only: ReShade 6.8 reshade_overlay.hpp requires imgui.h 19250 (docking)
  # (the senseforge.<game>.addon64 panel draws through ReShade's ImGui).
  "imgui_reshade|https://github.com/ocornut/imgui.git|3912b3d9a9c1b3f17431aebafd86d2f40ee6e59c"  # v1.92.5-docking
)

{
  echo "| dep | repo | pinned ref | resolved HEAD |"
  echo "| :-- | :-- | :-- | :-- |"
} > "$TP/VERSIONS.md"

for entry in "${DEPS[@]}"; do
  IFS='|' read -r name repo ref <<< "$entry"
  dest="$TP/$name"

  if ! [[ "$ref" =~ ^[0-9a-f]{40}$ ]]; then
    echo "[$name] pin '$ref' is not a full commit SHA" >&2
    exit 1
  fi

  if [ -d "$dest/.git" ] && [ "$(git -C "$dest" rev-parse HEAD 2>/dev/null)" = "$ref" ]; then
    echo "[$name] present at pinned commit — skipping"
  else
    echo "[$name] fetching $repo @ $ref"
    rm -rf "$dest"
    git init --quiet "$dest"
    git -C "$dest" remote add origin "$repo"
    git -C "$dest" fetch --quiet --depth 1 origin "$ref"
    git -C "$dest" checkout --quiet FETCH_HEAD
  fi

  head="$(git -C "$dest" rev-parse HEAD)"
  if [ "$head" != "$ref" ]; then
    echo "[$name] HEAD $head does not match the pinned commit $ref" >&2
    exit 1
  fi
  echo "| $name | $repo | $ref | $head |" >> "$TP/VERSIONS.md"

  # Vendored-drift fixes (see runtime-asi/third_party_patches/README):
  # applied in order, idempotent — already-applied patches fail --check
  # and are skipped. Paths must be native (cygpath -m) for git.exe.
  patch_dir="$ROOT/runtime-asi/third_party_patches/$name"
  if [ -d "$patch_dir" ]; then
    for p in "$patch_dir"/*.patch; do
      [ -e "$p" ] || continue
      p_m="$(cygpath -m "$p")"
      if git -C "$dest" apply --check "$p_m" >/dev/null 2>&1; then
        git -C "$dest" apply "$p_m"
        echo "[$name] applied $(basename "$p")"
      fi
    done
  fi
done

echo
echo "OK — third_party ready under $TP"
echo "CMake picks it up via SENSEFORGE_THIRD_PARTY_DIR (defaults to this location)."
