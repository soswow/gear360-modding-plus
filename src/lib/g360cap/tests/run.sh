#!/bin/sh
# Host tests for g360cap: the pure checks, then the library and g360-shot
# against the synthetic capture library.
set -eu
here=$(CDPATH= cd -- "$(dirname "$0")/.." && pwd)
out=${TMPDIR:-/tmp}/g360cap-check-test
cc=${CC:-cc}
"$cc" -std=c99 -Wall -Wextra -Werror -pedantic -fsanitize=address,undefined \
  -I"$here" "$here/g360cap_check.c" "$here/tests/test_g360cap_check.c" -o "$out"
"$out"
rm -f "$out"
re=$(CDPATH= cd -- "$here/.." && pwd)
for src in "$here/g360cap.c" "$here/g360cap_check.c" "$here/g360_dng.c" "$here/g360_exif.c" "$here/g360_output.c" "$re/encoded-set/encoded_set.c"; do
  "$cc" -std=c99 -Wall -Wextra -Werror -I"$here" -I"$re/capture-session" -I"$re/encoded-set" \
    -c "$src" -o "$out-$(basename "$src" .c).o"
done
"${CXX:-c++}" -std=c++11 -Wall -Wextra -Werror -pedantic -I"$here" "$here/tests/test_cpp_linkage.cpp" \
  "$out-g360cap.o" "$out-g360cap_check.o" "$out-encoded_set.o" "$out-g360_dng.o" "$out-g360_exif.o" "$out-g360_output.o" -o "$out-cpp"
"$out-cpp"
rm -f "$out-cpp" "$out-g360cap.o" "$out-g360cap_check.o" "$out-encoded_set.o" "$out-g360_dng.o" "$out-g360_exif.o" "$out-g360_output.o"
CC="$cc" python3 "$here/tests/test_g360cap.py"

CC="$cc" python3 "$here/tests/test_dng.py"
CC="$cc" python3 "$here/tests/test_exif.py"
CC="$cc" python3 "$here/tests/test_output.py"
CC="$cc" python3 "$here/tests/test_pose.py"
