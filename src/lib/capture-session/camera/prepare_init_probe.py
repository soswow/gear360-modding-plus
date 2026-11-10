#!/usr/bin/env python3
"""Generate fixed, offline watchdog/recovery payloads for an init-only probe.

This prepares local bytes only. It does not stage or execute them.
"""
from __future__ import annotations

import argparse
import hashlib
import importlib.util
import re
import shlex
import sys
from pathlib import Path


RUNNER = Path(__file__).with_name("run_load_check.py")
OLD_ARGV = '"$helper_path" --load-only --library "$library_path" --log "$progress_path"'
PROBE_ARGV = ('"$helper_path" --probe --probe-hold-ms 30000 --verbose '
              '--library "$library_path" --log "$progress_path"')
#: The same probe with the one zero-length liveness request added.  It is a
#: separate fixed argv so that the plain probe's command is untouched.
PROBE_NOTIFY_ARGV = ('"$helper_path" --probe --probe-notify --probe-hold-ms 30000 '
                     '--verbose --library "$library_path" --log "$progress_path"')

STILL_ARGV = ('"$helper_path" --diagnostic-one-still --library "$library_path" '
              '--log "$progress_path" --free-bytes {free_bytes}')

#: The one-still diagnostic that reproduces the stock application's ordering:
#: the admission handshake, then one liveview start edge, then the one still
#: request.  It is a separate helper binary with its own mode token, so the
#: reviewed one-still helper's own command and its pinned binary stay
#: untouched.
STILL_LIVEVIEW_ARGV = ('"$helper_path" --diagnostic-one-still-liveview --library "$library_path" '
                        '--log "$progress_path" --free-bytes {free_bytes}')

#: The liveview-only diagnostic.  It issues one liveview start edge and observes
#: the app-facing notification callback.  It never issues a still command and
#: never reads an image pointer.  It takes no --free-bytes argument because it
#: does not allocate a RAW buffer.
LIVEVIEW_ARGV = ('"$helper_path" --diagnostic-liveview-only --library "$library_path" '
                 '--log "$progress_path" --free-bytes {free_bytes}')

#: The liveview diagnostic that also reads the transport's destination-alive
#: byte immediately before and after the one liveview request.  It is a
#: separate helper binary with its own mode token, so the liveview-only
#: helper's own command and its pinned binary stay untouched.
LIVEVIEW_LATCH_ARGV = ('"$helper_path" --diagnostic-liveview-latch-only --library "$library_path" '
                       '--log "$progress_path" --free-bytes {free_bytes}')

#: The liveview diagnostic that first performs the transport's own admission
#: handshake, one explicit empty category 0x10 send, and then issues the one
#: liveview request.  It is a separate helper binary with its own mode token.
LIVEVIEW_ADMIT_ARGV = ('"$helper_path" --diagnostic-liveview-admit-only --library "$library_path" '
                        '--log "$progress_path" --free-bytes {free_bytes}')


def validate_diagnostic_mode(diagnostic_still: bool, free_bytes: str | None,
                             local_bootstrap: bool, notify: bool,
                             diagnostic_liveview: bool = False,
                             diagnostic_liveview_latch: bool = False,
                             diagnostic_liveview_admit: bool = False,
                             diagnostic_still_liveview: bool = False) -> str | None:
    """Validate fixed-mode arguments without accessing the camera."""
    if not isinstance(diagnostic_still, bool):
        raise ValueError("diagnostic_still must be boolean")
    if not isinstance(diagnostic_liveview, bool):
        raise ValueError("diagnostic_liveview must be boolean")
    if not isinstance(diagnostic_liveview_latch, bool):
        raise ValueError("diagnostic_liveview_latch must be boolean")
    if not isinstance(diagnostic_liveview_admit, bool):
        raise ValueError("diagnostic_liveview_admit must be boolean")
    if not isinstance(diagnostic_still_liveview, bool):
        raise ValueError("diagnostic_still_liveview must be boolean")
    liveview_modes = [diagnostic_liveview, diagnostic_liveview_latch,
                       diagnostic_liveview_admit]
    if sum(1 for mode in liveview_modes if mode) > 1:
        raise ValueError("the liveview diagnostic modes are exclusive")
    if diagnostic_still and any(liveview_modes):
        raise ValueError("diagnostic_still and the liveview modes are exclusive")
    if diagnostic_still and diagnostic_still_liveview:
        raise ValueError("diagnostic_still and diagnostic_still_liveview are exclusive")
    if diagnostic_still_liveview and any(liveview_modes):
        raise ValueError("diagnostic_still_liveview and the liveview modes are exclusive")
    if not diagnostic_still and not diagnostic_still_liveview:
        if any(liveview_modes):
            # The liveview-only helper also takes --free-bytes, because its own
            # CLI is strict about it.  It never allocates a RAW buffer, so the
            # value is only parsed and recorded.
            if not isinstance(free_bytes, str) or re.fullmatch(r"[0-9]+", free_bytes) is None:
                raise ValueError("free_bytes must be an ASCII unsigned decimal u64")
            canonical = free_bytes.lstrip("0") or "0"
            if len(canonical) > 20 or (len(canonical) == 20 and canonical > "18446744073709551615"):
                raise ValueError("free_bytes exceeds u64")
            return canonical
        if free_bytes is not None:
            raise ValueError("free_bytes requires a diagnostic mode")
        return None
    if local_bootstrap is not True or notify is not True:
        raise ValueError("the still diagnostic modes require local_bootstrap and notify")
    if not isinstance(free_bytes, str) or re.fullmatch(r"[0-9]+", free_bytes) is None:
        raise ValueError("free_bytes must be an ASCII unsigned decimal u64")
    canonical = free_bytes.lstrip("0") or "0"
    if len(canonical) > 20 or (len(canonical) == 20 and canonical > "18446744073709551615"):
        raise ValueError("free_bytes exceeds u64")
    return canonical


