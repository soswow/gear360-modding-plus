#!/bin/sh
# g360wifi: join an existing Wi-Fi network (station mode) for one boot, with
# the camera's own access point as the way back. README.md beside this file
# explains the design and the trial.
#
#   wifi-join.sh check      read-only preflight; changes nothing
#   wifi-join.sh scan       look for the network while the access point runs
#   wifi-join.sh status     read-only summary of the current state
#   wifi-join.sh ap-cycle   access point off, then on again (tests the way back)
#   wifi-join.sh join       join the network named in <card>/wifi
#   wifi-join.sh host-ok    the host has found the camera on that network
#   wifi-join.sh revert     leave the network and bring the access point back
#   wifi-join.sh apply off|ap|home
#                           g360-app's WIFI setting, started by the app in the
#                           background: Wi-Fi off; the camera's own access
#                           point; or the network if it is in range, else the
#                           access point (see README.md)
#
# ap-cycle, join and revert detach (the web CGI's connection drops with the
# access point), pause g360-app, log to wifi.log, and
# start g360-app again at the end. A watchdog brings the access point back
# if a run dies part way.
#
# Nothing here writes to the built-in flash. The network settings live in
# RAM under /tmp and the station firmware is loaded into the Wi-Fi chip's
# RAM, so a reboot always returns to the stock access point. (The access
# point agent rewrites its own config on the flash each time the access
# point starts, as it does at every boot.)
#
# The network file contains name/password pairs. Blank and # lines are ignored.
# A single name without a password retains the legacy open-network behavior.
# Neither names nor passwords go into wifi.log or the output.
#
# The G360_WIFI_* variables exist for the host-side tests only.

set -u
LC_ALL=C
export LC_ALL
PATH=${G360_WIFI_PATH:-/usr/sbin:/usr/bin:/sbin:/bin}
export PATH

HERE=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
SELF=$HERE/$(basename -- "$0")
CARD=${G360_WIFI_CARD:-/opt/storage/sdcard}
RUN=${G360_WIFI_RUN:-/tmp/g360-wifi}
PROC=${G360_WIFI_PROC:-/proc}
DHD=${G360_WIFI_DHD:-/sys/module/dhd/parameters}
LOCKS=${G360_WIFI_LOCKS:-/tmp}
AGENT_CONF=${G360_WIFI_AGENT_CONF:-/opt/var/lib/mobileap-agent/hostapd.conf}
APP_START=${G360_WIFI_APP_START:-$CARD/mods/g360-app-v1-20261105/start.sh}
LOG=${G360_WIFI_LOG:-$HERE/wifi.log}
AP_CACHE=${G360_WIFI_AP_CACHE:-$HERE/ap-settings}
# What wlan.sh writes for station mode; the driver loads "<path>_blob".
FW_NET=${G360_WIFI_FW:-/lib/firmware/wlan_net_bcm43455.bin}
NV_NET=${G360_WIFI_NVRAM:-/opt/etc/nvram/nvram_net_bcm43455.txt}
# Seconds.
DETACH_DELAY=${G360_WIFI_DETACH_DELAY:-3}
APP_STOP_WAIT=${G360_WIFI_APP_STOP_WAIT:-40}
JOIN_WAIT=${G360_WIFI_JOIN_WAIT:-45}
DHCP_WAIT=${G360_WIFI_DHCP_WAIT:-30}
HOST_WAIT=${G360_WIFI_HOST_WAIT:-300}
AP_WAIT=${G360_WIFI_AP_WAIT:-25}
HOLD=${G360_WIFI_HOLD:-20}
FAIL_HOLD=${G360_WIFI_FAIL_HOLD:-60}
WATCH_LIMIT=${G360_WIFI_WATCH_LIMIT:-900}
WATCH_POLL=${G360_WIFI_WATCH_POLL:-5}
SCAN_WAIT=${G360_WIFI_SCAN_WAIT:-4}
APP_JOIN_WAIT=${G360_WIFI_APP_JOIN_WAIT:-30}
RETRY_AFTER=${G360_WIFI_RETRY_AFTER:-12}
APPLY_WAIT=${G360_WIFI_APPLY_WAIT:-180}
AP_SETTLE=${G360_WIFI_AP_SETTLE:-15}
KEEP_POLL=${G360_WIFI_KEEP_POLL:-10}
KEEP_LOST=${G360_WIFI_KEEP_LOST:-120}

NET_FILE=$CARD/wifi
AP_PASS_FILE=$CARD/mods/ap-passphrase
IFACE=wlan0
AP_ADDR=192.168.43.1
CTRL=$RUN/ctrl
CR=$(printf '\r')
WATCHED=0
WANT=
REASON=
SSID= PASS= KEY= AP_SSID= AP_PASS= AP_CHANNEL= AP_SOURCE= IP=

log() { printf '%s [%s] %s\n' "$(date '+%F %T')" "$$" "$*" >>"$LOG"; }
# A milestone: on the card before the next step, in case the camera dies.
mark() { log "$@"; sync; }
refuse() { echo "REFUSED: $*" >&2; log "REFUSED: $*"; exit 1; }

