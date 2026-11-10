#!/bin/sh
# Read-only observation: full inventory up to 600 samples; --compact up to 1800.
# Use raw values: absent properties are unknown, never 0% or "not charging".
set -eu
if [ "${1:-}" = --compact ]; then
    [ "$#" -le 2 ] || exit 2
    here=$(CDPATH= cd -- "$(dirname "$0")" && pwd)
    exec sh "$here/platform-compact.sh" "${2:-10}"
fi
count=${1:-10}
case "$count" in ''|*[!0-9]*) exit 2;; esac
[ "$count" -ge 1 ] && [ "$count" -le 600 ] || exit 2
root=${G360_SNAPSHOT_ROOT:-}
read_file() {
    if [ -r "$root$1" ]; then
        printf 'node=%s value=' "$1"
        cat "$root$1" || printf 'UNKNOWN(read failed)\n'
    else
        printf 'node=%s value=UNKNOWN(unavailable)\n' "$1"
    fi
}
echo 'schema=gear360-platform-v1 raw-units=as-reported'
read_file /proc/bus/input/devices
read_file /proc/asound/cards
for led in "$root"/sys/class/leds/*; do
    [ -d "$led" ] || continue
    for property in brightness max_brightness trigger; do
        read_file "${led#"$root"}/$property"
    done
done
for thermal in "$root"/sys/class/thermal/thermal_zone*; do
    [ -d "$thermal" ] || continue
    read_file "${thermal#"$root"}/type"
    read_file "${thermal#"$root"}/temp"
done
i=0
while [ "$i" -lt "$count" ]; do
    echo "sample=$i"
    read_file /proc/uptime
    read_file /proc/stat
    read_file /proc/meminfo
    supplies=0
    for supply in "$root"/sys/class/power_supply/*; do
        [ -d "$supply" ] || continue
        supplies=$((supplies + 1))
        for property in type present status capacity capacity_level online voltage_now current_now charge_now charge_full energy_now energy_full temp health; do
            read_file "${supply#"$root"}/$property"
        done
    done
    [ "$supplies" -ne 0 ] || echo 'power_supply=UNKNOWN(no providers)'
    i=$((i + 1))
    [ "$i" -ge "$count" ] || sleep 1
done
