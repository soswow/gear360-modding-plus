#!/bin/sh
#
# ap-boot.sh - ask the camera's mobileap-agent to bring the access point up.
#
# SCOPE: this is ONLY the access-point request.  It installs nothing, changes
# no configuration, touches no service, does not stop or start the stock
# application, and does not start the card's FTP/telnet/web servers.  The
# parent decides when, where and whether this runs.
#
# WHAT IT DOES
#   1. validate the controls it was given, then ask a three-state question of
#      the process list: is hostapd running, is it known not to be running, or
#      is the process list unreadable?  If it is running, report and exit
#      WITHOUT calling enable_wifi_ap (asking again would make the agent turn
#      a working radio off and re-apply the configuration).  If the process
#      list cannot be read, refuse before any bus call - "not observed" is
#      not "not running";
#   2. read ssid and channel from the hostapd.conf the agent wrote last time
#      (the passphrase is NOT read from there - that file holds the derived
#      64-character hex key, which the agent refuses);
#   3. read the passphrase from its own named file; the file is kept, not
#      consumed, so a failed request can be retried;
#   4. validate ssid / key / channel / mode locally, the way the agent will;
#   5. check that the tethering interface answers a read-only probe, and if
#      it does not, stop before the request - the request is never sent into
#      an interface that did not answer;
#   6. call org.tizen.tethering.enable_wifi_ap ONCE, with exactly the six
#      keys the agent's parser accepts, bounded by the system timeout(1);
#   7. read the numeric result out of the method's reply, separately from
#      gdbus's exit status, and report what was actually observed.
#
# THE SIX KEYS the agent's own parser accepts (anything else is refused with
# "Invalid psrameter"):
#   ssid string, key string, sec_type int32 (1 = wpa2-psk),
#   channel int32, visibility boolean, mode string
#
# The key must be the passphrase (8..63 characters), not the derived hex key;
# a 64-character hex key is refused by the agent with "hex key length is not
# correct", so it is rejected here, before the bus is touched.
#
# SECRETS: the passphrase is never printed, never logged, never checksummed,
# and never echoed into an error message.  It IS present in gdbus's argument
# vector, because gdbus's command line requires it and anything on the
# camera can read another process's argv.  That is a property of the CLI,
# not something this script can hide, and this script does not pretend
# otherwise.
#
# OVERRIDES (documented names; the defaults are the camera's own paths):
#   AP_CONF             hostapd.conf      /opt/var/lib/mobileap-agent/hostapd.conf
#   AP_PASSPHRASE_FILE  passphrase file   /opt/storage/sdcard/mods/ap-passphrase
#   AP_LOG              log file          /opt/storage/sdcard/ap-boot.log
#   AP_MODE             mode string       ios
#   AP_TIMEOUT_BIN      timeout utility   timeout
#   AP_TIMEOUT_SECS     gdbus time limit  15   (1..300)
#   AP_BUS_WAIT_SECS    read-only probes  5    (1..300)
#   AP_GDBUS            gdbus binary      gdbus
#   AP_PS               ps binary         ps
#
# EXIT CODES
#   0  hostapd is observed running; connectivity is untested
#   1  the request was accepted or is pending, and hostapd is known not to be
#      running
#   2  the agent refused the settings (method result other than 0/4/9)
#   3  the outcome is unknown: the reply was lost or unreadable, or the state
#      of hostapd could not be read.  The call is NOT repeated, and an
#      observation never turns an unknown or a refusal into "up".
#   4  refused before any bus call (missing/invalid input, invalid control
#      value, missing utility, unreadable process list, silent interface)

set -u

AP_CONF="${AP_CONF:-/opt/var/lib/mobileap-agent/hostapd.conf}"
AP_PASSPHRASE_FILE="${AP_PASSPHRASE_FILE:-/opt/storage/sdcard/mods/ap-passphrase}"
AP_LOG="${AP_LOG:-/opt/storage/sdcard/ap-boot.log}"
AP_MODE="${AP_MODE:-ios}"
AP_TIMEOUT_BIN="${AP_TIMEOUT_BIN:-timeout}"
AP_TIMEOUT_SECS="${AP_TIMEOUT_SECS:-15}"
AP_BUS_WAIT_SECS="${AP_BUS_WAIT_SECS:-5}"
AP_GDBUS="${AP_GDBUS:-gdbus}"
AP_PS="${AP_PS:-ps}"