# Live processes whose name is exactly $1.
pids_named() {
    for c in "$PROC"/[0-9]*/comm; do
        [ -r "$c" ] || continue
        [ "$(cat "$c" 2>/dev/null)" = "$1" ] || continue
        p=${c%/comm}
        p=${p##*/}
        kill -0 "$p" 2>/dev/null && echo "$p"
    done
}
running() { [ -n "$(pids_named "$1")" ]; }

# wait_until SECONDS COMMAND...: succeed as soon as COMMAND does.
wait_until() {
    w=$1
    shift
    while :; do
        "$@" && return 0
        [ "$w" -gt 0 ] || return 1
        sleep 1
        w=$((w - 1))
    done
}

# Never log a line that carries a password.
scrub() {
    for s in "$AP_PASS" "$PASS"; do
        [ -n "$s" ] || continue
        case $1 in *"$s"*) echo '[withheld: carried a password]'; return;; esac
    done
    echo "$1"
}

# Read one pair by index without storing credentials outside the protected card.
# A lone name is the legacy open-network format; an incomplete multi-pair
# file is rejected rather than silently pairing the wrong credentials.
read_network() {
    [ -r "$NET_FILE" ] || { REASON="no network file at $NET_FILE"; return 1; }
    count=$(awk '{ sub(/\r$/, ""); if ($0 != "" && substr($0, 1, 1) != "#") n++ } END { print n+0 }' "$NET_FILE")
    [ "$count" -gt 0 ] || { REASON='the network file has no networks'; return 1; }
    if [ "$count" -gt 1 ] && [ $((count % 2)) -ne 0 ]; then
        REASON='the network file has an incomplete pair'; return 1
    fi
    NETWORK_COUNT=$(((count + 1) / 2))
    index=${1:-1}
    [ "$index" -le "$NETWORK_COUNT" ] || return 2
    SSID=$(awk -v wanted="$((index * 2 - 1))" '{ sub(/\r$/, ""); if ($0 != "" && substr($0, 1, 1) != "#" && ++n == wanted) { print; exit } }' "$NET_FILE")
    PASS=$(awk -v wanted="$((index * 2))" '{ sub(/\r$/, ""); if ($0 != "" && substr($0, 1, 1) != "#" && ++n == wanted) { print; exit } }' "$NET_FILE")
    case $SSID$PASS in
    *[![:print:]]*) REASON='the network file has control or non-ASCII characters'; return 1;;
    esac
    if [ "${#SSID}" -lt 1 ] || [ "${#SSID}" -gt 32 ]; then
        REASON="the network name is ${#SSID} bytes (1 to 32)"; return 1
    fi
    case ${#PASS} in
    0) KEY=open ;;
    64) case $PASS in
        *[!0-9a-fA-F]*) REASON='a 64-character password must be hex'; return 1;;
        esac
        KEY=hex ;;
    *) if [ "${#PASS}" -lt 8 ] || [ "${#PASS}" -gt 63 ]; then
           REASON="the password is ${#PASS} characters (8 to 63)"; return 1
       fi
       KEY=passphrase ;;
    esac
}

validate_networks() {
    network_index=1
    while :; do
        read_network "$network_index" || return 1
        [ "$network_index" -ge "$NETWORK_COUNT" ] && break
        network_index=$((network_index + 1))
    done
    read_network 1
}

# Try configured pairs in order, starting at the first one found by the
# pre-scan when available. Later pairs have not been scanned yet.
join_candidates() {
    candidate=${2:-1}
    while :; do
        read_network "$candidate" || return 1
        log "trying network $candidate of $NETWORK_COUNT"
        if join_network "$1" && get_address; then return 0; fi
        log "network $candidate failed: $REASON"
        stop_pidfile udhcpc.pid udhcpc
        stop_pidfile wpa.pid wpa_supplicant
        rm -rf "$CTRL" "$RUN/wpa.conf" "$RUN/ip" "$RUN/lease"
        ip route del default dev "$IFACE" 2>/dev/null
        ip addr flush dev "$IFACE" 2>/dev/null
        [ "$candidate" -ge "$NETWORK_COUNT" ] && return 1
        candidate=$((candidate + 1))
    done
}

