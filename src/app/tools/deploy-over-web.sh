#!/bin/sh
# Deploy a local file to the camera's SD card: FTP for the bytes, the mod's
# HTTP shell for the checks and the swap.
#
# Usage: ./src/app/tools/deploy-over-web.sh [--stage] local-file remote-path [chunk]
#
# The upload never writes to remote-path itself, so a cut-off upload (Wi-Fi
# drop, reboot, a killed script) leaves the old file working:
#   1. The file goes into /tmp/g360-up.<pid> (camera RAM) over FTP (port 21,
#      BusyBox ftpd started by the SD boot hook): 98 KB in about 0.2 s. If
#      FTP fails, it goes as base64 pieces through the HTTP shell instead,
#      about 2.7 s per KB (4.5 minutes for the app).
#   2. Once that copy's hash matches, it is copied to remote-path.new on the
#      card, synced, and read back.
#   3. One camera command deletes the old file, then renames remote-path.new
#      into its place. Delete first: renaming over an existing file on this
#      card leaves two entries with one name (AGENTS.md, "Camera rules").
#   4. The final file's hash is checked.
# --stage stops after step 2 (tools/install-app.sh swaps the app and its hash
# file together).
#
# The CGI only accepts short query strings, so the fallback sends base64
# pieces.  Every piece is a multiple of four characters, so each one decodes
# on its own and can simply be appended.  The camera's CGI drops commands
# silently past about 64 characters of base64, so 64 is the size used here;
# a lost piece only shows in the hash check.  Every request has a time
# limit, so a dead connection fails the deploy instead of hanging it.
# GEAR360_CGI_BASE picks the camera (default: its access point);
# GEAR360_FTP_BASE overrides the FTP address (default: root on the same host).
set -eu

stage=
if [ "${1:-}" = --stage ]; then stage=1; shift; fi
local=${1:?usage: $0 [--stage] local-file remote-path [chunk]}
remote=${2:?usage: $0 [--stage] local-file remote-path [chunk]}
chunk=${3:-64}
base=${GEAR360_CGI_BASE:-http://192.168.43.1:8888/cgi-bin}
host=${base#*://}
host=${host%%[:/]*}
ftp=${GEAR360_FTP_BASE:-ftp://root:@$host}
local_sha=$(shasum -a 256 "$local" | awk '{print $1}')
tmp=/tmp/g360-up.$$
new=$remote.new

# The launcher only starts g360-app when g360-app.sha256 names it, so the two
# must change together.
case $remote in
  */g360-app) [ -n "$stage" ] || {
    echo "Use src/app/tools/install-app.sh for g360-app: it swaps the app and its hash file together" >&2
    exit 2
  };;
esac

# Runs one command on the camera and prints its output (the CGI wraps it in
# HTML after an "Executing:" line). A failed request ends the script.
cam() {
  reply=$(curl --fail --silent --show-error --max-time "${2:-20}" --get \
    --data-urlencode "rawcmd=$1" "$base/execute") || exit 1
  printf '%s\n' "$reply" | awk '/<\/pre>/ {p = 0} p {print} /^Executing:/ {p = 1}'
}
sha_of() {
  cam "/usr/bin/openssl dgst -sha256 $1" | sed -n 's/.*= *\([0-9a-f]\{64\}\).*/\1/p'
}
check() {
  actual=$(sha_of "$1")
  if [ "$actual" != "$local_sha" ]; then
    echo "Deployment check failed for $1 on the camera${2- (old file untouched)}" >&2
    echo "  local  $local_sha" >&2
    echo "  remote ${actual:-unreadable}" >&2
    exit 1
  fi
}

# A failed or interrupted deploy removes its copy from camera RAM if the
# camera still answers. The live file is only replaced after both checks.
trap 'curl --silent --max-time 5 --get --data-urlencode "rawcmd=rm -f $tmp" "$base/execute" >/dev/null 2>&1 || true' EXIT
trap 'exit 1' INT TERM HUP
if curl --silent --connect-timeout 5 --max-time 60 --upload-file "$local" "$ftp$tmp"; then
  via=FTP
else
  echo "FTP upload failed; sending it through the HTTP shell instead (about 2.7 s per KB)" >&2
  via="the HTTP shell"
  cam ": > $tmp" >/dev/null
  # The echo ends the last piece with a newline, so read sees it.
  { base64 <"$local" | tr -d '\n'; echo; } | fold -w "$chunk" | while IFS= read -r piece; do
    cam "echo $piece | openssl base64 -d >> $tmp" >/dev/null
  done
fi
check "$tmp"
cam "rm -f $new && cp $tmp $new && chmod 755 $new && sync && rm -f $tmp" 60 >/dev/null
check "$new"
if [ -n "$stage" ]; then
  printf 'Staged %s as %s over %s (%s)\n' "$local" "$new" "$via" "$local_sha"
  exit 0
fi
cam "rm -f $remote && mv $new $remote && sync" 60 >/dev/null
check "$remote" " after the swap"
printf 'Deployed %s to %s over %s (%s)\n' "$local" "$remote" "$via" "$local_sha"
