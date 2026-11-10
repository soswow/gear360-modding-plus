#!/bin/sh
# Build the existing renderer for the camera without the stock-app mirror.
# Offline only; no display operation or capture SDK dependency.
set -eu
here=$(CDPATH= cd -- "$(dirname "$0")" && pwd)
re=$(CDPATH= cd -- "$here/.." && pwd)
out=${1:-"$re/../output/experiment-display-build"}
dump=${GEAR360_DUMP:-"$re/../dump/extracted/platform"}
clang=${CLANG:-/opt/homebrew/opt/llvm/bin/clang}
lld=${LLD:-/opt/homebrew/opt/llvm@17/bin/ld.lld}
mkdir -p "$out"
"$clang" -target armv7a-linux-gnueabi -marm -mcpu=cortex-a9 -std=c11 -O2 \
    -fno-builtin -fno-stack-protector -ffreestanding -Wall -Wextra -Werror \
    -nostdlib -fno-pie -no-pie -I"$re/../src/lib/capture-session" \
    -DG360_CAMERA_LIBC -DHDR_EXPERIMENTAL_DRIVER -DHDR_NO_STOCK_MIRROR \
    -fuse-ld=lld --ld-path="$lld" -Wl,--dynamic-linker=/lib/ld-linux.so.3 \
    -Wl,--no-undefined -Wl,-z,noexecstack -L"$dump/lib" \
    "$re/../src/lib/capture-session/start_hosted.S" "$here/gear360-display.c" \
    -o "$out/gear360-display" -l:libc.so.6 -l:libgcc_s.so.1
shasum -a 256 "$out/gear360-display"
