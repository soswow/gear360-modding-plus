#!/bin/sh
# Host build and tests for the stock-behaviour producer reference model.
#
# Everything here runs on the host with the system compilers.  Nothing in this
# script executes on the camera, and nothing is built for ARM unless the
# caller asks for it explicitly with CHECK_ARM=1.
set -eu

root=$(CDPATH= cd -- "$(dirname "$0")" && pwd)
out=${1:-"$root/../../../output/receive-contract-build"}
mkdir -p "$out"

sanitize_flags=
if [ "${SANITIZE:-0}" = 1 ]; then
  sanitize_flags='-fsanitize=address,undefined'
fi

# shellcheck disable=SC2086
"${CC:-cc}" -std=c11 -Wall -Wextra -Werror -pedantic $sanitize_flags -I"$root" \
  -c "$root/mcb_producer_ring.c" -o "$out/mcb_producer_ring.o"
"${AR:-ar}" rcs "$out/libmcb_producer_ring.a" "$out/mcb_producer_ring.o"
# shellcheck disable=SC2086
"${CC:-cc}" -std=c11 -Wall -Wextra -Werror -pedantic $sanitize_flags -I"$root" \
  "$root/tests/test_mcb_producer_ring.c" "$out/libmcb_producer_ring.a" $sanitize_flags \
  -o "$out/test_mcb_producer_ring"
# shellcheck disable=SC2086
"${CXX:-c++}" -std=c++11 -Wall -Wextra -Werror -pedantic $sanitize_flags -I"$root" \
  "$root/tests/test_cpp_linkage.cpp" "$out/libmcb_producer_ring.a" $sanitize_flags \
  -o "$out/test_cpp_linkage"
"$out/test_mcb_producer_ring"
"$out/test_cpp_linkage"

# Optional ARMv7 compile check.  Not run by default: the parent builds and
# validates the ARM target during integration.
if [ "${CHECK_ARM:-0}" = 1 ]; then
  "${ARM_CLANG:-clang}" -target armv7a-linux-gnueabi -mcpu=cortex-a9 -marm \
    -ffreestanding -fno-builtin -Wall -Wextra -Werror -std=c11 \
    -I"$root" -c "$root/mcb_producer_ring.c" -o "$out/mcb_producer_ring.armv7.o"
fi
