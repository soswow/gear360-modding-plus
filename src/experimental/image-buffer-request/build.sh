#!/bin/sh
set -eu

root=$(CDPATH= cd -- "$(dirname "$0")" && pwd)
mcb="$root/../mcb-frame"
out=${1:-"$root/../../../output/image-buffer-request-build"}
mkdir -p "$out"

sanitize_flags=
if [ "${SANITIZE:-0}" = 1 ]; then
  sanitize_flags='-fsanitize=address,undefined'
fi

# The framing dependency is built here rather than by running its own build
# script, so this component can be validated on its own.
# shellcheck disable=SC2086
"${CC:-cc}" -std=c11 -Wall -Wextra -Werror -pedantic $sanitize_flags -I"$mcb" \
  -c "$mcb/mcb_frame.c" -o "$out/mcb_frame.o"
"${AR:-ar}" rcs "$out/libmcb_frame.a" "$out/mcb_frame.o"

# shellcheck disable=SC2086
"${CC:-cc}" -std=c11 -Wall -Wextra -Werror -pedantic $sanitize_flags \
  -I"$root" -I"$mcb" \
  -c "$root/image_buffer_request.c" -o "$out/image_buffer_request.o"
"${AR:-ar}" rcs "$out/libimage_buffer_request.a" "$out/image_buffer_request.o"

# shellcheck disable=SC2086
"${CC:-cc}" -std=c11 -Wall -Wextra -Werror -pedantic $sanitize_flags \
  -I"$root" -I"$mcb" \
  "$root/tests/test_image_buffer_request.c" "$out/libimage_buffer_request.a" \
  "$out/libmcb_frame.a" $sanitize_flags -o "$out/test_image_buffer_request"

# shellcheck disable=SC2086
"${CXX:-c++}" -std=c++11 -Wall -Wextra -Werror -pedantic $sanitize_flags \
  -I"$root" -I"$mcb" \
  "$root/tests/test_cpp_linkage.cpp" "$out/libimage_buffer_request.a" \
  "$out/libmcb_frame.a" $sanitize_flags -o "$out/test_cpp_linkage"

"$out/test_image_buffer_request"
"$out/test_cpp_linkage"

if [ "${CHECK_ARM:-0}" = 1 ]; then
  "${ARM_CLANG:-clang}" -target armv7a-linux-gnueabi -mcpu=cortex-a9 -marm \
    -ffreestanding -fno-builtin -Wall -Wextra -Werror -std=c11 \
    -I"$mcb" -c "$mcb/mcb_frame.c" -o "$out/mcb_frame.armv7.o"
  "${ARM_CLANG:-clang}" -target armv7a-linux-gnueabi -mcpu=cortex-a9 -marm \
    -ffreestanding -fno-builtin -Wall -Wextra -Werror -std=c11 \
    -I"$root" -I"$mcb" \
    -c "$root/image_buffer_request.c" -o "$out/image_buffer_request.armv7.o"
fi
