#!/bin/sh
# Cross-compile the capture host's sources for the camera.
#
# **This script compiles objects only.**  It does not link a program, it does
# not run anything, and it needs nothing from the extracted filesystem dump:
# the camera's C library is installed without its headers, so every
# declaration the sources need comes from capture_libc.h and the compiler's
# own freestanding headers.
#
# The final ARM program - the link against the camera's own libraries, with
# the startup code in start_hosted.S and the capture library resolved by the
# camera's own loader at run time - is the **parent's** step, and it needs
# the pinned dump, which is outside this packet.  What this script proves is
# that the sources compile for the target with the target's declarations.
#
# Nothing here touches the camera.
set -eu

root=$(CDPATH= cd -- "$(dirname "$0")" && pwd)
out=${1:-"$root/../../../output/capture-host-build"}
clang=${CLANG:-/opt/homebrew/opt/llvm/bin/clang}

mkdir -p "$out"

target="-target armv7a-linux-gnueabi -marm -mcpu=cortex-a9"
cflags="-std=c99 -O2 -fno-builtin -fno-stack-protector -ffreestanding -Wall -Wextra -Werror"
includes="-I$root -I$root/../still-request -I$root/../encoded-set"

# shellcheck disable=SC2086
for source in capture_session.c capture_session_library.c capture-shot.c; do
    object="$out/${source%.c}.arm.o"
    # shellcheck disable=SC2086
    "$clang" $target $cflags $includes -c "$root/$source" -o "$object"
done

# shellcheck disable=SC2086
"$clang" $target $cflags -I"$root/../still-request" \
    -c "$root/../still-request/still_request.c" -o "$out/still_request.arm.o"
# shellcheck disable=SC2086
"$clang" $target $cflags -I"$root/../encoded-set" \
    -c "$root/../encoded-set/encoded_set.c" -o "$out/encoded_set.arm.o"

shasum -a 256 "$out"/*.arm.o
