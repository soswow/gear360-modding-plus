#!/bin/sh
set -eu
root=$(CDPATH= cd -- "$(dirname "$0")/.." && pwd)
cc=${CC:-/usr/bin/clang}
sanitize_flags=
if [ "${SANITIZE:-0}" = 1 ]; then
  sanitize_flags="-fsanitize=address,undefined -fno-omit-frame-pointer"
fi
test_tmp=${OUT_DIR:-${TMPDIR:-/tmp}}
if [ -n "${OUT_DIR:-}" ]; then mkdir -p "$OUT_DIR"; fi
out=$(mktemp "$test_tmp/ram-reader-enc-ring.XXXXXX")
guard_log="$out.guard"
ipcc_out=$(mktemp "$test_tmp/ram-reader-ipcc.XXXXXX")
peer_out=$(mktemp "$test_tmp/ram-reader-peer.XXXXXX")
export_out=$(mktemp "$test_tmp/ram-reader-export.XXXXXX")
svc6_out=$(mktemp "$test_tmp/ram-reader-svc6.XXXXXX")
vptr_out=$(mktemp "$test_tmp/ram-reader-vptr.XXXXXX")
profile8_out=$(mktemp "$test_tmp/ram-reader-profile8.XXXXXX")
trap 'rm -f "$out" "$guard_log" "$ipcc_out" "$peer_out" "$export_out" "$svc6_out" "$vptr_out" "$profile8_out"' EXIT

# shellcheck disable=SC2086
"$cc" -std=c11 $sanitize_flags -DRAM_HELPER_HOST_TEST -Wall -Wextra -Werror \
  "$root/tests/test_enc_ring_state.c" -o "$out"
"$out"

# shellcheck disable=SC2086
"$cc" -std=c11 $sanitize_flags -DRAM_HELPER_HOST_TEST -Wall -Wextra -Werror \
  "$root/tests/test_ipcc_map_state.c" -o "$ipcc_out"
"$ipcc_out"

# shellcheck disable=SC2086
"$cc" -std=c11 $sanitize_flags -DRAM_HELPER_HOST_TEST -Wall -Wextra -Werror \
  "$root/tests/test_peer_state.c" -o "$peer_out"
"$peer_out"

# shellcheck disable=SC2086
"$cc" -std=c11 $sanitize_flags -DRAM_HELPER_HOST_TEST -Wall -Wextra -Werror \
  "$root/tests/test_enc_export_state.c" -o "$export_out"
"$export_out"

# shellcheck disable=SC2086
"$cc" -std=c11 $sanitize_flags -DRAM_HELPER_HOST_TEST -Wall -Wextra -Werror \
  "$root/tests/test_svc6_dispatch_state.c" -o "$svc6_out"
"$svc6_out"

# shellcheck disable=SC2086
"$cc" -std=c11 $sanitize_flags -DRAM_HELPER_HOST_TEST -Wall -Wextra -Werror \
  "$root/tests/test_manager_vptr_state.c" -o "$vptr_out"
"$vptr_out"

# shellcheck disable=SC2086
"$cc" -std=c11 $sanitize_flags -DRAM_HELPER_HOST_TEST -Wall -Wextra -Werror \
  "$root/tests/test_profile8_fields.c" -o "$profile8_out"
"$profile8_out"

if "$cc" -target armv7a-linux-gnueabi -ffreestanding \
    -DRAM_HELPER_HOST_TEST -fsyntax-only "$root/ram_helper.c" \
    >"$guard_log" 2>&1; then
  echo "FAIL: target build accepted the host syscall seam" >&2
  exit 1
fi
if ! grep -q 'RAM_HELPER_HOST_TEST cannot build as a Linux ARM syscall shim' "$guard_log"; then
  cat "$guard_log" >&2
  exit 1
fi
echo "Linux ARM host-seam rejection: PASS"
