#!/bin/sh
set -eu

PKG_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)

[ "$(id -u)" = 0 ] || {
	echo "ERROR: run as root" >&2
	exit 1
}

install -m 755 \
	"$PKG_DIR/files/usr/local/sbin/lswxl-ramroot-standby" \
	/usr/local/sbin/lswxl-ramroot-standby

install -d -m 755 /etc/default
install -m 644 \
	"$PKG_DIR/files/etc/default/lswxl-ramroot-standby" \
	/etc/default/lswxl-ramroot-standby.example
echo "installed /etc/default/lswxl-ramroot-standby.example"

if [ ! -e /etc/default/lswxl-ramroot-standby ]; then
	install -m 644 \
		"$PKG_DIR/files/etc/default/lswxl-ramroot-standby" \
		/etc/default/lswxl-ramroot-standby
	echo "installed /etc/default/lswxl-ramroot-standby"
fi

echo "installed /usr/local/sbin/lswxl-ramroot-standby"