# The camera's own access point. The agent deletes its hostapd.conf while
# the access point is off, so its name and channel are also kept in
# ap-settings beside this script (they are not secret). Without either, the
# name is the device name (the stock app names the access point after it).
read_ap() {
    AP_SSID= AP_CHANNEL= AP_SOURCE=agent
    if [ -r "$AGENT_CONF" ]; then
        AP_SSID=$(sed -n 's/^ssid=//p' "$AGENT_CONF" | head -n 1)
        AP_CHANNEL=$(sed -n 's/^channel=//p' "$AGENT_CONF" | head -n 1)
        if [ -n "$AP_SSID" ] && [ -n "$AP_CHANNEL" ]; then
            saved=$(printf '%s\n%s' "$AP_SSID" "$AP_CHANNEL")
            [ "$(cat "$AP_CACHE" 2>/dev/null)" = "$saved" ] ||
                printf '%s\n' "$saved" >"$AP_CACHE"
        fi
    elif [ -r "$AP_CACHE" ]; then
        AP_SOURCE=saved
        { IFS= read -r AP_SSID || :; IFS= read -r AP_CHANNEL || :; } <"$AP_CACHE"
    else
        AP_SOURCE='device name'
        AP_SSID=$(vconftool get db/setting/device_name 2>/dev/null |
            sed -n 's/^.*, value = \(.*\) (string)$/\1/p')
        AP_CHANNEL=6
    fi
    AP_PASS=$(cat "$AP_PASS_FILE" 2>/dev/null) || AP_PASS=
    [ -n "$AP_SSID" ] || { REASON='the access point name is unknown'; return 1; }
    case $AP_SSID$AP_PASS in
    *"'"*|*\\*|*[![:print:]]*)
        REASON='the access point name or password has a quote, backslash or control character'; return 1;;
    esac
    case $AP_CHANNEL in
    ''|*[!0-9]*) REASON="the access point channel is [$AP_CHANNEL]"; return 1;;
    esac
    if [ "${#AP_PASS}" -lt 8 ] || [ "${#AP_PASS}" -gt 63 ]; then
        REASON="no usable access point password in $AP_PASS_FILE"; return 1
    fi
}

# The access point agent (mobileap-agent) on the system bus.
agent() {
    m=$1
    shift
    gdbus call --system --timeout 20 --dest org.tizen.tethering \
        --object-path /Tethering --method "org.tizen.tethering.$m" "$@" 2>&1
}
ap_up_now() {
    running hostapd && ip addr show dev "$IFACE" 2>/dev/null | grep -q "inet $AP_ADDR/"
}
ap_gone() { ! running hostapd; }
ap_off() {
    log "disable_wifi_ap: $(scrub "$(agent disable_wifi_ap)")"
    wait_until 15 ap_gone && return 0
    log "disable_wifi_tethering: $(scrub "$(agent disable_wifi_tethering)")"
    wait_until 15 ap_gone
}
# The same request as ../../hdr/boot/autostart-services.sh.
ap_on() {
    settings="{'ssid': <'$AP_SSID'>, 'key': <'$AP_PASS'>, 'sec_type': <int32 1>, 'channel': <int32 $AP_CHANNEL>, 'visibility': <true>, 'mode': <'ios'>}"
    log "enable_wifi_ap: $(scrub "$(agent enable_wifi_ap "$settings")")"
    wait_until "$AP_WAIT" ap_up_now
}

app_gone() { ! running g360-app; }
stop_app() {
    pids=$(pids_named g360-app)
    if [ -z "$pids" ]; then
        echo 0 >"$RUN/app-was-running"
        log 'g360-app was not running'
        return 0
    fi
    echo 1 >"$RUN/app-was-running"
    for p in $pids; do
        log "stopping g360-app (pid $p)"
        kill -TERM "$p" 2>/dev/null
    done
    wait_until "$APP_STOP_WAIT" app_gone || { REASON='g360-app did not stop'; return 1; }
}

# The same three locks as src/app/start.sh. Children that outlive this
# run are started with 7>&- 8>&- 9>&- so they never hold them.
take_locks() {
    exec 9>"$LOCKS/gear360-controls.lock"
    flock -n 9 || { REASON='another controller owns the camera'; return 1; }
    exec 8>"$LOCKS/gear360-screen.lock"
    flock -n 8 || { REASON='another controller owns the screen'; return 1; }
    exec 7>"$LOCKS/gear360-experiments.lock"
    flock -n 7 || { REASON='another experiment owns the camera'; return 1; }
}
drop_locks() {
    exec 7>&- 8>&- 9>&-
}

station_firmware() {
    ifconfig "$IFACE" down 2>/dev/null
    sleep 1
    if ! echo "$FW_NET" >"$DHD/firmware_path" || ! echo "$NV_NET" >"$DHD/nvram_path"; then
        REASON='NO FIRMWARE'; return 1
    fi
    ifconfig "$IFACE" up || { REASON='NO FIRMWARE'; return 1; }
    sleep 2
    log "firmware now: $(wl ver 2>&1 | tail -n 1)"
    log "radio country: $(wl country 2>&1)"
}

