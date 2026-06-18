# LS-WXL RAM-Root Standby

Experimental third standby approach for Buffalo LinkStation LS-WXL/LS-WSXL
running Debian.

> [!WARNING]
> This project controls late shutdown, disks, fans, LEDs, GPIOs, and board
> power rails on old Buffalo NAS hardware. A wrong configuration can make the
> system fail to boot, cut power to the wrong device, or risk data loss. Test
> only with verified backups and physical access to the machine.

This package implements the current RAM-root/exitrd standby approach. It starts
from the original Buffalo firmware design:

1. let Debian/systemd reach a very late shutdown state
2. create a tiny RAM root filesystem
3. switch into that RAM root
4. release the real root filesystem and disks
5. stop fan/HDD/USB hardware as needed
6. wait for a Wake-on-LAN magic packet
7. continue to poweroff, letting AUTO mode boot the NAS cleanly

The key design goal is that the internal HDD is not powered down while it is
still the active root filesystem.

## Related Projects

This project is intended for Buffalo LS-WXL/LS-WSXL systems running Debian. The
test system used during development was installed using the excellent
Debian_on_Buffalo project:

https://github.com/1000001101000/Debian_on_Buffalo

This standby project is separate from Debian_on_Buffalo. It is meant as an
optional add-on for users who already have Debian running on compatible Buffalo
hardware and want to experiment with late-shutdown standby behavior.

For normal booted-system hardware monitoring, fan control, LED handling, and
physical switch handling, see the related `lswxl-hw-monitor` project:

https://github.com/eduard156d/lswxl-hw-monitor

The two projects intentionally stay separate:

- `lswxl-hw-monitor` runs during normal Debian operation.
- `lswxl-ramroot-standby` runs only during the late shutdown/RAM-root standby
  phase.

## Current Status

Experimental dry-run, hardware-inspection, and Wake-on-LAN RAM-root standby
phase.

Installed command:

```sh
/usr/local/sbin/lswxl-ramroot-standby
```

## Installation

Clone the repository on the target Buffalo system and run the installer as
root:

```sh
git clone https://github.com/eduard156d/lswxl-ramroot-standby.git
cd lswxl-ramroot-standby
./install.sh
```

The installer:

- installs runtime/build dependencies with `apt-get` when available
- installs `/usr/local/sbin/lswxl-ramroot-standby`
- builds and installs `/usr/local/sbin/lswxl-wait-wake-switch` from local C
  source
- installs `/etc/default/lswxl-ramroot-standby.example`
- creates `/etc/default/lswxl-ramroot-standby` when it does not exist
- keeps existing config values and appends only missing config keys on upgrades
- runs a non-destructive `lswxl-ramroot-standby check`

Useful installer options:

```sh
# Do not run apt-get.
INSTALL_APT_DEPS=0 ./install.sh

# Do not run the final check.
RUN_CHECK=0 ./install.sh

# Use a specific compiler.
CC=gcc ./install.sh
```

After installation, review `/etc/default/lswxl-ramroot-standby` carefully before
enabling power rails or running a real standby test.

The main command is installed below `/usr/local/sbin` and must be run with root
privileges. On many Debian systems, `/usr/local/sbin` is not in a normal user's
`PATH`, so use the full path and either `sudo` or a root shell.

Examples:

```sh
sudo /usr/local/sbin/lswxl-ramroot-standby status

# Or open a root shell first:
su -
/usr/local/sbin/lswxl-ramroot-standby status
```

Supported commands:

```sh
/usr/local/sbin/lswxl-ramroot-standby check
/usr/local/sbin/lswxl-ramroot-standby status
/usr/local/sbin/lswxl-ramroot-standby build-root
/usr/local/sbin/lswxl-ramroot-standby test-root
/usr/local/sbin/lswxl-ramroot-standby test-pivot
/usr/local/sbin/lswxl-ramroot-standby test-release-oldroot
/usr/local/sbin/lswxl-ramroot-standby test-hardware
/usr/local/sbin/lswxl-ramroot-standby prepare-standby
/usr/local/sbin/lswxl-ramroot-standby enter-standby
/usr/local/sbin/lswxl-ramroot-standby clean
```