def load_runner():
    spec = importlib.util.spec_from_file_location("gear360_run_load_check", RUNNER)
    if spec is None or spec.loader is None:
        raise RuntimeError(f"cannot import {RUNNER}")
    module = importlib.util.module_from_spec(spec)
    sys.modules[spec.name] = module
    spec.loader.exec_module(module)
    return module


def boot_guard(expected: str) -> str:
    # expected has already been restricted to UUID:btime, so it is safe as a
    # single-quoted shell literal.
    return f'''probe_guard() {{
  GUARD_REASON=unit-read
  unit_state=$(systemctl show --property=ActiveState --property=MainPID di-camera-app.service 2>/dev/null) || return 1
  active=$(printf '%s\\n' "$unit_state" | sed -n 's/^ActiveState=//p')
  mainpid=$(printf '%s\\n' "$unit_state" | sed -n 's/^MainPID=//p')
  [ -n "$active" ] && [ -n "$mainpid" ] || return 1
  GUARD_REASON=unit-state
  [ "$active" = inactive ] || return 1
  GUARD_REASON=unit-mainpid
  [ "$mainpid" = 0 ] || return 1
  for proc_dir in "$PROC_ROOT"/[0-9]*; do
    [ -d "$proc_dir" ] || continue
    proc_comm=$(cat "$proc_dir/comm" 2>/dev/null)
    if [ $? -ne 0 ]; then
      [ -d "$proc_dir" ] || continue
      GUARD_REASON=comm-read
      return 1
    fi
    GUARD_REASON=stock-seen
    [ "$proc_comm" != di-camera-app ] || return 1
  done
  GUARD_REASON=boot-read
  current_boot=$(cat "$PROC_ROOT/sys/kernel/random/boot_id" 2>/dev/null) || return 1
  [ -n "$current_boot" ] || return 1
  GUARD_REASON=boot-time-read
  current_btime=$(awk '$1 == "btime" {{print $2; exit}}' "$PROC_ROOT/stat" 2>/dev/null) || return 1
  [ -n "$current_btime" ] || return 1
  GUARD_REASON=boot-changed
  [ "$current_boot:$current_btime" = '{expected}' ]
}}
'''


