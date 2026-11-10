#!/bin/sh
# Cross-compile the read-only observation helpers. Does not run anything.
set -eu
root=$(CDPATH= cd -- "$(dirname "$0")" && pwd)
out=${1:-"$root/../../output/observe-build"}
mkdir -p "$out"
clang=/opt/homebrew/opt/llvm/bin/clang
for tool in rtoslog ipccrec threadsample accelprops accelrec accelserve fsynctee cpuhog pagewatch ptstop irqwatch regwatch; do
  "$clang" -target armv7a-linux-gnueabi -marm \
    -mcpu=cortex-a9 -O2 -fno-vectorize -fno-slp-vectorize \
    -ffreestanding -fno-builtin -fno-stack-protector \
    -fno-pic -fno-pie -nostdlib -static \
    -fuse-ld=lld --ld-path=/opt/homebrew/opt/llvm@17/bin/ld.lld \
    -Wall -Wextra -Werror -Wframe-larger-than=1024 \
    -Wl,-e,_start -Wl,--build-id=none -Wl,-z,noexecstack \
    "$root/$tool.c" "$root/../probes/capture-timeline-observer/start.S" -o "$out/$tool"
  shasum -a 256 "$out/$tool"
done
