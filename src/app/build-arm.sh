#!/bin/sh
# Offline build: one app with the capture, controls, display and output modules.
set -eu
here=$(CDPATH= cd -- "$(dirname "$0")" && pwd)
lib=$here/../lib
out=${1:-"$here/../../output/camera-app-build"}
dump=${GEAR360_DUMP:-"$here/../../dump/extracted/platform"}
clang=${CLANG:-/opt/homebrew/opt/llvm/bin/clang}
lld=${LLD:-/opt/homebrew/opt/llvm@17/bin/ld.lld}
mkdir -p "$out"
cflags="-target armv7a-linux-gnueabi -marm -mcpu=cortex-a9 -std=c11 -O2 -fno-builtin -fno-stack-protector -ffreestanding -Wall -Wextra -Werror -nostdlib -fno-pie -I$here -I$lib/capture-session -I$lib/encoded-set -I$lib/g360cap -I$lib/g360ctl -I$lib/g360display"
for src in "$here/main.c" "$here/storage.c" "$here/queue.c" "$here/wear.c" "$here/settings.c" "$here/status.c" "$here/alerts.c" "$here/wifi.c" "$here/remote.c" "$here/preview.c" "$here/power.c" "$here/power_relaunch.c" "$here/motion.c" "$lib/g360display/g360display.c" "$lib/g360ctl/g360ctl.c" "$lib/encoded-set/encoded_set.c" "$lib/g360cap/g360cap.c" "$lib/g360cap/g360cap_check.c" "$lib/g360cap/g360_dng.c" "$lib/g360cap/g360_exif.c" "$lib/g360cap/g360_output.c" "$lib/g360cap/g360_pose.c"; do
    # shellcheck disable=SC2086
    "$clang" $cflags -c "$src" -o "$out/$(basename "$src" .c).o"
done
# shellcheck disable=SC2086
"$clang" $cflags -no-pie -fuse-ld=lld --ld-path="$lld" -Wl,--dynamic-linker=/lib/ld-linux.so.3 -Wl,--allow-shlib-undefined -Wl,--no-undefined -Wl,-z,noexecstack -L"$dump/lib" -L"$dump/usr/lib" -Wl,-rpath-link,"$dump/lib" -Wl,-rpath-link,"$dump/usr/lib" "$lib/capture-session/start_hosted.S" "$out/main.o" "$out/storage.o" "$out/queue.o" "$out/wear.o" "$out/settings.o" "$out/status.o" "$out/alerts.o" "$out/wifi.o" "$out/remote.o" "$out/preview.o" "$out/power.o" "$out/power_relaunch.o" "$out/motion.o" "$out/g360display.o" "$out/g360ctl.o" "$out/encoded_set.o" "$out/g360cap.o" "$out/g360cap_check.o" "$out/g360_dng.o" "$out/g360_exif.o" "$out/g360_output.o" "$out/g360_pose.o" -o "$out/g360-app" -Wl,--no-as-needed -l:libm.so.6 -l:libdevice-node.so.0.1 -Wl,--as-needed -l:libpthread.so.0 -l:libc.so.6 -l:libdl.so.2 -l:librt.so.1 -l:libgcc_s.so.1
# The web page's CGI (remote_cgi.c): the socket client alone.
"$clang" $cflags -c "$here/remote_cgi.c" -o "$out/remote_cgi.o"
# shellcheck disable=SC2086
"$clang" $cflags -no-pie -fuse-ld=lld --ld-path="$lld" -Wl,--dynamic-linker=/lib/ld-linux.so.3 -Wl,--allow-shlib-undefined -Wl,--no-undefined -Wl,-z,noexecstack -L"$dump/lib" -L"$dump/usr/lib" -Wl,-rpath-link,"$dump/lib" -Wl,-rpath-link,"$dump/usr/lib" "$lib/capture-session/start_hosted.S" "$out/remote_cgi.o" "$out/remote.o" -o "$out/g360-remote" -Wl,--as-needed -l:libc.so.6
shasum -a 256 "$out/g360-app" "$out/g360-remote"
