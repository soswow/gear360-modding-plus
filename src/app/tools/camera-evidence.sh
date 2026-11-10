#!/bin/sh
# Run one read-only command on the camera and save the result as evidence,
# in the same shape as the other evidence files in this repository.
#
# Usage: ./src/app/tools/camera-evidence.sh output-file camera-command
#
# The command runs through the mod's HTTP shell.  Nothing here changes the
# camera: no service is started, no firmware is written and the camera is
# not power-cycled.  The boot identity is recorded with the result, so a
# later reader can tell which boot produced it.
set -eu

out=${1:?usage: $0 output-file camera-command}
command=${2:?usage: $0 output-file camera-command}
base=${GEAR360_CGI_BASE:-http://192.168.43.1:8888/cgi-bin}

response=$(mktemp)
trap 'rm -f "$response"' EXIT HUP INT TERM

start=$(date +%s)
curl --noproxy '*' --fail --silent --show-error --connect-timeout 5 --max-time 120 --get \
  --data-urlencode "rawcmd=$command" \
  "$base/execute" >"$response"
end=$(date +%s)

{
  echo "# command: $command"
  echo "# elapsed_s: $((end - start))"
  echo "# boot_id: $(curl --noproxy '*' --fail --silent --show-error --connect-timeout 5 --max-time 20 --get \
      --data-urlencode 'rawcmd=cat /proc/sys/kernel/random/boot_id' "$base/execute" | \
      sed -n 's/.*\([0-9a-f]\{8\}-[0-9a-f]\{4\}-[0-9a-f]\{4\}-[0-9a-f]\{4\}-[0-9a-f]\{12\}\).*/\1/p')"
  sed -e 's/<[^>]*>//g' -e 's/&lt;/</g' -e 's/&gt;/>/g' -e 's/&amp;/\&/g' "$response" | \
    sed -e '1,/^Executing:/d' -e '/^Return to the home page$/d'
} >"$out"

echo "Saved $out"
