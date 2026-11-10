#!/bin/sh
set -eu
root=$(CDPATH= cd -- "$(dirname "$0")" && pwd)
out=${1:-"$root/../../../output/preview-sample-build"}
mkdir -p "$out"
/opt/homebrew/opt/llvm/bin/clang -target armv7a-linux-gnueabi -marm \
  -mcpu=cortex-a9 -ffreestanding -fno-builtin -fno-stack-protector \
  -fno-pic -fno-pie -nostdlib -static \
  -fuse-ld=lld --ld-path=/opt/homebrew/opt/llvm@17/bin/ld.lld \
  -Wall -Wextra -Werror -Wl,-e,_start -Wl,--build-id=none \
  -Wl,-z,noexecstack "$root/preview_sample.c" "$root/start.S" \
  -o "$out/preview-sample-reader.elf"
/opt/homebrew/opt/llvm/bin/llvm-readelf -h -l "$out/preview-sample-reader.elf"
