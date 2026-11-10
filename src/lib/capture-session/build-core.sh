#!/bin/sh
# Compile portable core for the camera ABI, without linking or running it.
set -eu
root=$(CDPATH= cd -- "$(dirname "$0")" && pwd)
out=${1:-"$root/../../../output/capture-session-arm-core"}
clang=${CLANG:-/opt/homebrew/opt/llvm/bin/clang}
mkdir -p "$out"
"$clang" -target armv7a-linux-gnueabi -marm -mcpu=cortex-a9 -ffreestanding -fno-builtin -fno-stack-protector -std=c99 -Wall -Wextra -Werror -O2 -I"$root" -I"$root/../still-request" -c "$root/capture_session.c" -o "$out/capture_session.o"
"$clang" -target armv7a-linux-gnueabi -marm -mcpu=cortex-a9 -ffreestanding -fno-builtin -std=c99 -Wall -Wextra -Werror -O2 -I"$root/../still-request" -c "$root/../still-request/still_request.c" -o "$out/still_request.o"
