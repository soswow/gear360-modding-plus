#!/bin/sh
# Build IANA 2026f TZif files, using fat format for the camera's older libc.
set -eu
here=$(CDPATH= cd -- "$(dirname "$0")" && pwd)
out=${1:-"$here/../../../output/timezone-build"}
mkdir -p "$out"
out=$(CDPATH= cd -- "$out" && pwd)
archive=$out/tzdata2026f.tar.gz
curl -fsSL https://data.iana.org/time-zones/releases/tzdata2026f.tar.gz -o "$archive"
expected=01b4a91cec5167ed48c11e8ab78f2048cead78ce94b0cafcad0ae8c5a3044d3d
actual=$(shasum -a 256 "$archive" | awk '{print $1}')
[ "$actual" = "$expected" ] || { echo 'timezone archive hash mismatch' >&2; exit 1; }
rm -rf "$out/source" "$out/zoneinfo-2026f"
mkdir -p "$out/source" "$out/zoneinfo-2026f"
tar -xzf "$archive" -C "$out/source"
cd "$out/source"
zic -b fat -d "$out/zoneinfo-2026f" africa antarctica asia australasia europe northamerica southamerica etcetera backward
# The SD card does not support symlinks. Archive each alias as a regular file.
python3 - "$out" <<'PY'
import pathlib, sys, tarfile
root = pathlib.Path(sys.argv[1]); zones = root / 'zoneinfo-2026f'
with tarfile.open(root / 'zoneinfo-2026f.tar', 'w', dereference=True) as archive:
    archive.add(zones, arcname=zones.name)
PY
printf '%s\n' "$out/zoneinfo-2026f.tar"
