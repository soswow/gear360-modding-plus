#!/bin/sh
# On-camera experiment wrapper. Preparation never invokes this script.
# MODE is fresh (stop stock once) or reenter (stock must already be absent).
# Caller records/reviews the expected btime and pins the staged executable.
set -eu
if [ "$#" -lt 5 ]; then
    echo 'usage: run-session.sh fresh|reenter BTIME RUN_DIR PROBE_SHA256 [g360-shot options]' >&2
    exit 2
fi
mode=$1; expected_boot=$2; run_dir=$3; probe_hash=$4; shift 4
case "$mode" in fresh|reenter) ;; *) exit 2;; esac
case "$expected_boot" in ''|*[!0-9]*) exit 2;; esac
case "$probe_hash" in *[!0-9a-f]*|'') exit 2;; esac
[ "${#probe_hash}" -eq 64 ] || exit 2
case "$run_dir" in /opt/storage/sdcard/exp-*) ;; *) exit 2;; esac
name=${run_dir#/opt/storage/sdcard/exp-}
case "$name" in ''|*[!A-Za-z0-9_-]*) exit 2;; esac
here=$(CDPATH= cd -- "$(dirname "$0")" && pwd)
probe=$here/g360-shot
display=$here/gear360-display
screen_text=${DISPLAY_TEXT:-TEST MODE}
die() { echo "REFUSED: $*" >&2; exit 1; }
boot() { awk '$1 == "btime" {print $2}' /proc/stat; }
same_boot() { [ "$(boot)" = "$expected_boot" ] || die 'boot time changed'; }
hash_is() {
    actual=$(/usr/bin/openssl dgst -sha256 "$1") || die "hash unreadable: $1"
    [ "${actual##* }" = "$2" ] || die "hash mismatch: $1"
}
no_clients() {
    for p in /proc/[0-9]*/comm; do
        comm=$(cat "$p" 2>/dev/null || true)
        case "$comm" in g360-shot|capture-shot|capture-idle|capture-session*|gear360-screen|gear360-control*)
            die "capture/UI client already present: $p $comm";;
        esac
    done
}
state() { systemctl show -p ActiveState -p MainPID di-camera-app.service; }
stock_absent() {
    s=$(state)
    echo "$s" | grep -qx 'ActiveState=inactive' || die 'stock service is not inactive'
    echo "$s" | grep -qx 'MainPID=0' || die 'stock MainPID is nonzero'
    for p in /proc/[0-9]*/comm; do
        [ "$(cat "$p" 2>/dev/null || true)" != di-camera-app ] || die 'stock process remains'
    done
}
check_hdr() {
    [ -n "${REQUIRE_WB:-}" ] || return 0
    case "$REQUIRE_WB" in WB_*) ;; *) die 'bad white balance name';; esac
    case "$REQUIRE_WB" in *[!A-Z0-9_]*) die 'bad white balance name';; esac
    users=$(st cap capdtm usrlist) || die 'settings unreadable'
    # The firmware's console includes embedded NUL separators. Normalize
    # them before matching fields rather than letting grep treat it as binary.
    users=$(printf '%s' "$users" | tr -d '\000')
    echo "$users" | grep 'USERDATA_WB ' | grep -q "| *$REQUIRE_WB *|" || die 'white balance mismatch'
    echo "$users" | grep 'USERDATA_SMARTRANGE ' | grep -q 'SMARTRANGE_OFF' || die 'Smart Range must be off'
    printf '%s\n' "$users"
}
# Reject argument overrides of paths/free-space: the generated plans supply
# only capture options. Every argument is passed as an argument, never eval'd.
for arg in "$@"; do
    case "$arg" in --out|--log|--free-bytes) die "reserved option: $arg";; esac
