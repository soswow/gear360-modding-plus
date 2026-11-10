#!/bin/sh
# Double-Power entry target. The existing passive gear360-entry listener calls
# this through a small SD-card trampoline; boot/network hooks stay unchanged.
# No arguments/environment overrides: deployment pins this executable's hash.
set -eu
[ "$#" -eq 0 ] || { echo 'usage: start.sh' >&2; exit 2; }
PATH=/usr/sbin:/usr/bin:/sbin:/bin
export PATH
here=$(CDPATH= cd -- "$(dirname "$0")" && pwd)
root=/opt/storage/sdcard/G360
binary=$here/g360-app
# Sweep frames wait on the built-in flash's data partition before they move
# to the card (src/app/README.md, "Holding Capture: the HDR sweep").
flash=/opt/usr
queue=$flash/g360-app/queue
reserve=134217728

die() { echo "REFUSED: $*" >&2; exit 1; }
boot() { awk '$1 == "btime" {print $2}' /proc/stat; }
state() { systemctl show -p ActiveState -p MainPID di-camera-app.service; }
hash_is() {
    actual=$(/usr/bin/openssl dgst -sha256 "$1") || die "hash unreadable: $1"
    [ "${actual##* }" = "$2" ] || die "hash mismatch: $1"
}
no_clients() {
    for p in /proc/[0-9]*/comm; do
        [ -e "$p" ] || continue
        comm=$(cat "$p" 2>/dev/null || true)
        case "$comm" in g360-app|g360-shot|capture-shot|capture-idle|capture-session*|gear360-screen|gear360-control*)
            die "capture/UI client already present: $p $comm";;
        esac
    done
}
outside_stock_group() {
    groups=$(cat /proc/self/cgroup) || die 'cannot read own cgroup'
    printf '%s\n' "$groups" | grep -Eq '^[0-9]+:name=systemd:/' || die 'systemd cgroup is not identified'
    case "$groups" in *di-camera-app.service*) return 1;; *) return 0;; esac
}
escape_stock_group() {
    outside_stock_group && return 0
    group=/sys/fs/cgroup/systemd/system.slice/gear360-mod
    mkdir -p "$group" || die 'cannot create mod cgroup'
    [ -w "$group/tasks" ] || die 'mod cgroup tasks is not writable'
    printf '%s\n' "$$" >"$group/tasks" || die 'cannot leave stock cgroup'
    outside_stock_group || die 'still inside stock cgroup'
}
stock_absent() {
    s=$(state) || die 'stock service state unreadable'
    echo "$s" | grep -qx 'ActiveState=inactive' || die 'stock service is not inactive'
    echo "$s" | grep -qx 'MainPID=0' || die 'stock MainPID is nonzero'
    for p in /proc/[0-9]*/comm; do
        [ -e "$p" ] || continue
        [ "$(cat "$p" 2>/dev/null || true)" != di-camera-app ] || die 'stock process remains'
    done
}

[ "$(uname -m)" = armv7l ] || die 'unexpected architecture'
expected_boot=$(boot)
case "$expected_boot" in ''|*[!0-9]*) die 'boot time unreadable';; esac
expected_hash=$(cat "$here/g360-app.sha256") || die 'executable hash file unreadable'
case "$expected_hash" in ''|*[!0-9a-f]*) die 'bad executable hash';; esac
[ "${#expected_hash}" -eq 64 ] || die 'bad executable hash length'
[ -x "$binary" ] || die 'g360-app is not executable'
hash_is "$binary" "$expected_hash"
hash_is /usr/lib/libcapture-fw-prod.so fba8947358ff1fd666fca794d2af77e77a61964e5dd022c04c499ae7b4cc8cc7
hash_is /usr/lib/libmulticore-bridge.so 9129761c6ee9f6b62968d3a8307a8b5fbf02e1493323658c42d9ebe587673a20
free=$(df -B1 /opt/storage/sdcard | awk 'NR == 2 {print $4}')
case "$free" in ''|*[!0-9]*) die 'card free space unreadable';; esac
[ "$free" -ge "$reserve" ] || die 'insufficient card free space'
# gear360-entry's active() checks the first lock. All three descriptors stay
# open across exec, preventing another entry, screen controller or trial.
exec 9>/tmp/gear360-controls.lock
flock -n 9 || die 'another controller owns the camera'
exec 8>/tmp/gear360-screen.lock
flock -n 8 || die 'another controller owns the screen'
exec 7>/tmp/gear360-experiments.lock
flock -n 7 || die 'another experiment owns the camera'
no_clients
s=$(state) || die 'stock service state unreadable'
if echo "$s" | grep -qx 'ActiveState=active'; then
    echo "$s" | grep -Eq '^MainPID=[1-9][0-9]*$' || die 'active stock service has no PID'
    mode=fresh
