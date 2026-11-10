#!/bin/sh
# Cross-compile the read-only capture timeline observer. Does not execute the ELF.
set -eu
root=$(CDPATH= cd -- "$(dirname "$0")" && pwd)
out=${1:-"$root/../../../output/capture-timeline-observer-build"}
mkdir -p "$out"
clang=/opt/homebrew/opt/llvm/bin/clang
"$clang" -target armv7a-linux-gnueabi -marm \
  -mcpu=cortex-a9 -O2 -fno-vectorize -fno-slp-vectorize \
  -ffreestanding -fno-builtin -fno-stack-protector \
  -fno-pic -fno-pie -nostdlib -static \
  -fuse-ld=lld --ld-path=/opt/homebrew/opt/llvm@17/bin/ld.lld \
  -Wall -Wextra -Werror -Wframe-larger-than=1024 \
  -Wl,-e,_start -Wl,--build-id=none -Wl,-z,noexecstack \
  "$root/capture_timeline.c" "$root/start.S" -o "$out/capture-timeline.elf"
shasum -a 256 "$out/capture-timeline.elf"
