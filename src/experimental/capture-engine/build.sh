#!/bin/sh
# Build the capture-engine components and run their host checks.
# Set CHECK_ARM=1 (and optionally ARM_CLANG) to also compile ARMv7 objects.
set -eu

root=$(CDPATH= cd -- "$(dirname "$0")" && pwd)
out=${1:-"$root/../../../output/capture-engine-build"}
mkdir -p "$out"

flags="-std=c11 -Wall -Wextra -Werror -pedantic -I$root"
for source in pool_monitor raw_format samsung3; do
  "${CC:-cc}" $flags -c "$root/$source.c" -o "$out/$source.o"
done
"${AR:-ar}" rcs "$out/libg360_capture_engine.a" "$out/pool_monitor.o" "$out/raw_format.o" "$out/samsung3.o"
"${CC:-cc}" $flags "$root/tests/test_engine.c" "$out/libg360_capture_engine.a" -o "$out/test_engine"
"${CXX:-c++}" -std=c++11 -Wall -Wextra -Werror -pedantic -I"$root" \
  "$root/tests/test_cpp_linkage.cpp" "$out/libg360_capture_engine.a" -o "$out/test_cpp_linkage"
"${CC:-cc}" $flags "$root/tools/g360_dng_wrap.c" "$out/libg360_capture_engine.a" -o "$out/g360-dng-wrap"
"$out/test_engine"
"$out/test_cpp_linkage"
"${PYTHON:-python3}" "$root/tests/test_samsung3_decode.py"

if [ "${CHECK_ARM:-0}" = 1 ]; then
  for source in pool_monitor raw_format samsung3; do
    "${ARM_CLANG:-clang}" -target armv7a-linux-gnueabi -mcpu=cortex-a9 -marm \
      -ffreestanding -fno-builtin -Wall -Wextra -Werror -std=c11 \
      -I"$root" -c "$root/$source.c" -o "$out/$source.armv7.o"
  done
fi
