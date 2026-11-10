#!/bin/sh
# Build two SDK-free payloads. This script never contacts the camera.
set -eu
root=$(CDPATH= cd -- "$(dirname "$0")" && pwd)
out=${1:-"$root/../../../../output/capture-watchdog-build"}
dump=${GEAR360_DUMP:-"$root/../../../../dump/extracted/platform"}
clang=${CLANG:-/opt/homebrew/opt/llvm/bin/clang}
lld=${LLD:-/opt/homebrew/opt/llvm@17/bin/ld.lld}
readelf=${READELF:-/opt/homebrew/opt/llvm/bin/llvm-readelf}
mkdir -p "$out"
for hang in 0 1; do
    binary="$out/watchdog-payload-$hang"
    "$clang" -target armv7a-linux-gnueabi -marm -mcpu=cortex-a9 \
        -std=c99 -O2 -Wall -Wextra -Werror -fno-builtin -fno-stack-protector \
        -ffreestanding -nostdlib -fno-pie -no-pie -fuse-ld=lld --ld-path="$lld" \
        -Wl,--dynamic-linker=/lib/ld-linux.so.3 -Wl,--allow-shlib-undefined \
        -Wl,--no-undefined -Wl,--as-needed \
        -L"$dump/lib" -L"$dump/usr/lib" \
        -Wl,-rpath-link,"$dump/lib" -Wl,-rpath-link,"$dump/usr/lib" \
        -I"$root/.." -DWATCHDOG_TEST_HANG="$hang" \
        "$root/../start_hosted.S" "$root/watchdog-payload.c" \
        -o "$binary" -l:libc.so.6 -l:libgcc_s.so.1
    "$readelf" -h "$binary" | awk '/Class:|Machine:|Type:|Entry point/'
    "$readelf" -l "$binary" | awk '/interpreter/'
    needed=$("$readelf" -d "$binary" | awk '/NEEDED/ {gsub(/[][]/, "", $NF); print $NF}')
    for library in $needed; do
        case "$library" in
            libc.so.6|libgcc_s.so.1) ;;
            *) echo "unexpected dependency: $library" >&2; exit 1 ;;
        esac
    done
    for section in .init_array .fini_array .ctors .dtors; do
        if "$readelf" -S "$binary" | grep -qF "$section"; then
            echo "unexpected constructor section: $section" >&2
            exit 1
        fi
    done
    "$readelf" -d "$binary" | awk '/NEEDED/'
    shasum -a 256 "$binary"
done
