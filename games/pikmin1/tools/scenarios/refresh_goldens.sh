#!/usr/bin/env bash
# Regenerate every OS's golden screenshots from CI (issue #87 P2). One command:
#
#   games/pikmin1/tools/scenarios/refresh_goldens.sh [--ref BRANCH] [--run RUN_ID]
#
# It pushes BRANCH (default: the current branch) to a temporary golden-refresh/<time> branch, which
# starts the "Disc-free scenarios" workflow in update mode, waits for it, deletes that branch,
# downloads the golden-update-<OS> artifacts of the Linux, macOS and Windows runners and copies them
# into tools/scenarios/golden/<os>/. The goldens come from the CI runners, never from this
# machine's GPU. Review the pictures, then commit golden/. With --run, skips the push and
# downloads from an existing update run. Needs the GitHub CLI (`gh`) signed in to the target repository.
# (A workflow_dispatch with update_goldens=true does the same where GitHub allows it: it only
# dispatches a workflow file that exists on the default branch.)
set -euo pipefail

repo="${PIKMIN_SCENARIO_REPO:-$(gh repo view --json nameWithOwner --jq .nameWithOwner)}"
ref="$(git rev-parse --abbrev-ref HEAD)"
run=""
while [ $# -gt 0 ]; do
  case "$1" in
    --ref) ref="$2"; shift 2 ;;
    --run) run="$2"; shift 2 ;;
    *) echo "usage: $0 [--ref BRANCH] [--run RUN_ID]" >&2; exit 2 ;;
  esac
done

here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
golden="$here/golden"

if [ -z "$run" ]; then
  branch="golden-refresh/$(date -u +%Y%m%d-%H%M%S)"
  git push origin "$ref:refs/heads/$branch"
  echo "pushed $branch; waiting for the run to appear"
  for _ in $(seq 1 30); do
    sleep 5
    run="$(gh run list --repo "$repo" --workflow disc-free-scenarios.yml --branch "$branch" \
      --limit 1 --json databaseId --jq '.[0].databaseId // empty')"
    [ -n "$run" ] && break
  done
  [ -n "$run" ] || { echo "no run found for $branch" >&2; exit 1; }
  echo "run $run"
  # The scenarios job writes the goldens even when an unrelated job in the run fails.
  gh run watch "$run" --repo "$repo" --interval 30 >/dev/null || true
  git push origin ":refs/heads/$branch" || true
fi

tmp="$(mktemp -d)"
trap 'rm -rf "$tmp"' EXIT
for pair in Linux:linux macOS:macos Windows:windows; do
  art="golden-update-${pair%%:*}"
  gh run download "$run" --repo "$repo" --name "$art" --dir "$tmp/$art"
  [ -s "$tmp/$art/PROVENANCE.txt" ] || { echo "missing provenance: $art" >&2; exit 1; }
  if [ "${pair%%:*}" = "Linux" ]; then
    [ -s "$tmp/$art/MESA_PIN" ] || { echo "missing Mesa pin: $art" >&2; exit 1; }
  fi
  python3 "$here/golden_shots.py" validate --dir "$tmp/$art"
done
# Validate every OS before replacing any committed golden.
for pair in Linux:linux macOS:macos Windows:windows; do
  art="golden-update-${pair%%:*}"
  dir="$golden/${pair##*:}"
  mkdir -p "$dir"
  cp "$tmp/$art"/* "$dir/"
  echo "updated $dir ($(ls "$tmp/$art" | wc -l | tr -d ' ') files)"
done
echo "Done. Look at the pictures in golden/, then commit them."
