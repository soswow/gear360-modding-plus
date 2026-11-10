#!/bin/sh
set -eu

root=$(CDPATH= cd -- "$(dirname "$0")" && pwd)
out=${1:-"$root/../../../output/encoded-set-build"}
mkdir -p "$out"

sanitize_flags=
if [ "${SANITIZE:-0}" = 1 ]; then
  sanitize_flags='-fsanitize=address,undefined'
fi

# shellcheck disable=SC2086
"${CC:-cc}" -std=c99 -Wall -Wextra -Werror -pedantic $sanitize_flags -I"$root" \
  -c "$root/encoded_set.c" -o "$out/encoded_set.o"
"${AR:-ar}" rcs "$out/libencoded_set.a" "$out/encoded_set.o"

# shellcheck disable=SC2086
"${CC:-cc}" -std=c99 -Wall -Wextra -Werror -pedantic $sanitize_flags -I"$root" \
  "$root/tests/test_encoded_set.c" "$out/libencoded_set.a" $sanitize_flags \
  -o "$out/test_encoded_set"

# shellcheck disable=SC2086
"${CXX:-c++}" -std=c++11 -Wall -Wextra -Werror -pedantic $sanitize_flags \
  -I"$root" \
  "$root/tests/test_cpp_linkage.cpp" "$out/libencoded_set.a" $sanitize_flags \
  -o "$out/test_cpp_linkage"

"$out/test_encoded_set"
"$out/test_cpp_linkage"

if [ "${CHECK_ARM:-0}" = 1 ]; then
  "${ARM_CLANG:-clang}" -target armv7a-linux-gnueabi -mcpu=cortex-a9 -marm \
    -ffreestanding -fno-builtin -Wall -Wextra -Werror -std=c99 \
    -I"$root" -c "$root/encoded_set.c" -o "$out/encoded_set.armv7.o"
fi
