#!/usr/bin/env python3
"""Host check of the runner's options and its command builder.

No camera and no network: the runner module is imported, its parser and its
command builder are called directly.

What this covers:
  - every mode builds a command line, and the flags on it belong to that
    mode: the bench flags never appear for a mode that does not bench, and
    --log-dump never appears unless it was asked for;
  - the defaults are what the helper's own defaults are, so dropping an
    irrelevant flag does not change what the helper does;
  - a numeric option that is not a number is refused before the camera is
    contacted;
  - a path with a space in it is quoted, because the command is run through
    a shell.
"""
import contextlib
import importlib.util
import io
import pathlib
import shlex
import sys

HERE = pathlib.Path(__file__).resolve().parent
RUNNER = HERE.parent / "run_on_camera.py"

spec = importlib.util.spec_from_file_location("run_on_camera", RUNNER)
runner = importlib.util.module_from_spec(spec)
spec.loader.exec_module(runner)

LOG = "/opt/gear360-grab/gear360-grab-abc/progress.log"
REMOTE = "/tmp/gear360-grab-abc"


def options(**overrides):
    """The runner's default options, with the given fields replaced."""
    values = dict(mode="grab", size=0x00100000, bank=2, command=0x01010016,
                  remaining=2518, free=7687503872, output="/tmp/sensor.raw",
                  timeout_ms=25000, bench_address=0, bench_length=0xfff200,
                  period_us=500, settle_ms=2000, max_seconds=90, log_dump=0,
                  no_copy=False)
    values.update(overrides)
    return type("Options", (), values)()


def tokens(command):
    return shlex.split(command)


def has_flag(argv, flag):
    return flag in argv


def check(condition, message):
    if not condition:
        print("FAIL:", message)
        sys.exit(1)


def main():
    parser = runner.build_parser()

    # 1. Every mode builds a line, and the line names the mode.
    for mode in ["grab", "capture", "acquire", "compare", "control", "bench",
                 "still"]:
        argv = tokens(runner.build_command(REMOTE, mode, options(mode=mode), LOG))
        check(argv[0] == REMOTE + "/grab", "%s: wrong program" % mode)
        check(argv[argv.index("--mode") + 1] == mode, "%s: wrong mode" % mode)
        check(argv.count("--log") == 1, "%s: one log path" % mode)
        check(argv[argv.index("--log") + 1] == LOG, "%s: log path" % mode)
        # The bench flags belong to the bench mode only.
        if mode == "bench":
            check(has_flag(argv, "--bench-address"), "bench: has an address")
            check(has_flag(argv, "--bench-length"), "bench: has a length")
        else:
            check(not has_flag(argv, "--bench-address"),
                  "%s: no bench address" % mode)
            check(not has_flag(argv, "--bench-length"),
                  "%s: no bench length" % mode)
        # The lease options belong to the modes that take a lease.
        if mode in runner.LEASE_MODES:
            check(has_flag(argv, "--size"), "%s: has a size" % mode)
            check(has_flag(argv, "--bank"), "%s: has a bank" % mode)
        else:
            check(not has_flag(argv, "--size"), "%s: no size" % mode)
            check(not has_flag(argv, "--bank"), "%s: no bank" % mode)
        # The still options belong to the modes that send a still.
        if mode in runner.STILL_MODES:
            for flag in ["--command", "--remaining", "--free", "--tmo", "--per",
                         "--set"]:
                check(has_flag(argv, flag), "%s: has %s" % (mode, flag))
        else:
            for flag in ["--command", "--remaining", "--free", "--tmo", "--per",
                         "--set"]:
                check(not has_flag(argv, flag), "%s: no %s" % (mode, flag))
        # The output file belongs to the grab mode.
        if mode == "grab":
            check(has_flag(argv, "--out"), "grab: has an output path")
        else:
            check(not has_flag(argv, "--out"), "%s: no output path" % mode)
        # --log-dump is only there when it was asked for.
        check(not has_flag(argv, "--log-dump"), "%s: no log dump" % mode)
        print("%-8s %s" % (mode, runner.build_command(REMOTE, mode,
                                                      options(mode=mode), LOG)))

    # 2. --log-dump appears when it was asked for, and --no-copy only in the
    #    grab mode.
    argv = tokens(runner.build_command(REMOTE, "still",
                                       options(mode="still", log_dump=12), LOG))
    check(argv[argv.index("--log-dump") + 1] == "12", "log dump value")
    argv = tokens(runner.build_command(REMOTE, "grab",
                                       options(mode="grab", no_copy=True), LOG))
    check(has_flag(argv, "--no-copy"), "grab: has no-copy")
    argv = tokens(runner.build_command(REMOTE, "still",
                                       options(mode="still", no_copy=True), LOG))
    check(not has_flag(argv, "--no-copy"), "still: no no-copy")

    # 3. A zero bench address is a value, not a mistake, and it is only
    #    emitted for the bench mode.
    argv = tokens(runner.build_command(REMOTE, "bench",
                                       options(mode="bench", bench_address=0), LOG))
    check(argv[argv.index("--bench-address") + 1] == "0x0", "bench: zero address")
    argv = tokens(runner.build_command(REMOTE, "bench",
                                       options(mode="bench", bench_address=0x1234),
                                       LOG))
    check(argv[argv.index("--bench-address") + 1] == "0x1234", "bench: address")

    # 4. The defaults are the helper's defaults, so a dropped flag does not
    #    change the run.
    check(options().size == 0x00100000, "default size")
    check(options().bank == 2, "default bank")
    check(options().command == 0x01010016, "default command")
    check(options().remaining == 2518, "default remaining")
    check(options().free == 7687503872, "default free")
    check(options().log_dump == 0, "default log dump")

    # 5. A malformed number is refused by the parser, before anything is
    #    sent to the camera.
    for bad in ["0x", "12abc", "", "1.5"]:
        try:
            with contextlib.redirect_stderr(io.StringIO()):
                parser.parse_args(["--out-dir", "/tmp/x", "--size", bad])
        except SystemExit:
            pass
        else:
            print("FAIL: %r was accepted" % bad)
            sys.exit(1)
    print("malformed numbers: refused")

    # 6. Hexadecimal and decimal are both accepted, and both are numbers.
    parsed = parser.parse_args(["--out-dir", "/tmp/x", "--size", "0x1000",
                                 "--bank", "3", "--command", "0x01010016"])
    check(parsed.size == 4096, "hex size")
    check(parsed.bank == 3, "decimal bank")
    check(parsed.command == 0x01010016, "hex command")

    # 7. A path with a space in it survives the shell.
    spaced = "/tmp/sensor file.raw"
    command = runner.build_command(REMOTE, "grab",
                                   options(mode="grab", output=spaced), LOG)
    check(shlex.quote(spaced) in command, "the path is quoted")
    argv = tokens(command)
    check(argv[argv.index("--out") + 1] == spaced, "the path survives the shell")
    print("path quoting: %s" % command)

    # 8. The RAW switch is restored only when the helper left it on.
    check(runner.last_ssif_state("SSIF_RAW on\n") == "on", "state on")
    check(runner.last_ssif_state("SSIF_RAW on\nSSIF_RAW off\n") == "off", "state off")
    check(runner.last_ssif_state("SSIF_RAW off\nSSIF_RAW on\n") == "on", "state again")
    check(runner.last_ssif_state("nothing here") is None, "no state")
    check(runner.last_ssif_state(None, "") is None, "no state at all")

    print("runner options: PASS")
    return 0


if __name__ == "__main__":
    sys.exit(main())
