#!/bin/sh
set -eu

src="${1:-.}"
publicrepo="$src/../../public/dn-tools/dither"

cd "$src"

# Deploy exactly what git tracks, so nothing untracked — build output, the
# virtualenv, scratch renders — can reach the public repo. Modes follow the
# working tree, keeping the scripts executable and the sources not.
mkdir -p "$publicrepo"
git ls-files | while IFS= read -r f; do
    [ -f "$f" ] || continue          # tracked but deleted locally
    if [ -x "$f" ]; then mode=755; else mode=644; fi
    mkdir -p "$publicrepo/$(dirname "$f")"
    install -m "$mode" "$f" "$publicrepo/$f"
done