def build_payloads(remote_dir: str, expected_boot: str,
                   local_bootstrap: bool = False,
                   notify: bool = False, diagnostic_still: bool = False,
                   free_bytes: str | None = None,
                   diagnostic_liveview: bool = False,
                   diagnostic_liveview_latch: bool = False,
                   diagnostic_liveview_admit: bool = False,
                   diagnostic_still_liveview: bool = False) -> tuple[str, str]:
    canonical_free = validate_diagnostic_mode(diagnostic_still, free_bytes, local_bootstrap,
                                              notify, diagnostic_liveview,
                                              diagnostic_liveview_latch,
                                              diagnostic_liveview_admit,
                                              diagnostic_still_liveview)
    runner = load_runner()
    if not runner.is_generated_remote_dir(remote_dir):
        raise ValueError("remote_dir must be /tmp/gear360-load-<12 lowercase hex>")
    if re.fullmatch(r"[0-9a-f]{8}(?:-[0-9a-f]{4}){3}-[0-9a-f]{12}:[0-9]+", expected_boot) is None:
        raise ValueError("expected_boot must be a lowercase UUID followed by ':' and decimal btime")

    cfg = runner.Config(helper=Path("/unused/offline-only"), helper_sha256="0" * 64,
                        deadline_seconds=45, http_timeout=120.0)
    watchdog = runner.build_watchdog_script(cfg, remote_dir)
    if watchdog.count(OLD_ARGV) != 1:
        raise RuntimeError("runner watchdog argv drift: expected exactly one fixed --load-only command")
    if diagnostic_liveview_admit:
        probe_argv = LIVEVIEW_ADMIT_ARGV.format(free_bytes=canonical_free)
    elif diagnostic_liveview_latch:
        probe_argv = LIVEVIEW_LATCH_ARGV.format(free_bytes=canonical_free)
    elif diagnostic_liveview:
        probe_argv = LIVEVIEW_ARGV.format(free_bytes=canonical_free)
    elif diagnostic_still_liveview:
        probe_argv = STILL_LIVEVIEW_ARGV.format(free_bytes=canonical_free)
    elif diagnostic_still:
        probe_argv = STILL_ARGV.format(free_bytes=canonical_free)
    else:
        probe_argv = PROBE_NOTIFY_ARGV if notify else PROBE_ARGV
    if local_bootstrap and not diagnostic_still and not diagnostic_liveview \
            and not diagnostic_liveview_latch and not diagnostic_liveview_admit \
            and not diagnostic_still_liveview:
        if probe_argv.count(' --probe ') != 1:
            raise RuntimeError("probe argv drift before local-bootstrap insertion")
        probe_argv = probe_argv.replace(' --probe ', ' --probe --probe-local-bootstrap ', 1)
    watchdog = watchdog.replace(OLD_ARGV, probe_argv)

    launch = probe_argv
    guard_fn = boot_guard(expected_boot)
    cd_line = f"cd {shlex.quote(remote_dir)} || exit 90"
    if watchdog.count(cd_line) != 1:
        raise RuntimeError("runner watchdog structure drift before probe guard insertion")
    watchdog = watchdog.replace(cd_line, guard_fn + cd_line, 1)

    launch_line = launch + ' > "$stdout_path" 2> "$stderr_path" &'
    prelaunch = '''if ! probe_guard; then
  printf 'prelaunch\\n' > "$guard_path"
  printf '%s\\n' "$GUARD_REASON" > "$guard_reason_path"
  printf 'outcome=guard-lost\\nrc=unknown\\nterminated=no\\n' > "$status_path"
  exit 0
fi
'''
    if watchdog.count(launch_line) != 1:
        raise RuntimeError("runner watchdog probe launch drift")
    watchdog = watchdog.replace(launch_line, prelaunch + launch_line, 1)

    # Insert the marker path through a strict shell assignment anchor.
    owner_assignment = f"owner_path={shlex.quote(f'{remote_dir}/owner')}\n"
    if watchdog.count(owner_assignment) != 1:
        raise RuntimeError("runner watchdog owner-path assignment drift")
    watchdog = watchdog.replace(owner_assignment,
                                owner_assignment +
                                f"guard_path={shlex.quote(f'{remote_dir}/guard-lost')}\n" +
                                f"guard_reason_path={shlex.quote(f'{remote_dir}/guard-lost.reason')}\n", 1)

    loop_anchor = '    state=$(proc_state "$child")'
    if watchdog.count(loop_anchor) != 1:
        raise RuntimeError("runner watchdog loop anchor drift")
    lost_guard = '''    if ! probe_guard; then
      printf 'midrun\\n' > "$guard_path"
      printf '%s\\n' "$GUARD_REASON" > "$guard_reason_path"
      outcome=guard-lost
      if own_child "$child" "$tick" "$helper_path"; then
        kill -TERM "$child" 2>/dev/null
        sleep 2
        if own_child "$child" "$tick" "$helper_path"; then
          kill -KILL "$child" 2>/dev/null
          sleep 1
        fi
        case "$(proc_state "$child")" in
          stopped) terminated=yes ;;
          running) outcome=guard-lost-kill-unconfirmed; terminated=no ;;
          *) outcome=guard-lost-state-unknown; terminated=no ;;
        esac
      else
        nowtick=$(awk '{print $22}' "$PROC_ROOT/$child/stat" 2>/dev/null)
        if [ -z "$nowtick" ]; then
          outcome=identity-unknown
        elif [ "$nowtick" != "$tick" ]; then
          outcome=identity-lost
        else
          outcome=image-changed
        fi
        terminated=no
      fi
      break
    fi
'''
    watchdog = watchdog.replace(loop_anchor, lost_guard + loop_anchor, 1)

    recovery = runner.build_recovery_script(remote_dir)
    return watchdog, recovery


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("remote_dir")
    parser.add_argument("expected_boot")
    parser.add_argument("output_dir", type=Path)
    parser.add_argument("--local-bootstrap", action="store_true",
                        help="include the explicit local-bootstrap diagnostic step")
    args = parser.parse_args()
    watchdog, recovery = build_payloads(args.remote_dir, args.expected_boot,
                                        args.local_bootstrap)
    args.output_dir.mkdir(parents=True, exist_ok=True)
    for name, body in (("watchdog.sh", watchdog), ("recovery.sh", recovery)):
        path = args.output_dir / name
        path.write_text(body + "\n", encoding="utf-8")
        print(f"{path} sha256={hashlib.sha256(path.read_bytes()).hexdigest()}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
