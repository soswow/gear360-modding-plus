#!/bin/sh
# Cross-compile the standalone platform self-check for the camera.
#
# This program is the safe one to run on the camera: it links nothing but
# the camera's own C library, the clock library and the compiler's
# runtime, together with our startup code.  It does not link the capture
# session, the capture library host, or the camera's own capture
# library, and it does not link the dynamic loader's library either,
# because it never opens anything.
#
# The build checks that: the program may only need libc, librt and the
# compiler's runtime, and the link fails if any symbol is missing from
# the camera's libraries.
#
# Nothing here touches the camera.  The only inputs are the pinned
# source and the pinned dump.
set -eu

root=$(CDPATH= cd -- "$(dirname "$0")" && pwd)
out=${1:-"$root/../../../output/capture-platform-build"}
dump=${GEAR360_DUMP:-$root/../../../dump/extracted/platform}
clang=${CLANG:-/opt/homebrew/opt/llvm/bin/clang}
lld=${LLD:-/opt/homebrew/opt/llvm@17/bin/ld.lld}
readelf=${READELF:-/opt/homebrew/opt/llvm/bin/llvm-readelf}

mkdir -p "$out"

if [ ! -d "$dump/lib" ]; then
    echo "the camera's libraries are not in $dump" >&2
    echo "set GEAR360_DUMP to the extracted platform directory" >&2
    exit 1
fi

for library in libc.so.6 librt.so.1 libgcc_s.so.1; do
    if [ ! -e "$dump/lib/$library" ] && [ ! -e "$dump/usr/lib/$library" ]; then
        echo "the camera's $library is not in $dump" >&2
        exit 1
    fi
done
if [ ! -e "$dump/lib/ld-linux.so.3" ]; then
    echo "the camera's loader /lib/ld-linux.so.3 is not in $dump/lib" >&2
    exit 1
fi

"$clang" -target armv7a-linux-gnueabi -marm -mcpu=cortex-a9 \
    -O2 -fno-builtin -fno-stack-protector -nostdlib \
    -fno-pie -no-pie \
    -fuse-ld=lld --ld-path="$lld" \
    -Wl,--dynamic-linker=/lib/ld-linux.so.3 \
    -Wl,--allow-shlib-undefined \
    -Wl,--no-undefined \
    -Wl,--as-needed \
    -L"$dump/lib" -L"$dump/usr/lib" \
    -Wl,-rpath-link,"$dump/lib" -Wl,-rpath-link,"$dump/usr/lib" \
    -I"$root" \
    "$root/start_hosted.S" \
    "$root/platform-self-check.c" \
    -o "$out/platform-self-check" \
    -l:libc.so.6 -l:librt.so.1 -l:libgcc_s.so.1

# What was built, in a form that can be read without a disassembler.
elf_type=$("$readelf" -h "$out/platform-self-check" |
    awk '/Type:/ {print $2; exit}')
if [ "$elf_type" != "EXEC" ]; then
    echo "the built program is $elf_type, not a fixed-address EXEC" >&2
    exit 1
fi
"$readelf" -h "$out/platform-self-check" |
    awk '/Entry point/ {print "entry " $NF}'
"$readelf" -l "$out/platform-self-check" |
    awk '/interpreter/ {gsub(/]/, "", $NF); print "interpreter " $NF}'
"$readelf" -d "$out/platform-self-check" |
    awk '/NEEDED/ {print "needed " $NF}'

# The program must not have picked up anything beyond the libraries it
# is allowed to need: the C library, the clock library, and the
# compiler's runtime.  In particular it must not need the loader's
# library, the threading library or the capture library.
needed=$("$readelf" -d "$out/platform-self-check" |
    awk '/NEEDED/ {gsub(/[][]/, "", $NF); print $NF}' | tr '\n' ' ')
for library in $needed; do
    case "$library" in
        libc.so.6|librt.so.1|libgcc_s.so.1) ;;
        *)
            echo "the built program needs $library, which it must not" >&2
            exit 1
            ;;
    esac
done
for library in libc.so.6 librt.so.1; do
    case " $needed " in
        *" $library "*) ;;
        *)
            echo "the built program does not need $library" >&2
            exit 1
            ;;
    esac
done

# The program must not have any constructors of its own: the startup
# code passes a null init and a null fini, which is only right while
# there is nothing for the loader to run.
for section in .init_array .fini_array .ctors .dtors; do
    if "$readelf" -S "$out/platform-self-check" | grep -qF "$section"; then
        echo "the built program has a $section section; the startup code's" >&2
        echo "null init and fini would then be wrong" >&2
        exit 1
    fi
done

shasum -a 256 "$out/platform-self-check"
