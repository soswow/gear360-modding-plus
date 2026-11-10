#!/bin/sh
# Host tests for g360ctl: fake LED sysfs tree, recorded input events, fake player.
set -eu
here=$(CDPATH= cd -- "$(dirname "$0")/.." && pwd)
CC=${CC:-cc} python3 "$here/tests/test_g360ctl.py"