elif echo "$s" | grep -qx 'ActiveState=inactive'; then
    echo "$s" | grep -qx 'MainPID=0' || die 'inactive stock service has a PID'
    mode=reenter
else
    die 'stock service is neither active nor inactive'
fi
mkdir -p "$root/state" || die 'cannot create app state directory'
: >>"$root/state/app.log" || die 'cannot write launcher log'
exec >>"$root/state/app.log" 2>&1
printf 'launch mode=%s btime=%s executable=%s\n' "$mode" "$expected_boot" "$expected_hash"
# A listener started by stock can inherit its service cgroup. Move this shell
# first; its descendants then survive the graceful stock stop too.
escape_stock_group
[ "$(boot)" = "$expected_boot" ] || die 'boot time changed before stop'
if [ "$mode" = fresh ]; then
    # Best effort while stock is still alive: it may redraw over our text.
    "$binary" --loading || echo 'early loading display failed'
    systemctl --no-block stop di-camera-app.service || die 'stock stop failed'
    i=0
    while [ "$i" -lt 30 ]; do
        s=$(state) || die 'stock service state unreadable'
        if echo "$s" | grep -qx 'ActiveState=inactive' && echo "$s" | grep -qx 'MainPID=0'; then
            break
        fi
        sleep 1
        i=$((i + 1))
    done
fi
[ "$(boot)" = "$expected_boot" ] || die 'boot time changed during stop'
stock_absent
no_clients
outside_stock_group || die 'stock cgroup reappeared'
# Redraw after stock exits, before queue checks and full app initialization. A failed indication
# must not prevent the app from starting and reporting its own display error.
"$binary" --loading || echo 'loading display failed'
# Wi-Fi is the app's own setting: g360-app starts
# src/wifi/wifi-join.sh "apply" in the background.
# Without the flash queue the app still starts; a Capture hold is then an
# ordinary still.
set --
if mkdir -p "$flash/g360-app"; then
    flash_free=$(df -B1 "$flash" | awk 'NR == 2 {print $4}') || flash_free=
    case "$flash_free" in
        ''|*[!0-9]*) echo 'flash free space unreadable: sweeps off';;
        *) set -- --queue "$queue" --queue-free-bytes "$flash_free";;
    esac
else
    echo "cannot create $flash/g360-app: sweeps off"
fi
# Optional, user-written IANA timezone. The clock is never changed here.
# Use an absolute TZif path so libc reads the bundled data, not firmware data.
zonefile=/opt/storage/sdcard/timezone
zonedir=$here/zoneinfo-2026f
if [ -f "$zonefile" ]; then
    zone=$(cat "$zonefile") || zone=
    case "$zone" in
        ''|/*|*..*|*//*|*/|*[!A-Za-z0-9_+./-]*)
            echo 'timezone: invalid name; keeping camera timezone';;
        *) if [ -f "$zonedir/$zone" ]; then
               TZ=":$zonedir/$zone"
               export TZ
               echo "timezone: $zone (IANA 2026f)"
           else
               echo 'timezone: unknown name or database missing; keeping camera timezone'
           fi;;
    esac
fi
# Leave a conservative 5 MiB allowance for logs/state outside the shot budget.
exec "$binary" --root "$root" --free-bytes "$((free - 5242880))" --log "$root/state/capture.log" "$@"