Command overview:

| Command | What it does | When to use it |
| --- | --- | --- |
| `/usr/local/sbin/lswxl-ramroot-standby check` | Checks whether the installation is complete enough to run: required programs, helper binaries, configuration file, important paths, and basic hardware-control files. It does not build a new RAM root, does not install a shutdown hook, and does not enter standby. | Run this after installation or after changing the configuration. It is the safest first sanity check. |
| `/usr/local/sbin/lswxl-ramroot-standby status` | Prints the currently loaded configuration and detected runtime state, for example switch handling, wake ports, storage mode, fan/LED paths, log device settings, and configured power rails. It is read-only. | Use this when you want to see what the standby script would currently use before running a test. |
| `/usr/local/sbin/lswxl-ramroot-standby build-root` | Builds the temporary RAM-root tree below `/run/lswxl-ramroot-standby`. This copies the needed shell, BusyBox tools, helper binary, configuration, and scripts into a tiny filesystem that can run after the normal root filesystem is released. | Use this to prepare or inspect the RAM-root content without shutting down. This is also done automatically by the real standby path. |
| `/usr/local/sbin/lswxl-ramroot-standby test-root` | Builds/checks the RAM-root and verifies that the expected files and helper binaries are present and executable. It does not pivot into the RAM root. | Use this after code changes or installation changes to confirm that the generated RAM-root is internally complete. |
| `/usr/local/sbin/lswxl-ramroot-standby test-pivot` | Tests whether the script can enter the RAM-root environment using a private mount namespace. The running system root is not replaced globally. | Use this during development to validate the RAM-root handoff mechanics before trying a real shutdown/standby cycle. |
| `/usr/local/sbin/lswxl-ramroot-standby test-release-oldroot` | Tests the logic that would later release the old root filesystem from inside the RAM-root phase. It runs in a private mount namespace where applicable, so it does not detach the real running root of the live system. | Use this when changing mount, pivot, oldroot, or release logic. It helps catch obvious problems before a real standby run. |
| `/usr/local/sbin/lswxl-ramroot-standby test-hardware` | Probes and exercises visible hardware-control paths such as fan, LEDs, switch input, storage detection, and configured regulator/power-rail mappings. It does not start the real standby wait. | Use this after changing hardware-related config such as fan polarity, LED paths, storage selection, or rail mappings. Because it touches hardware controls, run it only with physical access. |
| `/usr/local/sbin/lswxl-ramroot-standby prepare-standby` | Builds the RAM root and installs the systemd exitrd handoff at `/run/initramfs/shutdown`. Nothing shuts down immediately. The standby code will run during the next normal shutdown. | Use this when another command or service should trigger the actual shutdown later. This is a two-step variant: prepare first, then shut down separately. |
| `/usr/local/sbin/lswxl-ramroot-standby enter-standby` | Builds the RAM root, installs the systemd exitrd handoff, and immediately starts a normal shutdown. During late shutdown, systemd hands over to the RAM-root standby logic, which releases storage, handles fan/LED/rails, waits for Wake-on-LAN or switch change, and then continues to final poweroff. | Use this for a real standby test or normal standby entry. This is the main operational command. |
| `/usr/local/sbin/lswxl-ramroot-standby clean` | Removes generated runtime files below `/run`, including the generated RAM-root tree and the prepared `/run/initramfs/shutdown` handoff if present. It does not uninstall the package. | Use this to reset a prepared-but-not-yet-used standby setup, or after development tests when you want to remove temporary runtime state. |

