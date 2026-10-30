#!/bin/bash
# Merge a RISC OS Open nightly source tarball into the Linux port.
#
#   nightly/update.sh src-iomd-dev.5.32.tar.bz2
#
# The tarball's RiscOS tree is committed on the rool-vendor branch (which
# holds upstream exactly as released), then merged into the current branch
# with git merge, so only what upstream changed since the last nightly has
# to be merged.  Afterwards nightly/check.py lists the usual Linux port
# hazards in what changed.
#
# Tarballs: https://www.riscosopen.org/content/downloads/risc-os-tarballs
# ("IOMD HAL" development source).

set -euo pipefail
tarball=$(realpath "$1")
cd "$(git rev-parse --show-toplevel)"
top=$PWD

if [[ -n "$(git status --porcelain --untracked-files=no)" ]]; then
  echo "Commit or stash your changes first." >&2
  exit 1
fi
git rev-parse -q --verify rool-vendor >/dev/null || {
  echo "No rool-vendor branch: this tree hasn't had a nightly merged with these tools." >&2
  exit 1
}

tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT
echo "Unpacking $(basename "$tarball")..."
mkdir "$tmp/x"
tar -xf "$tarball" -C "$tmp/x"
bs=$(find "$tmp/x" -maxdepth 4 -type d -path '*/RiscOS/BuildSys' -print -quit)
[[ -n "$bs" ]] || { echo "No RiscOS/BuildSys in the tarball." >&2; exit 1; }
root=$(dirname "$(dirname "$bs")")

echo "Committing it on rool-vendor..."
export GIT_INDEX_FILE="$tmp/index"
git -C "$root" --git-dir="$top/.git" --work-tree="$root" add -A RiscOS
tree=$(git write-tree)
unset GIT_INDEX_FILE
if [[ "$tree" == "$(git rev-parse 'rool-vendor^{tree}')" ]]; then
  echo "Nothing new: rool-vendor already has exactly this tree."
  exit 0
fi
name=$(basename "$tarball")
commit=$(git commit-tree "$tree" -p rool-vendor -m "RISC OS Open source tarball $name

Upstream tree only, as released.")
git update-ref refs/heads/rool-vendor "$commit"
git --no-pager diff --stat rool-vendor^ rool-vendor | tail -1

echo "Merging into $(git branch --show-current)..."
if git merge --no-ff --no-edit -m "Merge RISC OS Open $name into the Linux port" rool-vendor; then
  echo "Merged without conflicts."
  python3 nightly/check.py HEAD^1 || true
else
  echo
  echo "Conflicts to resolve (keep the port's Linux/PVOps parts, take upstream's logic):"
  git diff --name-only --diff-filter=U
  python3 nightly/check.py HEAD || true
  echo
  echo "When resolved: python3 nightly/check.py HEAD, then git commit."
  exit 1
fi
