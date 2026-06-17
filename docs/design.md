# Design

## Why RAM Root

The original Buffalo firmware uses `pwrmgr`, which contains strings for
`pivot_root`, `chroot`, `create ram-rootfs`, and
`/etc/pwrmgr/standby.inittab`. This strongly indicates that standby is entered
from a RAM-root environment rather than from the normal root filesystem.

That matters because the internal HDD cannot reliably remain in standby while
it still backs `/`. Even a late `systemd-shutdown` hook can leave ext4, md, and
kernel writeback threads alive enough to wake the disk again.

## Intended Flow

```text
Debian running
  |
  v
standby command requests systemd poweroff
  |
  v
prepare /run/initramfs/shutdown while Debian is still running
  |
  v
systemd reaches late shutdown and switches into exitrd
  |
  v
RAM root has /proc, /sys, /dev, wake helper, busybox/tools
  |
  v
pivot_root/chroot into RAM root
  |
  v
old root, md, SATA, USB can be released safely
  |
  v
HDD/USB off, LED standby indication, fan cooldown starts
  |
  v
wait for UDP Wake-on-LAN magic packet or switch change
  |
  v
fan stops after cooldown, unless standby was already left
  |
  v
final poweroff/reboot path, AUTO mode performs clean boot
```

## Safety Rules

- No hard HDD poweroff while `/` is still the active root.
- Keep the first implementation in dry-run mode.
- Keep all runtime state in `/run`.
- Use `/run/initramfs/shutdown` for long waits. The older
  `/lib/systemd/system-shutdown` hook experiment was removed because systemd
  applies a limited execution window there.
- Do not reuse Buffalo `pwrmgr` directly until its dependencies and kernel
  interface assumptions are understood.

## FAN Cooldown

The fan cooldown is part of the RAM-root wait loop. After storage handling and
optional rail poweroff, the info LED starts blinking and the fan is left running
for `STANDBY_FAN_COOLDOWN_SEC` seconds. The package default is 120 seconds; a
test setup can lower it to 60 seconds, and `0` stops the fan immediately.

The cooldown must not run as a blocking sleep before wake handling. Instead, the
wait loop uses short wake-helper time slices until the cooldown expires. This
keeps magic-packet wake and physical switch wake responsive while the disks and
case are still being cooled.

The standby indicator uses `linkstation:amber:info` in two phases. While the
helper is still entering standby and preparing storage/rails, it uses a fast
`250ms/250ms` blink pattern. Once the actual wake wait begins, it switches to a
slower `1000ms/3000ms` pattern. `lsmonitor` also uses `amber:info` for fast
busy/wake indication, but `lsmonitor` is no longer active during this late
shutdown/RAM-root phase.

## Storage Selection

Disk standby and rail poweroff are intentionally separate operations.

`STANDBY_STORAGE_DISKS` selects the block devices that should be flushed and
sent into ATA standby with `hdparm -y`. When the variable is empty, the helper
detects visible SATA/ATA disks at standby runtime by inspecting
`/sys/block/sd*/device`. This is the recommended default because disk names can
change when the installation is moved to another box or when an additional disk
is added later.

USB disks are not selected by the automatic SATA scan. This keeps a USB logging
stick available while standby diagnostics are being written. If a deployment
really needs to send a USB disk into standby, configure an explicit list instead.

Examples:

```sh
# Runtime auto-detection of all visible SATA disks.
STANDBY_STORAGE_DISKS=

# Fixed override for a known two-disk system.
STANDBY_STORAGE_DISKS="/dev/sda /dev/sdb"
```

`STANDBY_STORAGE_MODE` controls what is done with the selected disks:

```sh
# Only log block, mount, and md state.
STANDBY_STORAGE_MODE=log

# Flush selected disks and issue ATA standby.
STANDBY_STORAGE_MODE=standby

# Try to stop md arrays first, then flush and issue ATA standby.
STANDBY_STORAGE_MODE=md-stop-standby
```

