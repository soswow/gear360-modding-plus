#!/bin/sh
# Build the log-window scan probe (shared-memory reads only; no capture calls).
set -eu
root=$(CDPATH= cd -- "$(dirname "$0")/.." && pwd)
camera=$(CDPATH= cd -- "$(dirname "$0")" && pwd)
out=${1:-"$root/../../../output/log-window-scan-build"}
dump=${GEAR360_DUMP:-"$root/../../../dump/extracted/platform"}
clang=${CLANG:-/opt/homebrew/opt/llvm/bin/clang}
lld=${LLD:-/opt/homebrew/opt/llvm@17/bin/ld.lld}
readelf=${READELF:-/opt/homebrew/opt/llvm/bin/llvm-readelf}
mkdir -p "$out"
"$clang" -target armv7a-linux-gnueabi -marm -mcpu=cortex-a9 \
  -std=c99 -O2 -fno-builtin -fno-stack-protector -ffreestanding \
  -Wall -Wextra -Werror -nostdlib -fno-pie -I"$root" -c \
  "$camera/log-window-scan.c" -o "$out/log-window-scan.arm.o"
"$clang" -target armv7a-linux-gnueabi -marm -mcpu=cortex-a9 \
  -std=c99 -O2 -fno-builtin -fno-stack-protector -ffreestanding \
  -Wall -Wextra -Werror -nostdlib -fno-pie -no-pie \
  -fuse-ld=lld --ld-path="$lld" \
  -Wl,--dynamic-linker=/lib/ld-linux.so.3 -Wl,--allow-shlib-undefined \
  -Wl,--no-undefined -Wl,--as-needed -Wl,-z,noexecstack \
  -L"$dump/lib" -L"$dump/usr/lib" \
  -Wl,-rpath-link,"$dump/lib" -Wl,-rpath-link,"$dump/usr/lib" \
  "$root/start_hosted.S" "$out/log-window-scan.arm.o" \
  -o "$out/log-window-scan" -l:libc.so.6 -l:libdl.so.2 \
  -l:librt.so.1 -l:libgcc_s.so.1
"$readelf" -h -d "$out/log-window-scan"
shasum -a 256 "$out/log-window-scan"
