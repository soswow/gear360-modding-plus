#!/bin/sh
# Build g360-shot (the g360cap library plus its command-line program) for the
# camera: 32-bit ARM, glibc 2.13, against the extracted firmware's libraries.
# Builds only; does not touch the camera.
set -eu
here=$(CDPATH= cd -- "$(dirname "$0")" && pwd)
re=$(CDPATH= cd -- "$here/.." && pwd)
out=${1:-"$re/../../output/g360cap-build"}
dump=${GEAR360_DUMP:-"$re/../../dump/extracted/platform"}
clang=${CLANG:-/opt/homebrew/opt/llvm/bin/clang}
lld=${LLD:-/opt/homebrew/opt/llvm@17/bin/ld.lld}
readelf=${READELF:-/opt/homebrew/opt/llvm/bin/llvm-readelf}
mkdir -p "$out"
cflags="-target armv7a-linux-gnueabi -marm -mcpu=cortex-a9 -std=c99 -O2 -fno-builtin \
  -fno-stack-protector -ffreestanding -Wall -Wextra -Werror -nostdlib -fno-pie \
  -I$here -I$re/capture-session -I$re/encoded-set"
for src in "$here/g360cap.c" "$here/g360cap_check.c" "$here/g360_dng.c" "$here/g360_exif.c" "$here/g360_output.c" "$re/encoded-set/encoded_set.c" \
           "$here/g360-shot.c"; do
  # shellcheck disable=SC2086
  "$clang" $cflags -c "$src" -o "$out/$(basename "$src" .c).arm.o"
done
# libm and libdevice-node are linked directly (not as-needed): the gyro
# history needs them loaded in the executable itself (ACTIVE_PLAN step 4e).
# shellcheck disable=SC2086
"$clang" $cflags -no-pie -fuse-ld=lld --ld-path="$lld" \
  -Wl,--dynamic-linker=/lib/ld-linux.so.3 -Wl,--allow-shlib-undefined \
  -Wl,--no-undefined -Wl,--as-needed -Wl,-z,noexecstack \
  -L"$dump/lib" -L"$dump/usr/lib" \
  -Wl,-rpath-link,"$dump/lib" -Wl,-rpath-link,"$dump/usr/lib" \
  "$re/capture-session/start_hosted.S" "$out/g360cap.arm.o" "$out/g360cap_check.arm.o" \
  "$out/encoded_set.arm.o" "$out/g360_dng.arm.o" "$out/g360_exif.arm.o" "$out/g360_output.arm.o" "$out/g360-shot.arm.o" \
  -o "$out/g360-shot" -Wl,--no-as-needed -l:libm.so.6 -l:libdevice-node.so.0.1 \
  -Wl,--as-needed -l:libc.so.6 -l:libdl.so.2 -l:librt.so.1 -l:libgcc_s.so.1
# The T20 movie-preview probe: the same library, its own main.
# shellcheck disable=SC2086
"$clang" $cflags -c "$here/g360-preview.c" -o "$out/g360-preview.arm.o"
# shellcheck disable=SC2086
"$clang" $cflags -no-pie -fuse-ld=lld --ld-path="$lld" \
  -Wl,--dynamic-linker=/lib/ld-linux.so.3 -Wl,--allow-shlib-undefined \
  -Wl,--no-undefined -Wl,--as-needed -Wl,-z,noexecstack \
  -L"$dump/lib" -L"$dump/usr/lib" \
  -Wl,-rpath-link,"$dump/lib" -Wl,-rpath-link,"$dump/usr/lib" \
  "$re/capture-session/start_hosted.S" "$out/g360cap.arm.o" "$out/g360cap_check.arm.o" \
  "$out/encoded_set.arm.o" "$out/g360-preview.arm.o" \
  -o "$out/g360-preview" -Wl,--no-as-needed -l:libm.so.6 -l:libdevice-node.so.0.1 \
  -Wl,--as-needed -l:libc.so.6 -l:libdl.so.2 -l:librt.so.1 -l:libgcc_s.so.1
"$readelf" -h -d "$out/g360-shot" | grep -E 'Machine|NEEDED|Type'
shasum -a 256 "$out/g360-shot" "$out/g360-preview"