On the current Debian LS-WXL test system, `mdadm --stop` can still fail because
late ext4/md kernel threads keep references to the array. This is logged and
treated as best-effort; the subsequent disk standby and rail handling still run.

## Power Rails

`STANDBY_POWER_RAILS` controls board power rails that should be switched off
while the RAM-root helper waits for wake. The default is empty because rail
names, platform device names, and GPIO numbers are board-specific. Enabling a
wrong rail can remove power from hardware that the standby helper still needs.

Tested LS-WXL example:

```sh
STANDBY_POWER_RAILS="HDD0 HDD1"
```

Keep `USB` out of `STANDBY_POWER_RAILS` while USB logging is enabled. Switching
off the USB rail also removes the log device during the wait phase. This is
allowed, but it creates a diagnostic gap: the helper can log until just before
USB poweroff and again after USB power restore.

When `LOG_DEVICE` is set and `USB` is part of `STANDBY_POWER_RAILS`, the helper
syncs and unmounts `/log` before switching USB off. File logging is then
suspended so later status messages do not remount the USB stick immediately
before poweroff. After USB power is restored, the helper waits up to
`STANDBY_LOG_DEVICE_WAIT_SEC` seconds for `LOG_DEVICE` to reappear. That probe
is skipped when `LOG_DEVICE` is empty, so productive configurations without USB
logging do not pay an extra delay.

Rail execution order is deliberately fixed and does not follow the textual
order in `STANDBY_POWER_RAILS`: HDD/SATA rails are switched before the USB rail,
both during poweroff and restore. This keeps production and diagnostic behavior
consistent. When USB logging is needed for HDD rail restore messages, keep USB
out of `STANDBY_POWER_RAILS` for that diagnostic run.

The helper does not rely on the restored USB stick receiving the same
`/dev/sdX` name. If the configured `/dev/disk/by-uuid/...` symlink is missing
because udev is not running in RAM-root, it scans visible block devices with
`blkid` and mounts the one with the configured UUID.

`STANDBY_DIAGNOSTICS` separates diagnostic probes from standby control logic.
With the default `auto`, expensive dumps such as full mount listings, oldroot
block state, storage snapshots, and process reference scans are run only when
`LOG_DEVICE` is configured and available. Productive configurations without a
file log therefore avoid those probes, while still running sync, oldroot
unmount attempts, storage settle, mdadm, hdparm, rail control, fan control, and
wake handling.

The short names in `STANDBY_POWER_RAILS` are mapped through explicit variables:

```sh
STANDBY_POWER_RAIL_HDD0_DEV=regulators:regulator@2
STANDBY_POWER_RAIL_HDD0_GPIO=28
STANDBY_POWER_RAIL_HDD1_DEV=regulators:regulator@3
STANDBY_POWER_RAIL_HDD1_GPIO=29
STANDBY_POWER_RAIL_USB_DEV=regulators:regulator@1
STANDBY_POWER_RAIL_USB_GPIO=37
```

These values were discovered on the test LS-WXL from the local Device Tree and
sysfs, not from a generic Linux convention. A user porting this package should
verify the mapping before enabling rail control:

```sh
for r in /sys/class/regulator/regulator.*; do
    echo "== $r =="
    cat "$r/name" 2>/dev/null
    cat "$r/state" 2>/dev/null
done

mount -t debugfs debugfs /sys/kernel/debug 2>/dev/null || true
cat /sys/kernel/debug/gpio | grep 'regulators:regulator'
```

Expected LS-WXL relationship:

```text
USB Power  -> regulators:regulator@1 -> GPIO37
HDD0 Power -> regulators:regulator@2 -> GPIO28
HDD1 Power -> regulators:regulator@3 -> GPIO29
```

The helper switches a rail by unbinding the matching `reg-fixed-voltage`
platform device, exporting the GPIO, setting it low during standby, setting it
high after wake, and binding the platform device again. After rails are restored
the helper waits `STANDBY_POWER_RESTORE_DELAY_SEC` seconds before handing
control back to the final systemd poweroff path, so disks have time to spin up.
