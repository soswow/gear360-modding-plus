#!/bin/sh
# Deploy the remote page (src/app/README.md, "Remote control: the web page"):
# remote.html, the CGI (the g360-remote build, as cgi-bin/g360) and the old
# address hdr.html, which now redirects to remote.html. Each file goes
# through tools/deploy-over-web.sh (RAM, check, NAME.new, check, delete and
# rename, check).
#
# Usage: sh src/app/web/deploy.sh [--retire-hdr] [BUILD_DIR]
#   --retire-hdr  also deletes the old HDR page's CGIs and runner, which
#                 drove the stock app (hdr/web/README.md), from the card
#   BUILD_DIR     where build-arm.sh put g360-remote
#                 (default output/camera-app-build)
# GEAR360_CGI_BASE picks the camera (default: its own access point), e.g.
# GEAR360_CGI_BASE=http://<camera-address>:8888/cgi-bin
# when it has joined your network. Keep your own addresses in camera-hosts
# (see camera-hosts.example).
set -eu
retire=
if [ "${1:-}" = --retire-hdr ]; then retire=1; shift; fi
here=$(CDPATH= cd -- "$(dirname "$0")" && pwd)
root=$(CDPATH= cd -- "$here/../../.." && pwd)
build=${1:-"$root/output/camera-app-build"}
deploy="$here/../tools/deploy-over-web.sh"
www=/opt/storage/sdcard/mods/www
base=${GEAR360_CGI_BASE:-http://192.168.43.1:8888/cgi-bin}

[ -x "$build/g360-remote" ] || { echo "no $build/g360-remote: run sh src/app/build-arm.sh" >&2; exit 2; }
sh "$deploy" "$build/g360-remote" "$www/cgi-bin/g360"
sh "$deploy" "$here/remote.html" "$www/remote.html"
sh "$deploy" "$here/hdr.html" "$www/hdr.html"

if [ -n "$retire" ]; then
    curl --fail --silent --show-error --max-time 20 --get --data-urlencode \
        "rawcmd=rm -f $www/cgi-bin/hdr-start $www/cgi-bin/hdr-status $www/cgi-bin/hdr-stop /opt/storage/sdcard/mods/hdr-capture.sh && sync" \
        "$base/execute" >/dev/null
    echo 'Old HDR CGIs and runner deleted from the card'
fi
host=${base#*://}
echo "Remote: http://${host%%/*}/remote.html"
