# No-app-boot procedure — 2 November 2026

The camera's rootfs is a read-only ext4 (`mount -o remount,rw /` opens it
temporarily; the writes persist via the flash, the next boot mounts ro
again). The app's boot enablement was two symlinks: the vendor's
`/usr/lib/systemd/system/default.target.wants/di-camera-app.service` and an
`/etc` copy; `systemctl disable` removes only the /etc one, so the vendor's
symlink was removed directly.

Installed state (persistent):

- `/etc/systemd/system/gear360-rollback.service` — the one-shot rollback:
  after 600 s it remounts the rootfs rw, `systemctl enable`s and `start`s
  the app, and exits. Enabled via
  `/etc/systemd/system/default.target.wants/gear360-rollback.service`.
- `di-camera-app.service` — `is-enabled` = disabled; both wants symlinks
  removed. The app will not start at boot.

The boot without the app: the network (`net-config.service`, only
`Requires=dbus.socket`) and the FTP/CGI stay up. Within the 10-minute
window we connect and cancel the rollback
(`systemctl stop gear360-rollback.service` +
`systemctl disable gear360-rollback.service`); if the network were down,
the rollback restores the app by itself.

Revert (when done): `mount -o remount,rw /` then
`systemctl enable di-camera-app.service` (and disable the rollback), then
reboot. The helper scripts are `install_rollback.py`,
`enable_rollback.py`, `remove_vendor_symlink.py`, `fix_rollback_unit.py`
and `verify_enablement.py` in this directory.

## Outcome — the approach does not work on this camera

The reboot showed the app running anyway. Two findings:

1. **The systemd enablement is not the real lever.** The Tizen `deviced`
   (the app-manager) launches `di-camera-app` as the boot app at every
   boot; the app's process is systemd's child but started through the
   deviced's launch, not through any `WantedBy` symlink. Removing both
   enablement symlinks changed nothing.
2. **The rootfs flash refuses writes.** The `remount,rw` makes the mount
   look writable and metadata operations (link/unlink) persist, but file
   content writes fail with I/O errors. The rollback unit and its symlink
   were left as corrupted entries that even `rm` cannot remove; they are
   harmless (systemd skips them) and may be swept by a future boot-time
   e2fsck.

Cleanup (`cleanup_no_app_boot.py`) restored both enablement symlinks
(`is-enabled` = enabled again); the corrupted rollback entries remain and
are inert. A persistent no-app boot would have to go through the deviced's
boot-app configuration (the writable `/opt` partition), not systemd.

