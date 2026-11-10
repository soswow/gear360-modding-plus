#!/bin/sh
# Build the open-flag probe for the camera, the same way the app is built:
# freestanding, against the camera's own libraries in the extracted dump.
set -eu
here=$(CDPATH= cd -- "$(dirname "$0")" && pwd)
lib=$here/../../lib
out=${1:-"$here/../../../output/camera-app-build"}
dump=${GEAR360_DUMP:-"$here/../../../dump/extracted/platform"}
clang=${CLANG:-/opt/homebrew/opt/llvm/bin/clang}
lld=${LLD:-/opt/homebrew/opt/llvm@17/bin/ld.lld}
mkdir -p "$out"
cflags="-target armv7a-linux-gnueabi -marm -mcpu=cortex-a9 -std=c11 -O2 -fno-builtin -fno-stack-protector -ffreestanding -Wall -Wextra -Werror -nostdlib -fno-pie -I$lib/capture-session"
# shellcheck disable=SC2086
"$clang" $cflags -c "$here/open_flags_probe.c" -o "$out/open_flags_probe.o"
# shellcheck disable=SC2086
"$clang" $cflags -no-pie -fuse-ld=lld --ld-path="$lld" -Wl,--dynamic-linker=/lib/ld-linux.so.3 -Wl,--allow-shlib-undefined -Wl,--no-undefined -Wl,-z,noexecstack -L"$dump/lib" -L"$dump/usr/lib" -Wl,-rpath-link,"$dump/lib" -Wl,-rpath-link,"$dump/usr/lib" "$lib/capture-session/start_hosted.S" "$out/open_flags_probe.o" -o "$out/open-flags-probe" -Wl,--no-as-needed -l:libm.so.6 -l:libdevice-node.so.0.1 -Wl,--as-needed -l:libpthread.so.0 -l:libc.so.6 -l:libdl.so.2 -l:librt.so.1 -l:libgcc_s.so.1
shasum -a 256 "$out/open-flags-probe"
