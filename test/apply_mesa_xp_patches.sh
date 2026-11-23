#!/bin/sh
set -eu

repo_root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
mesa_dir="$repo_root/mesa"
expected_revision=357bbc55bd419dae5625a14ffa0acf1818f9ebbe

actual_revision=$(git -C "$mesa_dir" rev-parse HEAD)
if [ "$actual_revision" != "$expected_revision" ]; then
    echo "Mesa revision mismatch: expected Mesa 25.1.6 $expected_revision, got $actual_revision" >&2
    exit 1
fi

if [ -n "$(git -C "$mesa_dir" status --porcelain)" ]; then
    echo "Mesa submodule must be clean before applying XP patches." >&2
    exit 1
fi

patch_series="$repo_root/test/mesa25_patch_series.txt"


while IFS= read -r relative_patch; do
    [ -n "$relative_patch" ] || continue
    patch_file="$repo_root/$relative_patch"
    git -C "$mesa_dir" apply --check "$patch_file"
    git -C "$mesa_dir" apply "$patch_file"
    echo "Applied $(basename "$patch_file")."
done < "$patch_series"