start_supplicant() {
    # The last attempt's debug log stays as wpa.log.prev (RAM; no password).
    [ -e "$RUN/wpa.log" ] && mv -f "$RUN/wpa.log" "$RUN/wpa.log.prev"
    rm -rf "$CTRL" "$RUN/wpa.pid"
    mask=$(umask)
    umask 077
    {
        echo "ctrl_interface=$CTRL"
        echo 'update_config=0'
        echo 'ap_scan=1'
        echo 'network={'
        printf '\tssid="%s"\n' "$SSID"
        printf '\tscan_ssid=1\n'
        case $KEY in
        open) printf '\tkey_mgmt=NONE\n' ;;
        hex) printf '\tpsk=%s\n' "$PASS" ;;
        *) printf '\tpsk="%s"\n' "$PASS" ;;
        esac
        echo '}'
    } >"$RUN/wpa.conf"
    umask "$mask"
    wpa_supplicant -B -i "$IFACE" -D nl80211 -c "$RUN/wpa.conf" \
        -P "$RUN/wpa.pid" -f "$RUN/wpa.log" -t -d \
        </dev/null >/dev/null 2>&1 7>&- 8>&- 9>&-
    rc=$?
    # It has read the file; the password stays only in its memory.
    rm -f "$RUN/wpa.conf"
    [ "$rc" -eq 0 ] || { REASON='WPA FAILED'; return 1; }
    wait_until 5 test -s "$RUN/wpa.pid" || { REASON='WPA FAILED'; return 1; }
}

wpa() { wpa_cli -p "$CTRL" -i "$IFACE" "$@" 2>/dev/null; }
wpa_state() { wpa status | sed -n 's/^wpa_state=//p'; }
link_summary() {
    wpa status | grep -E '^(wpa_state|freq|key_mgmt|pairwise_cipher)=' | tr '\n' ' '
}

# join_network WAIT: wpa_supplicant joins within WAIT seconds. A healthy join
# takes 1-2 s. If it hasn't happened after RETRY_AFTER seconds, a fresh
# wpa_supplicant gets the rest: on 6 November the first scan after the
# station firmware loaded found 6 networks, then wpa_supplicant neither
# joined nor scanned again for 30 s (as if it had missed the scan's end),
# and a second try a minute later joined in 1 s. A wrong password is not
# tried again.
join_network() {
    total=$1
    start_supplicant || return 1
    if [ "$RETRY_AFTER" -lt "$total" ]; then
        JOIN_WAIT=$RETRY_AFTER
        await_join && return 0
        case $REASON in 'NOT FOUND'|'NO JOIN') ;; *) return 1 ;; esac
        log "no join after ${RETRY_AFTER}s ($REASON); starting wpa_supplicant again"
        stop_pidfile wpa.pid wpa_supplicant
        start_supplicant || return 1
        total=$((total - RETRY_AFTER))
    fi
    JOIN_WAIT=$total
    await_join
}

await_join() {
    t=$JOIN_WAIT
    last=
    while :; do
        s=$(wpa_state)
        if [ "$s" != "$last" ]; then
            log "wpa_state ${s:-none}"
            last=$s
            case $s in
            COMPLETED) return 0 ;;
            esac
        fi
        if grep -q -E 'WRONG_KEY|4-Way Handshake failed' "$RUN/wpa.log" 2>/dev/null; then
            REASON='WRONG PASS'
            return 1
        fi
        [ "$t" -gt 0 ] || break
        sleep 1
        t=$((t - 1))
    done
    # Why: was the network seen at all?
    seen=$(wpa scan_results | S=$SSID awk -F '\t' '$5 == ENVIRON["S"] { print "freq=" $2 " signal=" $3 " flags=" $4 }')
    if [ -n "$seen" ]; then
        log "seen but not joined: $(echo "$seen" | tr '\n' ' ')"
        REASON='NO JOIN'
    else
        log 'the network is not in the scan results'
        REASON='NOT FOUND'
    fi
    return 1
}

get_address() {
    rm -f "$RUN/ip" "$RUN/lease" "$RUN/dhcp-events"
    cp "$HERE/udhcpc.sh" "$RUN/udhcpc.sh" && chmod 700 "$RUN/udhcpc.sh" ||
        { REASON='NO ADDRESS'; return 1; }
    G360_WIFI_RUN=$RUN udhcpc -i "$IFACE" -f -n -t 8 -T 3 -H gear360 \
        -s "$RUN/udhcpc.sh" </dev/null >"$RUN/udhcpc.log" 2>&1 7>&- 8>&- 9>&- &
    echo $! >"$RUN/udhcpc.pid"
    if wait_until "$DHCP_WAIT" test -s "$RUN/ip"; then
        IP=$(cat "$RUN/ip")
        return 0
    fi
    REASON='NO ADDRESS'
    return 1
}

await_host() {
    t=$HOST_WAIT
    while [ "$t" -gt 0 ]; do
        [ -e "$RUN/host-ok" ] && return 0
        if [ $((t % 5)) -eq 0 ] && [ "$(wpa_state)" != COMPLETED ]; then
            REASON='LINK LOST'
            return 1
        fi
        sleep 1
        t=$((t - 1))
    done
    [ -e "$RUN/host-ok" ] && return 0
    REASON='NO HOST'
    return 1
}