The `test-*` commands use a private mount namespace where applicable and do
not replace the running system root. `prepare-standby` and `enter-standby` are
the commands that install the real shutdown handoff; `enter-standby` also
starts the shutdown immediately.

For real standby waits, use `enter-standby`. It prepares
`/run/initramfs/shutdown`, which is systemd's supported late shutdown handoff
mechanism. The standby wait uses `WAKE_TIMEOUT`: `0` means wait indefinitely,
non-zero values are test timeouts in seconds.

Verified on one Buffalo LS-WXL test system running Debian GNU/Linux 12
(bookworm), kernel `6.1.174`, architecture `armv5tel`:

- `WAKE_TIMEOUT=300` times out cleanly when no packet is sent.
- Sending a UDP magic packet to ports `9`/`2304` wakes the exitrd wait and logs
  `wake received rc=0`.
- AVM FRITZ!Box "Start Computer" Wake-on-LAN sends a raw Ethernet WoL frame
  with EtherType `0x0842`; the wake helper listens for that form as well.

## Optional USB Logging

The RAM-root logger can append diagnostic messages to an external block device.
This is useful because journald is usually already stopped during
`systemd-shutdown`.

Configure `/etc/default/lswxl-ramroot-standby`:

```sh
LOG_DEVICE=/dev/disk/by-uuid/YOUR-USB-LOG-UUID
LOG_FSTYPE=auto
LOG_FILE=lswxl-ramroot-standby.log
STANDBY_LOG_DEVICE_WAIT_SEC=10
STANDBY_DIAGNOSTICS=auto
WAKE_TIMEOUT=300
STANDBY_ALLOWED_SWITCH=AUTO
STANDBY_SWITCH_POLL_SEC=2
STANDBY_FAN_COOLDOWN_SEC=120
```

Leave `LOG_DEVICE` empty to disable external logging. The logger mounts the
device only while it writes and continues silently if the device is missing.
`STANDBY_DIAGNOSTICS` controls expensive diagnostic-only probes such as full
mount lists, oldroot block snapshots, storage state dumps, and process
reference scans. The default `auto` runs those probes only when `LOG_DEVICE` is
configured and currently available. Set it to `1` to force diagnostics or `0`
to skip them. Safety-critical actions such as sync, oldroot unmount attempts,
storage settle, mdadm, hdparm, rail control, fan control, and wake handling are
not disabled by this setting.
When `USB` is also listed in `STANDBY_POWER_RAILS`, the helper closes and
unmounts the log before USB poweroff. After USB power restore it waits up to
`STANDBY_LOG_DEVICE_WAIT_SEC` seconds for `LOG_DEVICE` to reappear. This wait
is skipped completely when `LOG_DEVICE` is empty. The rail order is fixed:
HDD/SATA rails are switched before the USB rail, both when powering off and
when restoring power. If USB was powered off, HDD rail restore messages cannot
be written to the USB log; keep USB out of `STANDBY_POWER_RAILS` for diagnostic
runs that need those messages. If `/dev/disk/by-uuid/...` is not recreated in
the RAM-root environment, the helper uses `blkid` to scan visible block devices
for the configured UUID.

`enter-standby` checks the physical switch before preparing the exitrd. By
default standby is allowed only when the switch is in `AUTO`. During standby,
the exitrd wait checks the switch every `STANDBY_SWITCH_POLL_SEC` seconds. A
change to `ON` or `OFF` leaves standby and hands control to the final poweroff
path; with the physical switch at `ON` the box is expected to boot again, while
`OFF` should leave it off.

## FAN Cooldown

The fan does not have to stop at the exact moment standby starts. Configure
`STANDBY_FAN_COOLDOWN_SEC` to keep it running for a short cooldown period after
storage handling and rail poweroff have completed:

```sh
# Package default.
STANDBY_FAN_COOLDOWN_SEC=120

# Shorter hardware test.
STANDBY_FAN_COOLDOWN_SEC=60

# Stop the fan immediately when the standby wait starts.
STANDBY_FAN_COOLDOWN_SEC=0
```

