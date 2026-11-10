#!/usr/bin/env python3
"""Cleanup: restore the vendor enablement symlink and remove the corrupted
rollback entries left by the no-app-boot attempt."""
import sys
sys.path.insert(0, '.')
import run_load_check as rlc

cam = rlc.HttpFtpCamera(host='192.168.43.1')

step = (
    "mount -o remount,rw / ; "
    "ln -s ../di-camera-app.service /usr/lib/systemd/system/default.target.wants/di-camera-app.service 2>&1 ; echo VENDOR_LN_RC=$? ; "
    "ln -s ../di-camera-app.service /etc/systemd/system/default.target.wants/di-camera-app.service 2>&1 ; echo ETC_LN_RC=$? ; "
    "rm -f /etc/systemd/system/gear360-rollback.service /etc/systemd/system/default.target.wants/gear360-rollback.service 2>&1 ; echo RM_RC=$? ; "
    "systemctl daemon-reload ; "
    "echo VENDOR: ; ls -la /usr/lib/systemd/system/default.target.wants/ | grep camera ; "
    "echo ETC: ; ls -la /etc/systemd/system/default.target.wants/ ; "
    "echo APP-STATE=$(systemctl is-enabled di-camera-app.service 2>&1) ; "
    "echo DONE"
)
print(cam.execute(step, timeout=30.0))