# stop_pidfile FILE NAME: stop the process in FILE if it is still NAME
# (PIDs are reused quickly on this camera).
stop_pidfile() {
    p=$(cat "$RUN/$1" 2>/dev/null) || return 0
    rm -f "$RUN/$1"
    case $p in ''|*[!0-9]*) return 0;; esac
    if [ "$(cat "$PROC/$p/comm" 2>/dev/null)" != "$2" ]; then
        log "$1: pid $p is no longer $2"
        return 0
    fi
    kill -TERM "$p" 2>/dev/null || return 0
    log "stopped $1 ($p)"
    wait_until 5 eval "! kill -0 $p 2>/dev/null" || kill -KILL "$p" 2>/dev/null
}
stop_keeper() {
    rm -f "$RUN/reconnecting"
    p=$(cat "$RUN/keeper.pid" 2>/dev/null) || return 0
    [ "$p" = "$$" ] && return 0
    rm -f "$RUN/keeper.pid"
    case $p in ''|*[!0-9]*) return 0;; esac
    ps -p "$p" -o args= 2>/dev/null | grep -q 'wifi-join.sh keep' || return 0
    kill -TERM "$p" 2>/dev/null && log "stopped the keeper ($p)"
    rm -f "$RUN/reconnecting"
}
leave_network() {
    stop_keeper
    stop_pidfile udhcpc.pid udhcpc
    stop_pidfile wpa.pid wpa_supplicant
    rm -rf "$CTRL" "$RUN/wpa.conf" "$RUN/ip" "$RUN/lease"
    ip route del default dev "$IFACE" 2>/dev/null
    ip addr flush dev "$IFACE" 2>/dev/null
    # Down, so that the agent's start loads the access point firmware.
    ifconfig "$IFACE" down 2>/dev/null
}

# restore_ap REASON: leave any network and bring the access point back.
restore_ap() {
    why=$1
    mark "bringing the access point back ($why)"
    leave_network
    sleep 2
    for try in 1 2; do
        if ap_on; then
            mark "access point back (try $try)"
            return 0
        fi
        log "the access point is not up after try $try"
        log "disable_wifi_ap: $(scrub "$(agent disable_wifi_ap)")"
        ifconfig "$IFACE" down 2>/dev/null
        sleep 3
    done
    mark 'THE ACCESS POINT DID NOT COME BACK'
    return 1
}

boot_time() { awk '$1 == "btime" { print $2 }' "$PROC/stat" 2>/dev/null; }

spawn_watchdog() {
    WATCHED=1
    G360_WIFI_DETACHED=1 setsid sh "$SELF" watchdog "$$" \
        </dev/null >>"$LOG" 2>&1 7>&- 8>&- 9>&- &
}

# Start a detached run: guard, watchdog, pause the app, take the locks.
begin_run() {
    mkdir "$RUN/active" 2>/dev/null || { log 'REFUSED: another run is in progress'; exit 1; }
    rm -f "$RUN/done.$$" "$RUN/host-ok"
    mark "run $1 started (boot $(boot_time))"
    sleep "$DETACH_DELAY"
    spawn_watchdog
    if ! stop_app; then
        mark "REFUSED: $REASON"
        touch "$RUN/done.$$"
        rmdir "$RUN/active"
        exit 1
    fi
    if ! take_locks; then
        mark "REFUSED: $REASON"
        finish 0
        exit 1
    fi
}

# finish SECONDS: leave the last message up, then hand the camera back.
finish() {
    sleep "$1"
    touch "$RUN/done.$$"
    drop_locks
    if [ "$(cat "$RUN/app-was-running" 2>/dev/null)" = 1 ]; then
        log 'starting g360-app again'
        G360_WIFI_SKIP_APPLY=1 setsid sh "$APP_START" </dev/null >>"$LOG" 2>&1 &
    fi
    rmdir "$RUN/active" 2>/dev/null
    mark 'run finished'
}

fall_back() {
    mark "join failed: $REASON"
    restore_ap "$REASON"
    finish "$FAIL_HOLD"
    exit 1
}

run_join() {
    read_ap || refuse "$REASON"
    validate_networks || refuse "$REASON"
    begin_run join
    if ! ap_off; then
        if ap_up_now; then
            mark 'the access point did not go off; nothing changed'
            finish "$HOLD"
            exit 1
        fi
        REASON='AP OFF FAIL'
        fall_back
    fi
    mark 'access point off'
    station_firmware || fall_back
    join_candidates "$JOIN_WAIT" || fall_back
    log "joined: $(link_summary)"
    mark "address $IP; $(grep -E '^(subnet|router)=' "$RUN/lease" | tr '\n' ' ')"
    await_host || fall_back
    mark "the host checked in; staying on the network at $IP"
    finish "$HOLD"
}

run_ap_cycle() {
    read_ap || refuse "$REASON"
    begin_run ap-cycle
    if ap_off; then
        mark 'access point off'
    elif ap_up_now; then
        mark 'the access point did not go off; nothing changed'
        finish "$HOLD"
        exit 1
    fi
    sleep 5
    restore_ap 'AP TEST'
    finish "$HOLD"
}

