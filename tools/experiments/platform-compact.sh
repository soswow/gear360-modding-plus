#!/bin/sh
# Bounded read-only telemetry using shell builtins to avoid a fork per node.
set -eu
count=${1:-10}
root=${G360_SNAPSHOT_ROOT:-}
trap 'exit 0' TERM INT
case "$count" in ''|*[!0-9]*) exit 2;; esac
[ "$count" -ge 1 ] && [ "$count" -le 1800 ] || exit 2
readval() {
    value=UNKNOWN
    if [ -r "$root$1" ]; then IFS= read -r value <"$root$1" || value=UNKNOWN; fi
}
echo 'uptime_s,capacity_raw,status,health,temp_raw,voltage_raw,ac_online,usb_online,mem_free_kB,cached_kB'
i=0
while [ "$i" -lt "$count" ]; do
    up=UNKNOWN
    if [ -r "$root/proc/uptime" ]; then read -r up idle <"$root/proc/uptime" || up=UNKNOWN; fi
    readval /sys/class/power_supply/battery/capacity; capacity=$value
    readval /sys/class/power_supply/battery/status; status=$value
    readval /sys/class/power_supply/battery/health; health=$value
    readval /sys/class/power_supply/battery/temp; temp=$value
    readval /sys/class/power_supply/battery/voltage_now; voltage=$value
    readval /sys/class/power_supply/ac/online; ac=$value
    readval /sys/class/power_supply/usb/online; usb=$value
    free=UNKNOWN; cached=UNKNOWN
    if [ -r "$root/proc/meminfo" ]; then
    while read -r key amount unit; do
        case "$key" in MemFree:) free=$amount;; Cached:) cached=$amount;; esac
    done <"$root/proc/meminfo"
    fi
    printf '%s,%s,%s,%s,%s,%s,%s,%s,%s,%s\n' "$up" "$capacity" "$status" "$health" "$temp" "$voltage" "$ac" "$usb" "$free" "$cached"
    i=$((i+1))
    [ "$i" -ge "$count" ] || sleep 1
done
