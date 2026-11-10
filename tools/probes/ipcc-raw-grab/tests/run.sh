#!/bin/sh
# Host checks for the raw grabber.  Builds and runs only on the host; it
# never touches the camera and never opens a device.
#
#   test_still_frame_bytes.c   the bytes the helper puts on the wire
#   test_argument_handling.c   the real c_entry, with the syscall layer
#                              replaced by the harness, so the argument
#                              handling and the early exits are exercised
#   test_ring_poll.c           the real ring watcher, over a synthetic
#                              shared window, so that a record that wraps
#                              the end of a ring is walked as it is
#   test_run_command.py        the runner's options and its command builder
set -eu

root=$(CDPATH= cd -- "$(dirname "$0")/.." && pwd)
# The build products stay inside this packet.  A shared temporary
# directory is not used, so nothing is written outside the packet and
# nothing outside it is removed.
packet=$(CDPATH= cd -- "$root/../../.." && pwd)
mkdir -p "$packet/tmp"
work=$(mktemp -d "$packet/tmp/ipcc-raw-grab.XXXXXX")
trap 'rm -rf "$work"' EXIT

# A compiler that can find the host's own headers.  CC overrides the search.
cc=${CC:-}
if [ -z "$cc" ]; then
    for candidate in /usr/bin/cc /opt/homebrew/opt/llvm/bin/clang cc clang; do
        if command -v "$candidate" >/dev/null 2>&1; then
            cc=$candidate
            break
        fi
    done
fi
if [ -z "$cc" ]; then
    echo "no host compiler found" >&2
    exit 1
fi

echo "== still frame bytes =="
"$cc" -std=c11 -Wall -Wextra -fno-builtin \
    -Werror -I"$root/../../../src/lib/still-request" -I"$root/../../../src/experimental/mcb-frame" \
    "$root/tests/test_still_frame_bytes.c" "$root/../../../src/lib/still-request/still_request.c" \
    "$root/../../../src/experimental/mcb-frame/mcb_frame.c" -o "$work/still_frame_bytes"
"$work/still_frame_bytes"

echo "== argument handling =="
"$cc" -std=c11 -Wall -Wextra -fno-builtin -DIPCC_RAW_GRAB_HOST_TEST \
    -Werror -I"$root/../../../src/lib/still-request" -I"$root/../../../src/experimental/mcb-frame" \
    "$root/tests/test_argument_handling.c" "$root/ipcc_raw_grab.c" \
    "$root/../../../src/lib/still-request/still_request.c" "$root/../../../src/experimental/mcb-frame/mcb_frame.c" \
    -o "$work/argument_handling"
"$work/argument_handling"

echo "== runner options =="
python3 "$root/tests/test_run_command.py"

echo "== ring poll =="
"$cc" -std=c11 -Wall -Wextra -fno-builtin -DIPCC_RAW_GRAB_HOST_TEST \
    -Werror -I"$root/../../../src/lib/still-request" -I"$root/../../../src/experimental/mcb-frame" \
    "$root/tests/test_ring_poll.c" "$root/ipcc_raw_grab.c" \
    "$root/../../../src/lib/still-request/still_request.c" "$root/../../../src/experimental/mcb-frame/mcb_frame.c" \
    -o "$work/ring_poll"
"$work/ring_poll"

echo "host checks: PASS"