run_revert() {
    read_ap || refuse "$REASON"
    begin_run revert
    restore_ap REVERT
    finish "$HOLD"
}

# --- g360-app's WIFI setting --------------------------------------------
# g360-app runs "apply off|ap|home" in the background when it starts and
# whenever the setting is chosen. There is no screen here: the app has it,
# and its status corner shows the result. One run at a time ($RUN/active,
# shared with the keeper and the watchdog); a run that had to wait goes ahead
# only if it is still the newest choice.

station_up() {
    running wpa_supplicant && [ "$(wpa_state)" = COMPLETED ] && [ -s "$RUN/ip" ]
}

# Is the network in a scan made while the access point keeps running?
# The scan asks for it by name: a plain scan missed it about one time in
# three on the camera, a named one found it five times in five (and it
# also finds a hidden network). One retry before saying it is not in range.
in_range() {
    for try in 1 2; do
        if ! wl scan -s "$SSID" >/dev/null 2>&1; then
            log "named scan $try: wl scan failed"
            sleep 2
            continue
        fi
        sleep "$SCAN_WAIT"
        if wl scanresults 2>/dev/null | S=$SSID awk '
            index($0, "SSID: ") == 1 && $0 == "SSID: \"" ENVIRON["S"] "\"" { found = 1 }
            END { exit !found }'; then
            log "named scan $try: the network is in range"
            return 0
        fi
        log "named scan $try: the network is not in the results"
    done
    return 1
}

keeper_running() {
    k=$(cat "$RUN/keeper.pid" 2>/dev/null) || return 1
    [ -n "$k" ] && ps -p "$k" -o args= 2>/dev/null | grep -q 'wifi-join.sh keep'
}
claim() { mkdir "$RUN/active" 2>/dev/null; }
apply_done() {
    rm -f "$RUN/searching"
    [ "$WATCHED" = 1 ] && touch "$RUN/done.$$"
    rmdir "$RUN/active" 2>/dev/null
    mark "apply $WANT finished"
}
apply_fall_back() {
    rm -f "$RUN/searching"
    echo 1 >"$RUN/error"
    mark "join failed: $REASON"
    restore_ap "$REASON"
    apply_done
    exit 1
}

# Off: leave the network, turn the access point off, take the radio down.
apply_off() {
    left=0
    if running wpa_supplicant || [ -e "$RUN/ip" ]; then
        leave_network
        mark 'left the network'
        left=1
    else
        stop_keeper
    fi
    # Early in boot the stock app's access point may still be on its way;
    # it would come up after this run. So give it AP_SETTLE seconds first.
    if [ "$left" = 0 ] && [ ! -e "$RUN/wifi-off" ] && ! running hostapd; then
        wait_until "$AP_SETTLE" running hostapd && log 'apply off: the access point came up'
    fi
    if running hostapd; then
        if ! ap_off; then
            mark 'apply off: the access point did not go off'
            apply_done
            exit 1
        fi
        mark 'access point off'
    fi
    ifconfig "$IFACE" down 2>/dev/null
    touch "$RUN/wifi-off"
    apply_done
}

# AP: the camera's own access point, leaving the network first.
apply_ap() {
    read_ap || { mark "apply ap: $REASON"; apply_done; exit 1; }
    was_off=0
    [ -e "$RUN/wifi-off" ] && was_off=1
    rm -f "$RUN/wifi-off"
    if ! running wpa_supplicant && [ ! -e "$RUN/ip" ]; then
        if [ "$was_off" = 0 ] && ! ap_up_now; then
            wait_until "$AP_SETTLE" ap_up_now && log 'apply ap: the access point came up'
        fi
        if ap_up_now; then
            log 'apply ap: the access point is up'
            apply_done
            return 0
        fi
    fi
    spawn_watchdog
    restore_ap SETTING || { echo 1 >"$RUN/error"; apply_done; exit 1; }
    apply_done
}