BUS_DEST="org.tizen.tethering"
BUS_PATH="/Tethering"
BUS_IFACE="org.tizen.tethering"
MAX_CONTROL=300
INT32_MAX=2147483647

log_line() {
    [ -n "$AP_LOG" ] || return 0
    _dir=${AP_LOG%/*}
    [ "$_dir" = "$AP_LOG" ] && _dir=.
    [ -n "$_dir" ] || _dir=/
    [ -d "$_dir" ] || return 0
    printf '%s\n' "$*" >>"$AP_LOG" 2>/dev/null || :
    return 0
}

say() {
    printf '%s\n' "$*"
    log_line "$*"
}

refuse() {
    say "ap-boot: refused: $1"
    exit 4
}

# positive_int VALUE LABEL MAXIMUM - a bounded positive whole number, or a
# refusal.  Zero is refused: a zero timeout would remove the bound, and a
# zero probe wait would skip the probe and still make the request.
positive_int() {
    case "$1" in
        '' | *[!0-9]*) refuse "$2 must be a positive whole number (got '$1')" ;;
    esac
    [ "${#1}" -le 6 ] || refuse "$2 is out of range (got '$1')"
    [ "$1" -ge 1 ] || refuse "$2 must be at least 1 (got '$1')"
    [ "$1" -le "$3" ] || refuse "$2 must be at most $3 (got '$1')"
    return 0
}

# ps_probe - three states, never two:
#   0  hostapd is running
#   1  the process list was read and there is no hostapd
#   2  the process list could not be read: the state is unknown
ps_probe() {
    _out=$("$AP_PS" -e -o comm= 2>/dev/null)
    if [ $? -eq 0 ] && [ -n "$_out" ]; then
        case "$_out" in *hostapd*) return 0 ;; esac
        return 1
    fi
    _out=$("$AP_PS" -e -o comm --no-headers 2>/dev/null)
    if [ $? -eq 0 ] && [ -n "$_out" ]; then
        case "$_out" in *hostapd*) return 0 ;; esac
        return 1
    fi
    _out=$("$AP_PS" -e 2>/dev/null)
    if [ $? -eq 0 ] && [ -n "$_out" ]; then
        case "$_out" in *hostapd*) return 0 ;; esac
        return 1
    fi
    return 2
}

# parse_result - the reply must be exactly one (uint32 N,) tuple.  A warning
# that mentions uint32, a missing tuple, or a reply with more than one value
# is not a result.
parse_result() {
    _lines=$(printf '%s\n' "$1" | grep -c . 2>/dev/null) || _lines=0
    [ "$_lines" = "1" ] || return 1
    printf '%s\n' "$1" | grep -q -x '(uint32 [0-9][0-9]*,)' || return 1
    printf '%s\n' "$1" | sed -n 's/^(uint32 \([0-9][0-9]*\),)$/\1/p'
}

# gvariant_escape: quote a string for the GVariant text format.
# Only the backslash and the apostrophe need escaping there; control
# characters are refused before this is called.
gvariant_escape() {
    printf '%s' "$1" | sed -e 's/\\/\\\\/g' -e "s/'/\\\\'/g"
}

conf_value() {
    sed -n "s/^$1=//p" "$AP_CONF" 2>/dev/null | head -1
}

# ---------------------------------------------------------------- step 1
# The controls first: a bound that is not a bound must not reach gdbus.
positive_int "$AP_TIMEOUT_SECS" "AP_TIMEOUT_SECS" "$MAX_CONTROL"
positive_int "$AP_BUS_WAIT_SECS" "AP_BUS_WAIT_SECS" "$MAX_CONTROL"

command -v "$AP_TIMEOUT_BIN" >/dev/null 2>&1 ||
    refuse "the timeout utility ($AP_TIMEOUT_BIN) is missing, so gdbus could not be bounded"
command -v "$AP_GDBUS" >/dev/null 2>&1 ||
    refuse "gdbus ($AP_GDBUS) is missing"

# Is the access point already up?  Three states: running, known not running,
# or unknown.  An unknown state is refused here, before any bus call.
ps_probe
case $? in
    0)
        say "ap-boot: hostapd is already running; enable_wifi_ap was not called; connectivity is untested"
        exit 0
        ;;
    2)
        refuse "the process list could not be read, so the state of the access point is unknown"
        ;;
esac

# ---------------------------------------------------------------- step 2
# The access point's own settings, as the agent wrote them last time.
[ -f "$AP_CONF" ] || refuse "there is no hostapd configuration at $AP_CONF"
SSID=$(conf_value ssid | tr -d '\r')
CHANNEL=$(conf_value channel | tr -d '\r')
[ -n "$SSID" ] || refuse "the hostapd configuration has no ssid"
[ -n "$CHANNEL" ] || refuse "the hostapd configuration has no channel"

# ---------------------------------------------------------------- step 3
# Local validation, the same limits the agent applies.
SSID_LEN=$(printf '%s' "$SSID" | wc -c | tr -d ' ')
[ "$SSID_LEN" -le 32 ] || refuse "the ssid is $SSID_LEN bytes long; the agent keeps at most 32"
printf '%s' "$SSID" | LC_ALL=C grep -q '[[:cntrl:]]' &&
    refuse "the ssid contains control characters"

# The channel comes from the existing configuration.  What is checked here is
# that it is a positive value that fits in the agent's int32 field - not
# which radio bands or channels this hardware supports.
case "$CHANNEL" in
    '' | *[!0-9]*) refuse "the channel in the configuration is not a number" ;;
esac
[ "${#CHANNEL}" -le 10 ] || refuse "the channel in the configuration is out of range"
[ "$CHANNEL" -ge 1 ] || refuse "the channel must be a positive number (got $CHANNEL)"
[ "$CHANNEL" -le "$INT32_MAX" ] || refuse "the channel must fit in an int32 (got $CHANNEL)"

MODE=$(printf '%s' "$AP_MODE" | tr 'A-Z' 'a-z')
case "$MODE" in
    android | ios) ;;
    *) refuse "the mode must be android or ios" ;;
esac

[ -f "$AP_PASSPHRASE_FILE" ] ||
    refuse "there is no passphrase file at $AP_PASSPHRASE_FILE"
PSK=$(cat "$AP_PASSPHRASE_FILE" 2>/dev/null) ||
    refuse "the passphrase file could not be read"
case "$PSK" in
    *"
"*) refuse "the passphrase file holds more than one line" ;;
esac
PSK=$(printf '%s' "$PSK" | tr -d '\r')
KEY_LEN=$(printf '%s' "$PSK" | wc -c | tr -d ' ')
[ "$KEY_LEN" -ge 8 ] && [ "$KEY_LEN" -le 63 ] ||
    refuse "the passphrase is $KEY_LEN characters long; the agent accepts 8 to 63 (a 64 character hex key is refused as 'hex key length is not correct')"
printf '%s' "$PSK" | LC_ALL=C grep -q '[[:cntrl:]]' &&
    refuse "the passphrase contains control characters"
say "ap-boot: the passphrase is $KEY_LEN characters long"

say "ap-boot: asking the agent for the access point on channel $CHANNEL (ssid is $SSID_LEN bytes)"

# ---------------------------------------------------------------- step 4
# A read-only probe: does the tethering interface answer at all?  Only this
# probe is repeated.  If it never answers, the request is not sent.
probe_ok=0
service_missing=0
waited=0
while [ "$waited" -lt "$AP_BUS_WAIT_SECS" ]; do
    PROBE_ERR=$("$AP_TIMEOUT_BIN" "$AP_TIMEOUT_SECS" "$AP_GDBUS" call --system \
        --dest "$BUS_DEST" --object-path "$BUS_PATH" \
        --method org.freedesktop.DBus.Properties.GetAll \
        "'$BUS_IFACE'" 2>&1 >/dev/null)
    PROBE_RC=$?
    if [ "$PROBE_RC" -eq 0 ]; then
        probe_ok=1
        break
    fi
    case "$PROBE_ERR" in
        *"not activatable"* | *ServiceUnknown* | *"not provided by any .service files"* | *"Unknown object"*)
            service_missing=1
            ;;
    esac
    waited=$((waited + 1))
    [ "$waited" -lt "$AP_BUS_WAIT_SECS" ] && sleep 1
done
if [ "$probe_ok" -ne 1 ]; then
    if [ "$service_missing" -eq 1 ]; then
        refuse "the tethering service is not on the bus"
    fi
    refuse "the tethering interface did not answer the read-only probe; the request was not sent"
fi
say "ap-boot: the tethering interface answered"

# ---------------------------------------------------------------- step 5
# The request.  Exactly the six keys, exactly the agent's types.  The
# dictionary is built as one argument; nothing is evaluated or expanded.
SSID_Q=$(gvariant_escape "$SSID")
KEY_Q=$(gvariant_escape "$PSK")
MODE_Q=$(gvariant_escape "$MODE")
DICT="{'ssid': <'$SSID_Q'>, 'key': <'$KEY_Q'>, 'sec_type': <int32 1>, 'channel': <int32 $CHANNEL>, 'visibility': <true>, 'mode': <'$MODE_Q'>}"

# stderr is dropped, not printed: a gdbus parse error quotes the request
# dictionary back, and the dictionary holds the passphrase.
OUT=$("$AP_TIMEOUT_BIN" "$AP_TIMEOUT_SECS" "$AP_GDBUS" call --system \
    --dest "$BUS_DEST" --object-path "$BUS_PATH" \
    --method "$BUS_IFACE.enable_wifi_ap" \
    "$DICT" 2>/dev/null)
RC=$?

# The numeric method result, read out of the reply, separately from the
# process status.
RESULT=$(parse_result "$OUT") || RESULT=""

# ---------------------------------------------------------------- step 6
# What is actually true: is hostapd running now?  Again three states.
ps_probe
case $? in
    0) POST=running ;;
    1) POST=notrunning ;;
    *) POST=unknown ;;
esac

if [ "$RC" -ne 0 ] || [ -z "$RESULT" ]; then
    say "ap-boot: the reply could not be read (gdbus exit $RC); the outcome is unknown and the call was not repeated"
    case "$POST" in
        running) say "ap-boot: hostapd is running now, which does not show what the request did" ;;
        notrunning) say "ap-boot: hostapd not observed now, which does not show what the request did" ;;
        *) say "ap-boot: the process list could not be read, so the state is unknown too" ;;
    esac
    exit 3
fi

case "$RESULT" in
    0 | 4) KIND=accepted ;;
    9) KIND=pending ;;
    *) KIND=refused ;;
esac

if [ "$KIND" = refused ]; then
    say "ap-boot: the agent refused the request (result $RESULT)"
    case "$POST" in
        running) say "ap-boot: hostapd is running now, but the request itself was refused" ;;
        notrunning) say "ap-boot: hostapd not observed now" ;;
        *) say "ap-boot: the process list could not be read, so the state is unknown too" ;;
    esac
    exit 2
fi

if [ "$KIND" = accepted ]; then
    say "ap-boot: the agent accepted the request (result $RESULT)"
else
    say "ap-boot: the agent is already turning the access point on (result $RESULT)"
fi

case "$POST" in
    running)
        say "ap-boot: hostapd is running; connectivity is untested"
        exit 0
        ;;
    notrunning)
        say "ap-boot: hostapd not observed yet"
        exit 1
        ;;
    *)
        say "ap-boot: the state of hostapd could not be read, so the state of the access point is unknown"
        exit 3
        ;;
esac
