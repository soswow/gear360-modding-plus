#!/bin/sh
# Cross-compile the on-camera lease prover for the Gear 360.
# Does not execute the ELF and does not touch the camera.
set -eu
root=$(CDPATH= cd -- "$(dirname "$0")" && pwd)
out=${1:-"$root/../../../output/ipcc-raw-grab-build"}
mkdir -p "$out"
clang=/opt/homebrew/opt/llvm/bin/clang
"$clang" -target armv7a-linux-gnueabi -marm \
  -mcpu=cortex-a9 -O2 -fno-vectorize -fno-slp-vectorize \
  -ffreestanding -fno-builtin -fno-stack-protector \
  -fno-pic -fno-pie -nostdlib -static \
  -fuse-ld=lld --ld-path=/opt/homebrew/opt/llvm@17/bin/ld.lld \
  -Wall -Wextra -Werror -Wframe-larger-than=2048 \
  -I"$root/../../../src/lib/still-request" -I"$root/../../../src/experimental/mcb-frame" \
  -Wl,-e,_start -Wl,--build-id=none -Wl,-z,noexecstack \
  "$root/ipcc_raw_grab.c" "$root/../../../src/lib/still-request/still_request.c" \
  "$root/../../../src/experimental/mcb-frame/mcb_frame.c" \
  "$root/start.S" -o "$out/ipcc-raw-grab.elf"
shasum -a 256 "$out/ipcc-raw-grab.elf"
