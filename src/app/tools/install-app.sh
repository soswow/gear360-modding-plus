#!/bin/sh
# Install a built g360-app on the camera so the launcher always has a pair it
# accepts: start.sh only starts g360-app when g360-app.sha256 names its hash.
#
# Usage: ./src/app/tools/install-app.sh [--backup LABEL] [--restart] path/to/g360-app
#
#   1. The build is staged next to the app as g360-app.new
#      (deploy-over-web.sh --stage: uploaded to camera RAM, checked, copied to
#      the card, checked again), and its hash as g360-app.sha256.new.
#   2. --backup LABEL copies the current pair to g360-app.g360-before-LABEL
#      and g360-app.sha256.g360-before-LABEL (refused if they exist).
#   3. --restart stops the running app with SIGTERM (an in-flight shot or
#      sweep finishes first). Without it, a running app is refused: the file
#      it runs from is never deleted under it.
#   4. One camera command puts both new files in place (delete, then rename:
#      AGENTS.md, "Camera rules"), and both are checked.
#   5. --restart then runs start.sh, the same path as Double-Power.
# A cut-off before step 4 leaves the old app and hash file working.
set -eu
here=$(CDPATH= cd -- "$(dirname "$0")" && pwd)
usage="usage: $0 [--backup LABEL] [--restart] path/to/g360-app"
backup= restart=
while [ $# -gt 1 ]; do
  case $1 in
    --backup) backup=$2; shift 2;;
    --restart) restart=1; shift;;
    *) break;;
  esac
done
[ $# -eq 1 ] || { echo "$usage" >&2; exit 2; }
binary=$1
[ -f "$binary" ] || { echo "no such file: $binary" >&2; exit 2; }
case $backup in *[!A-Za-z0-9._-]*) echo "bad backup label: $backup" >&2; exit 2;; esac
base=${GEAR360_CGI_BASE:-http://192.168.43.1:8888/cgi-bin}
export GEAR360_CGI_BASE="$base"
dir=/opt/storage/sdcard/mods/g360-app-v1-20261105
hash=$(shasum -a 256 "$binary" | awk '{print $1}')

die() { echo "install-app: $*" >&2; exit 1; }
# One command on the camera; prints its output. try_cam returns 1 when the
# camera doesn't answer, cam ends the script.
try_cam() {
  reply=$(curl --fail --silent --show-error --max-time "${2:-20}" --get \
    --data-urlencode "rawcmd=$1" "$base/execute") || return 1
  printf '%s\n' "$reply" | awk '/<\/pre>/ {p = 0} p {print} /^Executing:/ {p = 1}'
}
cam() { try_cam "$@" || exit 1; }
# Prints the app's PIDs; a camera that doesn't answer ends the script, so it
# never reads as "not running". Call it as p=$(pids), not inside a test.
pids() { out=$(cam "pgrep -x g360-app") || exit 1; printf '%s' "$out" | tr -cd '0-9 \n'; }

running=$(pids)
if [ -n "$running" ] && [ -z "$restart" ]; then
  die "g360-app is running (PID $(echo $running)); pass --restart to stop it, install and start it again"
fi

sh "$here/deploy-over-web.sh" --stage "$binary" "$dir/g360-app"
[ "$(cam "rm -f $dir/g360-app.sha256.new && echo $hash > $dir/g360-app.sha256.new && sync && cat $dir/g360-app.sha256.new" 60)" = "$hash" ] ||
  die "staged hash file does not read back as $hash"

if [ -n "$backup" ]; then
  old=g360-app.g360-before-$backup oldsha=g360-app.sha256.g360-before-$backup
  [ "$(cam "cd $dir && [ ! -e $old ] && [ ! -e $oldsha ] && cp g360-app $old && cp g360-app.sha256 $oldsha && sync && echo saved" 60)" = saved ] ||
    die "backup $old not made (does it exist already?); nothing installed"
  echo "Backed up the current app as $dir/$old"
fi

running=$(pids)
if [ -n "$restart" ] && [ -n "$running" ]; then
  cam "for p in \$(pgrep -x g360-app); do grep -qx g360-app /proc/\$p/comm && kill -TERM \$p; done; true" >/dev/null
  i=0
  while running=$(pids) && [ -n "$running" ]; do
    i=$((i + 1))
    [ "$i" -le 120 ] || die "g360-app did not stop in 120 s; nothing installed (new files staged as .new)"
    sleep 1
  done
  echo "Stopped g360-app"
fi
running=$(pids)
[ -z "$running" ] || die "g360-app is running (PID $(echo $running)); nothing installed (new files staged as .new)"

[ "$(cam "cd $dir && rm -f g360-app && mv g360-app.new g360-app && rm -f g360-app.sha256 && mv g360-app.sha256.new g360-app.sha256 && sync && echo swapped" 60)" = swapped ] ||
  die "swap did not finish: check $dir on the camera"
actual=$(cam "/usr/bin/openssl dgst -sha256 $dir/g360-app" | sed -n 's/.*= *\([0-9a-f]\{64\}\).*/\1/p')
[ "$actual" = "$hash" ] || die "installed g360-app reads back as ${actual:-unreadable}, not $hash"
[ "$(cam "cat $dir/g360-app.sha256")" = "$hash" ] || die "installed g360-app.sha256 does not name $hash"
echo "Installed g360-app $hash"

[ -n "$restart" ] || exit 0
cam "setsid sh $dir/start.sh </dev/null >/tmp/g360-start.log 2>&1 &" >/dev/null
# start.sh waits up to 30 s for the stock app to stop. Once running, the app
# applies its WIFI setting, which can take the camera off this network.
i=0
while [ "$i" -lt 45 ]; do
  sleep 1
  i=$((i + 1))
  if now=$(try_cam "pgrep -x g360-app" 5); then
    now=$(printf '%s' "$now" | tr -cd '0-9 \n')
    [ -z "$now" ] || { echo "Started g360-app (PID $(echo $now))"; exit 0; }
  else
    echo "Launched start.sh; the camera stopped answering at $base (its WIFI setting may have moved it to the other network)"
    exit 0
  fi
done
log=$(try_cam "cat /tmp/g360-start.log" || true)
die "g360-app did not start in 45 s. start.sh said: ${log:-nothing}"
