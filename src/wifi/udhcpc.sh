#!/bin/sh
# udhcpc event script for wifi-join.sh: set the address and the default
# route on the interface, and record the lease in the run directory.
# Nothing is written outside that directory (RAM); DNS is left alone.
PATH=${G360_WIFI_PATH:-/usr/sbin:/usr/bin:/sbin:/bin}
export PATH
run=${G360_WIFI_RUN:-/tmp/g360-wifi}
iface=${interface:-wlan0}
event=${1:-}

case "$event" in
deconfig)
    ip addr flush dev "$iface" 2>/dev/null
    rm -f "$run/ip"
    ;;
bound|renew)
    [ -n "${ip:-}" ] || exit 1
    # toybox may give the mask as dotted quads or as a prefix length.
    case "${subnet:-24}" in
    *.*) ifconfig "$iface" "$ip" netmask "$subnet" up ;;
    *)   ip addr flush dev "$iface" 2>/dev/null
         ip addr add "$ip/${subnet:-24}" dev "$iface" ;;
    esac
    if [ -n "${router:-}" ]; then
        set -- $router
        ip route replace default via "$1" dev "$iface"
    fi
    {
        echo "ip=$ip"
        echo "subnet=${subnet:-}"
        echo "router=${router:-}"
        echo "dns=${dns:-}"
        echo "lease=${lease:-}"
        echo "event=$event"
    } >"$run/lease"
    echo "$ip" >"$run/ip"
    ;;
leasefail|nak)
    echo "$event" >>"$run/dhcp-events"
    ;;
esac
exit 0
