#!/bin/sh
set -eu

root=$(CDPATH= cd -- "$(dirname "$0")" && pwd)
out=${1:-"$root/../../../output/still-request-build"}
mkdir -p "$out"

"${CC:-cc}" -std=c11 -Wall -Wextra -Werror -pedantic -I"$root" \
  -c "$root/still_request.c" -o "$out/still_request.o"
"${AR:-ar}" rcs "$out/libstill_request.a" "$out/still_request.o"
"${CC:-cc}" -std=c11 -Wall -Wextra -Werror -pedantic -I"$root" \
  "$root/tests/test_still_request.c" "$out/libstill_request.a" \
  -o "$out/test_still_request"
"${CXX:-c++}" -std=c++11 -Wall -Wextra -Werror -pedantic -I"$root" \
  "$root/tests/test_cpp_linkage.cpp" "$out/libstill_request.a" \
  -o "$out/test_cpp_linkage"
"$out/test_still_request"
"$out/test_cpp_linkage"

if [ "${CHECK_ARM:-0}" = 1 ]; then
  "${ARM_CLANG:-clang}" -target armv7a-linux-gnueabi -mcpu=cortex-a9 -marm \
    -ffreestanding -fno-builtin -Wall -Wextra -Werror -std=c11 \
    -I"$root" -c "$root/still_request.c" -o "$out/still_request.armv7.o"
fi
