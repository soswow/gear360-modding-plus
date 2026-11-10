#!/bin/sh
# Build the separate event-only diagnostic for the camera ABI. Does not run it.
set -eu
root=$(CDPATH= cd -- "$(dirname "$0")/.." && pwd)
camera=$(CDPATH= cd -- "$(dirname "$0")" && pwd)
out=${1:-"$root/../../../output/one-still-probe-build"}
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
lv_first=${STOP_LIVEVIEW_FIRST-0}
case "$lv_first" in
  0) lv_define= ;;
  1) lv_define=-DSTOP_LIVEVIEW_FIRST ;;
  *) printf '%s\n' 'STOP_LIVEVIEW_FIRST must be exactly 0 or 1' >&2; exit 2 ;;
esac
stock_like=${STOCK_LIKE_STILL-0}
case "$stock_like" in
  0) stock_define= ;;
  1) stock_define=-DSTOCK_LIKE_STILL ;;
  *) printf '%s\n' 'STOCK_LIKE_STILL must be exactly 0 or 1' >&2; exit 2 ;;
esac
mkdir -p "$out"
"$clang" -target armv7a-linux-gnueabi -marm -mcpu=cortex-a9 \
  -std=c99 -O2 -fno-builtin -fno-stack-protector -ffreestanding \
  -Wall -Wextra -Werror -nostdlib -fno-pie -I"$root" ${suppress_define} ${stock_define} ${lv_define} -c \
  "$camera/one-still-probe.c" -o "$out/one-still-probe.arm.o"
"$clang" -target armv7a-linux-gnueabi -marm -mcpu=cortex-a9 \
  -std=c99 -O2 -fno-builtin -fno-stack-protector -ffreestanding \
  -Wall -Wextra -Werror -nostdlib -fno-pie -no-pie \
  -fuse-ld=lld --ld-path="$lld" \
  -Wl,--dynamic-linker=/lib/ld-linux.so.3 -Wl,--allow-shlib-undefined \
  -Wl,--no-undefined -Wl,--as-needed -Wl,-z,noexecstack \
  -L"$dump/lib" -L"$dump/usr/lib" \
  -Wl,-rpath-link,"$dump/lib" -Wl,-rpath-link,"$dump/usr/lib" \
  "$root/start_hosted.S" "$out/one-still-probe.arm.o" \
  -o "$out/capture-shot" -Wl,--no-as-needed -l:libm.so.6 -l:libdevice-node.so.0.1 \
  -Wl,--as-needed -l:libc.so.6 -l:libdl.so.2 \
  -l:librt.so.1 -l:libgcc_s.so.1
"$readelf" -h -l -d "$out/capture-shot"
shasum -a 256 "$out/capture-shot"
