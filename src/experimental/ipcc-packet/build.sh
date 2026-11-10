#!/bin/sh
#
# build.sh - build the IPCC packet ABI adapter, its host regression tests and
# the C++ linkage check.
#
# This script is pure build tooling. It performs no ioctl(), no open(), no
# device access and no helper execution, and it installs nothing.
#
# Usage:
#     ./build.sh [output-directory]
#
#     output-directory   Where the artefacts are written.
#                        Default: "$root/../../../output/ipcc-packet-build"
#                        (the layout expected after integration).  Pass an
#                        explicit directory to keep the build inside a
#                        working tree.
#
# Environment overrides:
#     CC         C compiler for the library and the host tests   (default: cc)
#     CXX        C++ compiler for the linkage check             (default: c++)
#     AR         archiver for the static library               (default: ar)
#     ARM_CLANG  clang used for the ARMv7 compile check        (default: clang)
#     SANITIZE=1  also build and run an ASan/UBSan host test
#     CHECK_ARM=1 also compile the library for armv7a-linux-gnueabi
#                 (compile check only, never executed here)
#
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
out=${1:-"$root/../../../output/ipcc-packet-build"}

CC=${CC:-cc}
CXX=${CXX:-c++}
AR=${AR:-ar}
ARM_CLANG=${ARM_CLANG:-clang}
SANITIZE=${SANITIZE:-0}
CHECK_ARM=${CHECK_ARM:-0}

CSTD="-std=c11"
CWARN="-Wall -Wextra -Wpedantic -Werror"
CXXSTD="-std=c++11"
CXXWARN="-Wall -Wextra -Wpedantic -Werror"

echo "build.sh: root      = $root"
echo "build.sh: output    = $out"
echo "build.sh: CC=$CC CXX=$CXX AR=$AR ARM_CLANG=$ARM_CLANG"
echo "build.sh: SANITIZE=$SANITIZE CHECK_ARM=$CHECK_ARM"

mkdir -p "$out"

# ---- static library -------------------------------------------------------
"$CC" $CSTD $CWARN -I"$root" -c "$root/ipcc_packet_abi.c" -o "$out/ipcc_packet_abi.o"
"$AR" rcs "$out/libipcc_packet_abi.a" "$out/ipcc_packet_abi.o"
echo "build.sh: library   = $out/libipcc_packet_abi.a"

# ---- host regression tests ------------------------------------------------
"$CC" $CSTD $CWARN -I"$root" -c "$root/test_ipcc_packet_abi.c" -o "$out/test_ipcc_packet_abi.o"
"$CC" $CSTD $CWARN -o "$out/test_ipcc_packet_abi" \
    "$out/test_ipcc_packet_abi.o" "$out/libipcc_packet_abi.a"
"$out/test_ipcc_packet_abi"
echo "build.sh: host tests ok"

# ---- C++ linkage check ----------------------------------------------------
"$CXX" $CXXSTD $CXXWARN -I"$root" -c "$root/test_cpp_linkage.cpp" -o "$out/test_cpp_linkage.o"
"$CXX" $CXXSTD $CXXWARN -o "$out/test_cpp_linkage" \
    "$out/test_cpp_linkage.o" "$out/libipcc_packet_abi.a"
"$out/test_cpp_linkage"
echo "build.sh: c++ linkage ok"

# ---- optional sanitizer run ----------------------------------------------
if [ "$SANITIZE" = "1" ]; then
    "$CC" $CSTD $CWARN -O1 -g -fsanitize=address,undefined -I"$root" \
        -o "$out/test_ipcc_packet_abi_san" \
        "$root/ipcc_packet_abi.c" "$root/test_ipcc_packet_abi.c"
    "$out/test_ipcc_packet_abi_san"
    echo "build.sh: sanitizer run ok"
fi

# ---- optional ARMv7 compile check (compile only, never executed) ----------
if [ "$CHECK_ARM" = "1" ]; then
    "$ARM_CLANG" -target armv7a-linux-gnueabi -mcpu=cortex-a9 -marm -ffreestanding \
        -fno-builtin $CWARN $CSTD -I"$root" \
        -c "$root/ipcc_packet_abi.c" -o "$out/ipcc_packet_abi.armv7.o"
    echo "build.sh: armv7 compile check ok = $out/ipcc_packet_abi.armv7.o"
fi

echo "build.sh: done -> $out"
