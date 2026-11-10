#!/usr/bin/env python3
"""Install the Gear360 app-rollback unit and disable the app's boot enablement."""
import sys
sys.path.insert(0, '.')
import run_load_check as rlc

cam = rlc.HttpFtpCamera(host='192.168.43.1')

UNIT = (
    "[Unit]\\n"
    "Description=Gear360 automatic app re-enable rollback\\n"
    "[Service]\\n"
    "Type=oneshot\\n"
    "TimeoutStartSec=900\\n"
    "ExecStart=/bin/sh -c \\\"sleep 600; "
    "systemctl enable di-camera-app.service; "
    "systemctl start di-camera-app.service\\\"\\n"
    "[Install]\\n"
    "WantedBy=default.target\\n"
)

step1 = (
    "printf '" + UNIT + "' > /etc/systemd/system/gear360-rollback.service ; "
    "echo WRITE_RC=$? ; "
    "cat /etc/systemd/system/gear360-rollback.service"
)
print(cam.execute(step1, timeout=30.0))