# HOME: the network if it is in range, else keep or bring back the access
# point. After a join a keeper watches the link.
apply_home() {
    if ! validate_networks; then
        log "apply home: $REASON; the access point instead"
        apply_ap
        return
    fi
    read_ap || { mark "apply home: $REASON; not switching"; apply_done; exit 1; }
    if station_up; then
        IP=$(cat "$RUN/ip")
        log "apply home: already on the network at $IP"
        keeper_running || start_keeper
        apply_done
        return 0
    fi
    echo 1 >"$RUN/searching"
    spawn_watchdog
    was_off=0
    [ -e "$RUN/wifi-off" ] && was_off=1
    rm -f "$RUN/wifi-off"
    # A link that is down (the keeper is waiting) starts again from scratch.
    if running wpa_supplicant || [ -e "$RUN/ip" ]; then
        leave_network
        log 'apply home: left the half-joined network to start again'
        was_off=1
    fi
    # Early in boot the stock app may have asked for its access point
    # without getting it yet; if it came up after we joined, it would take
    # the radio. So give it AP_SETTLE seconds to appear first.
    if [ "$was_off" = 0 ] && ! ap_up_now; then
        wait_until "$AP_SETTLE" ap_up_now && log 'apply home: the access point came up'
    fi
    first_candidate=1
    if ap_up_now; then
        candidate=1
        found=0
        while [ "$candidate" -le "$NETWORK_COUNT" ]; do
            read_network "$candidate" || break
            if in_range; then found=1; break; fi
            candidate=$((candidate + 1))
        done
        if [ "$found" = 0 ]; then
            mark 'apply home: the network is not in range; keeping the access point'
            apply_done
            return 0
        fi
        first_candidate=$candidate
        if ! ap_off; then
            if ap_up_now; then
                mark 'the access point did not go off; nothing changed'
                apply_done
                return 0
            fi
            REASON='AP OFF FAIL'
            apply_fall_back
        fi
        mark 'access point off'
    else
        log 'apply home: the access point is not up; trying the network directly'
    fi
    station_firmware || apply_fall_back
    join_candidates "$((RETRY_AFTER + APP_JOIN_WAIT))" "$first_candidate" || apply_fall_back
    log "joined: $(link_summary)"
    mark "address $IP; $(grep -E '^(subnet|router)=' "$RUN/lease" | tr '\n' ' ')"
    start_keeper
    apply_done
}

apply() {
    WANT=$1
    echo "$WANT" >"$RUN/want"
    wait_until "$APPLY_WAIT" claim || { mark "apply $WANT: another run is still in progress"; exit 1; }
    newest=$(cat "$RUN/want" 2>/dev/null)
    if [ "$newest" != "$WANT" ]; then
        log "apply $WANT: replaced by a newer choice ($newest)"
        rmdir "$RUN/active"
        exit 0
    fi
    rm -f "$RUN/done.$$" "$RUN/host-ok"
    rm -f "$RUN/searching" "$RUN/error"
    echo 0 >"$RUN/app-was-running"
    mark "apply $WANT started (boot $(boot_time))"
    case $WANT in
    off) apply_off ;;
    ap) apply_ap ;;
    home) apply_home ;;
    esac
}

# The keeper runs from a copy in RAM (nothing held open on the card) and
# brings the access point back if the link is gone for KEEP_LOST seconds,
# for example when the camera leaves home.
start_keeper() {
    cp "$SELF" "$RUN/wifi-join.sh" || { log 'keeper not started'; return 0; }
    G360_WIFI_DETACHED=1 G360_WIFI_LOG=$LOG G360_WIFI_AP_CACHE=$AP_CACHE \
        setsid sh "$RUN/wifi-join.sh" keep </dev/null >/dev/null 2>&1 7>&- 8>&- 9>&- &
    log "keeper started (pid $!)"
}
keep() {
    echo "$$" >"$RUN/keeper.pid"
    down=0
    while :; do
        sleep "$KEEP_POLL"
        if [ -d "$RUN/active" ]; then down=0; continue; fi
        if ! running wpa_supplicant; then
            log 'keeper: wpa_supplicant has stopped; keeper done'
            rm -f "$RUN/reconnecting"
            echo 1 >"$RUN/error"
            rm -f "$RUN/keeper.pid"
            exit 0
        fi
        if [ "$(wpa_state)" = COMPLETED ]; then rm -f "$RUN/reconnecting"; down=0; continue; fi
        echo 1 >"$RUN/reconnecting"
        down=$((down + KEEP_POLL))
        [ "$down" -ge "$KEEP_LOST" ] || continue
        mkdir "$RUN/active" 2>/dev/null || continue
        mark "keeper: no link for ${down}s; bringing the access point back"
        rm -f "$RUN/reconnecting"
        echo 1 >"$RUN/error"
        read_ap || mark "keeper: $REASON"
        restore_ap 'LINK LOST'
        rmdir "$RUN/active"
        rm -f "$RUN/keeper.pid"
        exit 0
    done
}

# A run that dies part way leaves the access point off: bring it back.
# The run is identified by its PID and its command line, never the PID
# alone, and it says it finished with its own done.<pid> marker.
main_alive() { ps -p "$main" -o args= 2>/dev/null | grep -q 'wifi-join'; }
watchdog() {
    main=$1
    echo "$$" >"$RUN/watchdog.pid"
    t=0
    while [ "$t" -lt "$WATCH_LIMIT" ]; do
        [ -e "$RUN/done.$main" ] && { rm -f "$RUN/done.$main"; exit 0; }
        main_alive || break
        sleep "$WATCH_POLL"
        t=$((t + WATCH_POLL))
    done
    [ -e "$RUN/done.$main" ] && { rm -f "$RUN/done.$main"; exit 0; }
    if main_alive; then
        mark "watchdog: run $main still going after ${WATCH_LIMIT}s; stopping it"
        kill -TERM "$main" 2>/dev/null
        sleep 3
        main_alive && kill -KILL "$main" 2>/dev/null
    fi
    mark "watchdog: run $main ended without finishing"
    rm -f "$RUN/searching"
    echo 1 >"$RUN/error"
    read_ap || mark "watchdog: $REASON"
    take_locks || mark "watchdog: $REASON"
    restore_ap WATCHDOG
    finish "$FAIL_HOLD"
}

