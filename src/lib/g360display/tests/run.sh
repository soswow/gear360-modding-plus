#!/bin/sh
# Pure rendering and stubbed device calls only; no camera access.
set -eu
here=$(CDPATH= cd -- "$(dirname "$0")/.." && pwd)
re=$(CDPATH= cd -- "$here/.." && pwd)
work=$(mktemp -d "${TMPDIR:-/tmp}/g360display-tests.XXXXXX")
trap 'rm -rf "$work"' EXIT HUP INT TERM
"${CC:-cc}" -std=c99 -Wall -Wextra -Werror -pedantic -O1 -g \
    -fsanitize=address,undefined -fno-sanitize-recover=all \
    -DG360DISPLAY_TEST -I"$here" -I"$re/capture-session" \
    "$here/g360display.c" "$here/tests/test_display.c" -o "$work/test-display"
"$work/test-display"