done
[ "$(uname -m)" = armv7l ] || die 'unexpected architecture'
same_boot
hash_is /usr/lib/libcapture-fw-prod.so fba8947358ff1fd666fca794d2af77e77a61964e5dd022c04c499ae7b4cc8cc7
hash_is /usr/lib/libmulticore-bridge.so 9129761c6ee9f6b62968d3a8307a8b5fbf02e1493323658c42d9ebe587673a20
hash_is "$probe" "$probe_hash"
[ -x "$probe" ] || die 'staged probe is not executable; set its executable bit before the trial'
case "${DISPLAY_SHA256:-}" in ''|*[!0-9a-f]*) die 'pinned display hash required';; esac
[ "${#DISPLAY_SHA256}" -eq 64 ] || die 'bad display hash length'
hash_is "$display" "$DISPLAY_SHA256"
[ -x "$display" ] || die 'staged display helper is not executable'
# Render to stdout without --write first: reject bad text before stopping stock.
"$display" --center "$screen_text" >/dev/null || die 'invalid screen message'
exec 9>/tmp/gear360-experiments.lock
flock -n 9 || die 'another experiment owns the lock'
exec 8>/tmp/gear360-screen.lock
flock -n 8 || die 'another controller owns the screen'
no_clients
check_hdr
[ ! -e "$run_dir" ] || die 'run directory already exists'
# SHOTS_DIR may put the shots on the internal flash's data partition
# (/opt/usr, speed tests). The session records stay in RUN_DIR on the card.
shots=$run_dir/shots
space=/opt/storage/sdcard
if [ -n "${SHOTS_DIR:-}" ]; then
    case "$SHOTS_DIR" in /opt/usr/g360-exp-*) ;; *) die 'SHOTS_DIR must be /opt/usr/g360-exp-NAME';; esac
    case "${SHOTS_DIR#/opt/usr/g360-exp-}" in ''|*[!A-Za-z0-9_-]*) die 'bad SHOTS_DIR name';; esac
    [ ! -e "$SHOTS_DIR" ] || die 'shots directory already exists'
    shots=$SHOTS_DIR
    space=/opt/usr
fi
free=$(df -B1 "$space" | awk 'NR > 1 {print $4}')
case "$free" in ''|*[!0-9]*) die 'free space unreadable';; esac
reserve=${RESERVE_BYTES:-134217728}
case "$reserve" in ''|*[!0-9]*) die 'bad space reserve';; esac
[ "$free" -gt "$reserve" ] || die 'insufficient free space'
mkdir "$run_dir"
# All records remain on the card if capture fails. No automatic recovery.
exec >"$run_dir/session.txt" 2>&1
echo "mode=$mode btime=$expected_boot probe_sha256=$probe_hash"
cat /proc/uptime
state
st cap capdtm usrlist >"$run_dir/settings-before.txt"
if [ "$mode" = fresh ]; then
    state | grep -qx 'ActiveState=active' || die 'fresh mode needs active stock app'
    systemctl --no-block stop di-camera-app.service || die 'stock stop failed'
    i=0
    while [ "$i" -lt 30 ]; do
        s=$(state)
        echo "$s" | grep -qx 'ActiveState=inactive' && break
        sleep 1; i=$((i + 1))
    done
fi
same_boot
stock_absent
no_clients
check_hdr
echo "screen_message=$screen_text"
# This writes a complete blank-background bitmap with centered text in one
# draw/update. Only after stock is confirmed absent; no app-memory access.
"$display" --write --center "$screen_text" || die 'screen takeover failed; no capture sent'
mkdir "$shots"
echo "shots=$shots"
echo "capture_start_uptime=$(cat /proc/uptime)"
rc=0
"$probe" --out "$shots" --log "$run_dir/probe.log" --free-bytes "$((free - 5242880))" "$@" || rc=$?
echo "probe_exit=$rc"
same_boot
stock_absent
no_clients
echo "capture_end_uptime=$(cat /proc/uptime)"
st cap capdtm usrlist >"$run_dir/settings-after.txt" || rc=1
sync
echo "session_exit=$rc btime=$(boot)"
exit "$rc"
