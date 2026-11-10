#!/bin/sh
# Cross-compile only. This script does not execute the ELF.
set -eu
root=$(CDPATH= cd -- "$(dirname "$0")" && pwd)
out=${1:-"$root/../../../output/allocator-record-reader-build"}
mkdir -p "$out"
clang=/opt/homebrew/opt/llvm/bin/clang
readelf=/opt/homebrew/opt/llvm/bin/llvm-readelf
"$clang" -target armv7a-linux-gnueabi -marm \
  -mcpu=cortex-a9 -ffreestanding -fno-builtin -fno-stack-protector \
  -fno-pic -fno-pie -nostdlib -static \
  -fuse-ld=lld --ld-path=/opt/homebrew/opt/llvm@17/bin/ld.lld \
  -Wall -Wextra -Werror -Wframe-larger-than=1024 \
  -Wl,-e,_start -Wl,--build-id=none -Wl,-z,noexecstack \
  "$root/allocator_records.c" "$root/start.S" -o "$out/allocator-record-reader.elf"
"$readelf" -h -l -S "$out/allocator-record-reader.elf"
