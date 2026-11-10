#!/bin/sh
# Link the experimental host against the pinned camera libraries. No execution.
set -eu
root=$(CDPATH= cd -- "$(dirname "$0")" && pwd)
out=${1:-"$root/../../../output/capture-library-host-build"}
dump=${GEAR360_DUMP:-"$root/../../../dump/extracted/platform"}
clang=${CLANG:-/opt/homebrew/opt/llvm/bin/clang}
lld=${LLD:-/opt/homebrew/opt/llvm@17/bin/ld.lld}
readelf=${READELF:-/opt/homebrew/opt/llvm/bin/llvm-readelf}
sh "$root/build-arm.sh" "$out"
"$clang" -target armv7a-linux-gnueabi -marm -mcpu=cortex-a9 \
 -O2 -fno-builtin -fno-stack-protector -nostdlib -fno-pie -no-pie \
 -fuse-ld=lld --ld-path="$lld" \
 -Wl,--dynamic-linker=/lib/ld-linux.so.3 -Wl,--allow-shlib-undefined \
 -Wl,--no-undefined -Wl,--as-needed \
 -L"$dump/lib" -L"$dump/usr/lib" \
 -Wl,-rpath-link,"$dump/lib" -Wl,-rpath-link,"$dump/usr/lib" \
 "$root/start_hosted.S" "$out/capture_session.arm.o" \
 "$out/capture_session_library.arm.o" "$out/capture-shot.arm.o" \
 "$out/still_request.arm.o" "$out/encoded_set.arm.o" \
 -o "$out/capture-shot" -l:libc.so.6 -l:libdl.so.2 \
 -l:librt.so.1 -l:libgcc_s.so.1
"$readelf" -h -l -d "$out/capture-shot"
shasum -a 256 "$out/capture-shot"
