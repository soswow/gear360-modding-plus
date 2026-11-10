#!/bin/sh
set -eu

root=$(CDPATH= cd -- "$(dirname "$0")" && pwd)
out=${1:-"$root/../../../output/mcb-frame-build"}
mkdir -p "$out"

sanitize_flags=
if [ "${SANITIZE:-0}" = 1 ]; then
  sanitize_flags='-fsanitize=address,undefined'
fi

# shellcheck disable=SC2086
"${CC:-cc}" -std=c11 -Wall -Wextra -Werror -pedantic $sanitize_flags -I"$root" \
  -c "$root/mcb_frame.c" -o "$out/mcb_frame.o"
# shellcheck disable=SC2086
"${CC:-cc}" -std=c11 -Wall -Wextra -Werror -pedantic $sanitize_flags -I"$root" \
  -c "$root/mcb_ring.c" -o "$out/mcb_ring.o"
"${AR:-ar}" rcs "$out/libmcb_frame.a" "$out/mcb_frame.o" "$out/mcb_ring.o"
# shellcheck disable=SC2086
"${CC:-cc}" -std=c11 -Wall -Wextra -Werror -pedantic $sanitize_flags -I"$root" \
  "$root/tests/test_mcb_frame.c" "$out/libmcb_frame.a" $sanitize_flags \
  -o "$out/test_mcb_frame"
# shellcheck disable=SC2086
"${CC:-cc}" -std=c11 -Wall -Wextra -Werror -pedantic $sanitize_flags -I"$root" \
  "$root/tests/test_mcb_ring.c" "$out/libmcb_frame.a" $sanitize_flags \
  -o "$out/test_mcb_ring"
# shellcheck disable=SC2086
"${CXX:-c++}" -std=c++11 -Wall -Wextra -Werror -pedantic $sanitize_flags -I"$root" \
  "$root/tests/test_cpp_linkage.cpp" "$out/libmcb_frame.a" $sanitize_flags \
  -o "$out/test_cpp_linkage"
# Check the boundary between the recovered request body and transport envelope.
# shellcheck disable=SC2086
"${CC:-cc}" -std=c11 -Wall -Wextra -Werror -pedantic $sanitize_flags \
  -I"$root" -I"$root/../../lib/still-request" \
  "$root/tests/test_still_envelope.c" "$root/../../lib/still-request/still_request.c" \
  "$out/libmcb_frame.a" $sanitize_flags -o "$out/test_still_envelope"
"$out/test_mcb_frame"
"$out/test_mcb_ring"
"$out/test_cpp_linkage"
"$out/test_still_envelope"

if [ "${CHECK_ARM:-0}" = 1 ]; then
  "${ARM_CLANG:-clang}" -target armv7a-linux-gnueabi -mcpu=cortex-a9 -marm \
    -ffreestanding -fno-builtin -Wall -Wextra -Werror -std=c11 \
    -I"$root" -c "$root/mcb_frame.c" -o "$out/mcb_frame.armv7.o"
  "${ARM_CLANG:-clang}" -target armv7a-linux-gnueabi -mcpu=cortex-a9 -marm \
    -ffreestanding -fno-builtin -Wall -Wextra -Werror -std=c11 \
    -I"$root" -c "$root/mcb_ring.c" -o "$out/mcb_ring.armv7.o"
fi
