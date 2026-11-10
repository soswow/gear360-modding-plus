#!/usr/bin/env python3
"""Verify the enablement state of the app and the rollback."""
import sys
sys.path.insert(0, '.')
import run_load_check as rlc

cam = rlc.HttpFtpCamera(host='192.168.43.1')

step = (
    "echo APP-STATE=$(systemctl is-enabled di-camera-app.service 2>&1) ; "
    "echo RB-STATE=$(systemctl is-enabled gear360-rollback.service 2>&1) ; "
    "echo ETC-WANTS: ; ls -la /etc/systemd/system/default.target.wants/ | grep camera ; "
    "echo USRLIB-WANTS: ; ls -la /usr/lib/systemd/system/default.target.wants/ | grep camera ; "
    "echo DONE"
)
print(cam.execute(step, timeout=30.0))
