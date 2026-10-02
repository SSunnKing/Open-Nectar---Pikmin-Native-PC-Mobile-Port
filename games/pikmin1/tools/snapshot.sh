#!/usr/bin/env bash
# Source snapshots for the PC port.
#
# This is a supplementary local safety net for quick source-only checkpoints.
# Git remains the authoritative history for the GX translator, audio backend,
# launcher and the rest of the native port.
#
# They cover source only. ROMs, extracted assets, build directories and saves
# are deliberately excluded: they are large, they are yours, and none of them
# is what a bad edit destroys.
#
#   tools/snapshot.sh save [name]   create snapshots/<date>-<name>.tar.zst
#   tools/snapshot.sh list          show what is available
#   tools/snapshot.sh restore FILE  put those sources back
#
# Restoring overwrites the files held in the snapshot and leaves everything
# else alone, so a build directory survives; run cmake --build afterwards.

set -euo pipefail

repo="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
store="$repo/snapshots"

# What a snapshot holds. Add to this list rather than widening the excludes.
paths=(
    src include pc_port config tools
    CMakeLists.txt configure.py format-files.py format-files.sh
    .clang-format .flake8 .gitattributes .gitignore Doxyfile
)

compressor() {
    if command -v zstd >/dev/null 2>&1; then echo "zstd"; else echo "gzip"; fi
}

usage() {
    sed -n '2,20p' "${BASH_SOURCE[0]}" | sed 's/^# \?//'
    exit "${1:-1}"
}

cmd="${1:-}"
case "$cmd" in
save)
    mkdir -p "$store"
    name="${2:-stable}"
    stamp="$(date +%Y%m%d-%H%M%S)"
    case "$(compressor)" in
        zstd) ext="tar.zst"; tarflag=(--zstd) ;;
        *)    ext="tar.gz";  tarflag=(-z) ;;
    esac
    out="$store/$stamp-$name.$ext"

    # Record what this snapshot was, so a future restore is an informed choice.
    meta="$store/.meta-$stamp-$name.txt"
    {
        echo "snapshot: $stamp-$name"
        echo "date:     $(date -Is)"
        echo "git:      $(git -C "$repo" rev-parse --short HEAD 2>/dev/null || echo 'n/a')"
        echo "branch:   $(git -C "$repo" rev-parse --abbrev-ref HEAD 2>/dev/null || echo 'n/a')"
        echo "dirty:    $(git -C "$repo" status --porcelain 2>/dev/null | wc -l) tracked path(s) modified"
    } > "$meta"

    tar "${tarflag[@]}" -cf "$out" -C "$repo" --exclude-vcs "${paths[@]}" \
        "snapshots/$(basename "$meta")"
    rm -f "$meta"

    printf 'Saved %s (%s)\n' "$out" "$(du -h "$out" | cut -f1)"
    ;;

list)
    if [ ! -d "$store" ] || [ -z "$(ls -A "$store" 2>/dev/null)" ]; then
        echo "No snapshots yet. Create one with: tools/snapshot.sh save stable"
        exit 0
    fi
    ls -lh --time-style=+%Y-%m-%d\ %H:%M "$store"/*.tar.* | awk '{print $6, $7, "\t", $5, "\t", $8}'
    ;;

restore)
    file="${2:-}"
    [ -n "$file" ] || usage
    [ -f "$file" ] || { echo "No such snapshot: $file" >&2; exit 1; }

    # Never let a restore be the thing that loses work: keep the current state
    # first, then unpack over it.
    echo "Saving the current sources first, in case this restore is a mistake..."
    "$0" save pre-restore >/dev/null
    tar -xf "$file" -C "$repo"
    printf 'Restored %s\nRebuild with: cmake --build build -j"$(nproc)"\n' "$file"
    ;;

*)
    usage 0
    ;;
esac
