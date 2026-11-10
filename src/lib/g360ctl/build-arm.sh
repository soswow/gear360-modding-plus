#!/bin/sh
# Cross-build g360-ctl for the camera (ARMv7, its glibc 2.13 without headers).
# Does not contact the camera.
set -eu
here=$(CDPATH= cd -- "$(dirname "$0")" && pwd)
re=$(CDPATH= cd -- "$here/.." && pwd)
out=${1:-"$re/../../output/g360ctl-build"}
dump=${GEAR360_DUMP:-"$re/../../dump/extracted/platform"}
clang=${CLANG:-/opt/homebrew/opt/llvm/bin/clang}
lld=${LLD:-/opt/homebrew/opt/llvm@17/bin/ld.lld}
mkdir -p "$out"
"$clang" -target armv7a-linux-gnueabi -marm -mcpu=cortex-a9 -std=c99 -O2 \
    -fno-builtin -fno-stack-protector -ffreestanding -Wall -Wextra -Werror \
    -nostdlib -fno-pie -no-pie -I"$re/capture-session" -I"$here" \
    -fuse-ld=lld --ld-path="$lld" -Wl,--dynamic-linker=/lib/ld-linux.so.3 \
    -Wl,--no-undefined -Wl,-z,noexecstack -L"$dump/lib" \
    "$re/capture-session/start_hosted.S" "$here/g360ctl.c" "$here/g360-ctl.c" \
    -o "$out/g360-ctl" -l:libc.so.6 -l:librt.so.1 -l:libgcc_s.so.1
shasum -a 256 "$out/g360-ctl"
