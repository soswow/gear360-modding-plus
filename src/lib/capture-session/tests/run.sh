#!/bin/sh
# Portable session policy/ownership checks. No target library/device is opened.
set -eu
root=$(CDPATH= cd -- "$(dirname "$0")/.." && pwd)
out=${1:-"$root/../../../output/capture-session-core-build"}
mkdir -p "$out"
cc=${CC:-cc}
cxx=${CXX:-c++}
flags="-std=c99 -Wall -Wextra -Werror -O1 -g -fno-builtin -fsanitize=address,undefined -fno-omit-frame-pointer"
"$cc" $flags -I"$root" -I"$root/../still-request" -c "$root/capture_session.c" -o "$out/capture_session.o"
"$cc" $flags -I"$root/../still-request" -c "$root/../still-request/still_request.c" -o "$out/still_request.o"
"$cc" $flags -I"$root" -I"$root/../still-request" "$root/tests/test_capture_session.c" "$out/capture_session.o" "$out/still_request.o" -o "$out/test_capture_session" -ldl
"$out/test_capture_session"
"$cxx" -std=c++17 -Wall -Wextra -Werror -O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer -I"$root" -I"$root/../still-request" "$root/tests/test_cpp_linkage.cpp" "$out/capture_session.o" "$out/still_request.o" -o "$out/test_cpp_linkage"
"$out/test_cpp_linkage"

# The library-host backend, against a stub of the camera's capture library
# that exports the same C names.  The stub is what makes the backend's
# plumbing testable without a camera; it proves nothing about the camera.
"$cc" $flags -I"$root" -I"$root/../still-request" -fPIC -shared \
    "$root/tests/stub_capture_library.c" -o "$out/libstub-capture.so"
# The same stub without the image object's accessors, so that the backend's
# behaviour when the library does not export them is tested too.
"$cc" $flags -I"$root" -I"$root/../still-request" -fPIC -shared \
    -DSTUB_NO_IMAGE_ACCESSORS \
    "$root/tests/stub_capture_library.c" -o "$out/libstub-capture-plain.so"
"$cc" $flags -I"$root" -I"$root/../still-request" -I"$root/../encoded-set" -c \
    "$root/capture_session_library.c" -o "$out/capture_session_library.o"
"$cc" $flags -I"$root/../encoded-set" -c \
    "$root/../encoded-set/encoded_set.c" -o "$out/encoded_set.o"
"$cc" $flags -I"$root" -I"$root/../still-request" \
    "$root/tests/test_capture_library.c" "$out/capture_session.o" \
    "$out/still_request.o" "$out/capture_session_library.o" \
    "$out/encoded_set.o" \
    -o "$out/test_capture_library" -ldl -pthread
(cd "$out" && ./test_capture_library)

"$cxx" -std=c++17 -Wall -Wextra -Werror -O1 -g \
    -fsanitize=address,undefined -fno-omit-frame-pointer \
    -I"$root" -I"$root/../still-request" \
    "$root/tests/test_cpp_library_linkage.cpp" "$out/capture_session.o" \
    "$out/still_request.o" "$out/capture_session_library.o" \
    "$out/encoded_set.o" -o "$out/test_cpp_library_linkage" -ldl -pthread
"$out/test_cpp_library_linkage"

# The demonstration program, compiled for the host with the strict flags
# and linked, so that it can be run here.
"$cc" $flags -pedantic -I"$root" -I"$root/../still-request" \
    -I"$root/../encoded-set" \
    "$root/capture-shot.c" "$out/capture_session.o" \
    "$out/still_request.o" "$out/capture_session_library.o" \
    "$out/encoded_set.o" \
    -o "$out/capture-shot-host" -ldl -pthread

# And compiled again against the camera's own declarations instead of the
# host's headers, so that the program cannot quietly come to depend on a
# header the camera does not have.  The camera's branch of capture_libc.h
# declares the three calls the program makes for its own log.
#
# **This is a declaration-only check.**  It says that the program's own
# declarations are enough to compile it; it is **not** a platform ABI
# assertion, because this is still a host compiler and a host libc.  The
# target's ABI is checked by the parent's ARM build against the pinned
# dump, which is outside this packet.
"$cc" $flags -pedantic -U__APPLE__ -I"$root" -I"$root/../still-request" \
    -I"$root/../encoded-set" \
    -c "$root/capture-shot.c" -o "$out/capture-shot-declaration-only.o"

# The program run against a library that is not there, in ordinary still
# mode.  It has to refuse **before** it tries to open the library: the
# unsupported capability is a property of the backend, not of a library, so
# the refusal must come first and the library must never be opened.
#
# The status is the program's own unsupported status, and the output must
# not carry the "cannot host the capture library" line - that line is what
# a dlopen attempt would have produced.
set +e
"$out/capture-shot-host" --library "$out/no-such-library.so" \
    --log "$out/capture-shot-refusal.log" >"$out/capture-shot-refusal.out" 2>&1
refusal_status=$?
set -e
if [ "$refusal_status" -ne 3 ]; then
    echo "capture-shot: expected the unsupported status 3, got $refusal_status" >&2
    cat "$out/capture-shot-refusal.out" >&2
    exit 1
fi
if ! grep -q "no verified way to ask the camera to keep the sensor samples" \
        "$out/capture-shot-refusal.out"; then
    echo "capture-shot: the refusal did not say what the backend cannot do" >&2
    cat "$out/capture-shot-refusal.out" >&2
    exit 1
fi
if grep -q "cannot host the capture library" "$out/capture-shot-refusal.out"; then
    echo "capture-shot: the library was opened before the refusal" >&2
    cat "$out/capture-shot-refusal.out" >&2
    exit 1
fi

# The diagnostic modes, run against the **stub** only.  The stub is a
# shared object on the host: nothing here touches a camera, and the stub
# is not the camera's library.
set +e
(cd "$out" && ./capture-shot-host --load-only --library ./libstub-capture.so \
    --log ./capture-shot-load-only.log) >"$out/capture-shot-load-only.out" 2>&1
load_only_status=$?
(cd "$out" && ./capture-shot-host --probe --library ./libstub-capture.so \
    --log ./capture-shot-probe.log) >"$out/capture-shot-probe.out" 2>&1
probe_status=$?
set -e
if [ "$load_only_status" -ne 0 ]; then
    echo "capture-shot: --load-only failed against the stub" >&2
    cat "$out/capture-shot-load-only.out" >&2
    exit 1
fi
if [ "$probe_status" -ne 0 ]; then
    echo "capture-shot: --probe failed against the stub" >&2
    cat "$out/capture-shot-probe.out" >&2
    exit 1
fi