The cooldown is part of the wake wait loop. It does not block wake: a magic
packet or physical switch change leaves standby immediately even while the fan
is still in its cooldown window.

During standby entry, while storage and rails are still being prepared, the
helper sets `linkstation:amber:info` to a fast `250ms/250ms` blink pattern.
During the actual standby wait it switches to a slower `1000ms/3000ms` pattern.
The fast pattern overlaps visually with `lsmonitor` busy/wake, but `lsmonitor`
is already out of the way during the late shutdown/RAM-root phase.

## Storage And Power Rails

The standby helper has two separate storage-related steps:

1. Select block devices and put disks into ATA standby.
2. Optionally switch board power rails off while waiting for wake.

`STANDBY_STORAGE_DISKS` controls step 1.

```sh
# Auto-detect all visible SATA disks at standby runtime.
STANDBY_STORAGE_DISKS=

# Or force an explicit list.
STANDBY_STORAGE_DISKS="/dev/sda /dev/sdb"
```

When the variable is empty, the RAM-root helper scans `/sys/block/sd*/device`
and selects only SATA/ATA disks. USB disks are skipped, which allows a USB log
stick to remain online.

`STANDBY_POWER_RAILS` controls step 2.

```sh
# Disabled by default.
STANDBY_POWER_RAILS=

# Tested LS-WXL example.
STANDBY_POWER_RAILS="HDD0 HDD1"

# Productive target after USB rail validation.
STANDBY_POWER_RAILS="HDD0 HDD1 USB"
```

Power rails are board-specific. Do not enable them blindly on another model.
The order in `STANDBY_POWER_RAILS` is not used as execution order: the helper
always handles HDD/SATA rails before USB to keep test and production behavior
consistent.
The short names are mapped through configurable variables:

```sh
STANDBY_POWER_RAIL_HDD0_DEV=regulators:regulator@2
STANDBY_POWER_RAIL_HDD0_GPIO=28
STANDBY_POWER_RAIL_HDD1_DEV=regulators:regulator@3
STANDBY_POWER_RAIL_HDD1_GPIO=29
STANDBY_POWER_RAIL_USB_DEV=regulators:regulator@1
STANDBY_POWER_RAIL_USB_GPIO=37
```

On the tested LS-WXL, this mapping was discovered from the local Device Tree
and sysfs:

```sh
for r in /sys/class/regulator/regulator.*; do
    echo "== $r =="
    cat "$r/name" 2>/dev/null
    cat "$r/state" 2>/dev/null
done

mount -t debugfs debugfs /sys/kernel/debug 2>/dev/null || true
cat /sys/kernel/debug/gpio | grep 'regulators:regulator'
```

Expected LS-WXL-style output:

```text
USB Power
HDD0 Power
HDD1 Power

gpio-28  (regulators:regulator) out hi
gpio-29  (regulators:regulator) out hi
gpio-37  (regulators:regulator) out hi
```

The Device Tree names can also be inspected directly:

```sh
find /proc/device-tree/regulators -maxdepth 2 -type f -print |
while read f; do
    printf '%s=' "$f"
    tr -d '\000' < "$f" 2>/dev/null || od -An -tx1 -v "$f"
    echo
done
```

The helper switches a rail by temporarily unbinding the `reg-fixed-voltage`
platform device, exporting the GPIO, setting it low for standby, setting it
high on wake, and binding the regulator again. This has been verified on one
LS-WXL test system for USB, HDD0, and HDD1.

## Acknowledgements

This project was developed and tested on a Buffalo LS-WXL running Debian
GNU/Linux 12 (bookworm), installed with guidance from
[1000001101000/Debian_on_Buffalo](https://github.com/1000001101000/Debian_on_Buffalo).
No code from that repository is included here.

## License

This project is licensed under the MIT License. See [LICENSE](LICENSE).
