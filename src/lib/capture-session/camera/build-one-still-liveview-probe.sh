#!/bin/sh
# Build the one-still diagnostic with the stock liveview-first ordering for
# the camera ABI.  Does not run it.
set -eu
root=$(CDPATH= cd -- "$(dirname "$0")/.." && pwd)
camera=$(CDPATH= cd -- "$(dirname "$0")" && pwd)
out=${1:-"$root/../../../output/one-still-liveview-probe-build"}
dump=${GEAR360_DUMP:-"$root/../../../dump/extracted/platform"}
clang=${CLANG:-/opt/homebrew/opt/llvm/bin/clang}
lld=${LLD:-/opt/homebrew/opt/llvm@17/bin/ld.lld}
readelf=${READELF:-/opt/homebrew/opt/llvm/bin/llvm-readelf}
suppressed=${SUPPRESS_THE_STILL-0}
case "$suppressed" in
  0) suppress_define= ;;
  1) suppress_define=-DSUPPRESS_THE_STILL ;;
  *) printf '%s\n' 'SUPPRESS_THE_STILL must be exactly 0 or 1' >&2; exit 2 ;;
esac
mkdir -p "$out"
"$clang" -target armv7a-linux-gnueabi -marm -mcpu=cortex-a9 \
  -std=c99 -O2 -fno-builtin -fno-stack-protector -ffreestanding \
  -Wall -Wextra -Werror -nostdlib -fno-pie -I"$root" ${suppress_define} -c \
  "$camera/one-still-liveview-probe.c" -o "$out/one-still-liveview-probe.arm.o"
"$clang" -target armv7a-linux-gnueabi -marm -mcpu=cortex-a9 \
  -std=c99 -O2 -fno-builtin -fno-stack-protector -ffreestanding \
  -Wall -Wextra -Werror -nostdlib -fno-pie -no-pie \
  -fuse-ld=lld --ld-path="$lld" \
  -Wl,--dynamic-linker=/lib/ld-linux.so.3 -Wl,--allow-shlib-undefined \
  -Wl,--no-undefined -Wl,--as-needed -Wl,-z,noexecstack \
  -L"$dump/lib" -L"$dump/usr/lib" \
  -Wl,-rpath-link,"$dump/lib" -Wl,-rpath-link,"$dump/usr/lib" \
  "$root/start_hosted.S" "$out/one-still-liveview-probe.arm.o" \
  -o "$out/capture-shot" -l:libc.so.6 -l:libdl.so.2 \
  -l:librt.so.1 -l:libgcc_s.so.1
"$readelf" -h -l -d "$out/capture-shot"
shasum -a 256 "$out/capture-shot"