ok=1
problem() { echo "  PROBLEM: $*"; ok=0; }
note() { echo "  $*"; }
preflight() {
    ok=1
    [ "$(id -u)" = 0 ] || problem 'not running as root'
    for b in gdbus wpa_supplicant wpa_cli udhcpc ifconfig ip flock setsid wl sync awk sed vconftool; do
        command -v "$b" >/dev/null 2>&1 || problem "missing $b"
    done
    [ -r "$APP_START" ] || problem "no app launcher at $APP_START"
    [ -r "$HERE/udhcpc.sh" ] || problem 'no udhcpc.sh beside this script'
    [ -r "${FW_NET}_blob" ] || problem "no station firmware ${FW_NET}_blob"
    [ -r "$NV_NET" ] || problem "no station nvram $NV_NET"
    [ -w "$DHD/firmware_path" ] && [ -w "$DHD/nvram_path" ] || problem "cannot set the firmware in $DHD"
    if read_ap; then
        note "access point: name ${#AP_SSID} chars ($AP_SOURCE), password ${#AP_PASS} chars, channel $AP_CHANNEL"
    else
        problem "$REASON"
    fi
    if [ "$1" != revert ]; then
        if validate_networks; then
            note "network file: name ${#SSID} chars, key: $KEY"
        else
            problem "$REASON"
        fi
    fi
    if ap_up_now; then note 'the access point is up'; else note 'the access point is not up'; fi
    case $1 in join|ap-cycle)
        running wpa_supplicant && problem 'a wpa_supplicant is already running (use revert)';;
    esac
    [ -d "$RUN/active" ] && problem "a run is in progress ($RUN/active)"
    p=$(pids_named g360-app)
    note "g360-app: ${p:-not running}"
    [ "$ok" = 1 ]
}

status() {
    if ap_up_now; then echo 'access point: up'; else echo 'access point: down'; fi
    echo "wpa_state: $(wpa_state)"
    [ -r "$RUN/lease" ] && grep -E '^(ip|subnet|router)=' "$RUN/lease"
    p=$(pids_named g360-app)
    echo "g360-app: ${p:-not running}"
    [ -d "$RUN/active" ] && echo 'a run is in progress'
    k=$(cat "$RUN/keeper.pid" 2>/dev/null)
    if [ -n "$k" ] && ps -p "$k" -o args= 2>/dev/null | grep -q 'wifi-join.sh keep'; then
        echo "keeper: $k"
    fi
    echo '--- last log lines'
    tail -n 25 "$LOG" 2>/dev/null
}

# Radio scan from the running access point; prints only this network.
scan() {
    read_network || refuse "$REASON"
    wl scan -s "$SSID" 2>&1
    sleep "$SCAN_WAIT"
    wl scanresults 2>&1 | S=$SSID awk '
        index($0, "SSID: ") == 1 {
            mine = ($0 == "SSID: \"" ENVIRON["S"] "\"")
            if (mine) { n++; print "SSID: <yours>" }
            next
        }
        mine { print }
        END { if (!n) print "the network is not in the scan" }'
}

cd / || exit 1
mkdir -p "$RUN" && chmod 700 "$RUN" || exit 1

case "${1:-}" in
check)
    preflight check && echo 'check passed' || { echo 'check FAILED'; exit 1; } ;;
scan) scan ;;
status) status ;;
host-ok)
    touch "$RUN/host-ok" && log 'host-ok received' && echo 'host-ok noted' ;;
join|ap-cycle|revert)
    if [ "${G360_WIFI_DETACHED:-}" != 1 ]; then
        out=$(preflight "$1")
        rc=$?
        echo "$out"
        [ "$rc" -eq 0 ] || refuse "preflight failed for $1"
        G360_WIFI_DETACHED=1 setsid sh "$SELF" "$1" </dev/null >>"$LOG" 2>&1 &
        echo "started $1 (pid $!); progress is in $LOG"
        exit 0
    fi
    case $1 in
    join) run_join ;;
    ap-cycle) run_ap_cycle ;;
    revert) run_revert ;;
    esac ;;
watchdog)
    [ "${G360_WIFI_DETACHED:-}" = 1 ] || refuse 'the watchdog is started by a run'
    watchdog "${2:?pid}" ;;
apply)
    case ${2:-} in
    off|ap|home) apply "$2" ;;
    *) echo 'usage: wifi-join.sh apply off|ap|home' >&2; exit 2 ;;
    esac ;;
keep)
    [ "${G360_WIFI_DETACHED:-}" = 1 ] || refuse 'the keeper is started by apply home'
    keep ;;
*)
    echo 'usage: wifi-join.sh check|scan|status|ap-cycle|join|host-ok|revert|apply off|ap|home' >&2
    exit 2 ;;
esac
