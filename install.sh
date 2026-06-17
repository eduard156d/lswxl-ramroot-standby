#!/bin/sh
set -eu

PKG_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
DEFAULT_CONFIG=$PKG_DIR/files/etc/default/lswxl-ramroot-standby
CONFIG=/etc/default/lswxl-ramroot-standby
CONFIG_EXAMPLE=/etc/default/lswxl-ramroot-standby.example

INSTALL_APT_DEPS=${INSTALL_APT_DEPS:-1}
RUN_CHECK=${RUN_CHECK:-1}
CC=${CC:-cc}

log()
{
	echo "install: $*"
}

have_cmd()
{
	command -v "$1" >/dev/null 2>&1
}

[ "$(id -u)" = 0 ] || {
	echo "ERROR: run as root" >&2
	exit 1
}

install_apt_deps()
{
	[ "$INSTALL_APT_DEPS" = 1 ] || {
		log "apt dependency installation disabled"
		return 0
	}
	have_cmd apt-get || {
		log "apt-get not found; skipping dependency installation"
		return 0
	}

	log "installing runtime/build dependencies with apt-get"
	export DEBIAN_FRONTEND=noninteractive
	apt-get update
	apt-get install -y --no-install-recommends \
		busybox \
		gcc \
		libc6-dev \
		iproute2 \
		procps \
		util-linux \
		mdadm \
		hdparm
}

build_wait_helper()
{
	src=$PKG_DIR/src/lswxl-wait-wake-switch.c
	out=/usr/local/sbin/lswxl-wait-wake-switch

	if [ ! -r "$src" ]; then
		log "missing source: $src"
		return 1
	fi

	if ! have_cmd "$CC"; then
		echo "ERROR: C compiler '$CC' not found; install gcc or set CC=/path/to/compiler" >&2
		return 1
	fi

	log "building $out"
	"$CC" -O2 -Wall -Wextra -o "$out" "$src"
	chmod 755 "$out"
}

merge_missing_config_keys()
{
	[ -e "$CONFIG" ] || return 0

	tmp=$(mktemp)
	awk '
		FNR == NR {
			if ($0 ~ /^[A-Za-z_][A-Za-z0-9_]*=/) {
				key = $0
				sub(/=.*/, "", key)
				defaults[key] = $0
				order[++n] = key
			}
			next
		}
		{
			print
			if ($0 ~ /^[A-Za-z_][A-Za-z0-9_]*=/) {
				key = $0
				sub(/=.*/, "", key)
				seen[key] = 1
			}
		}
		END {
			first = 1
			for (i = 1; i <= n; i++) {
				key = order[i]
				if (!(key in seen)) {
					if (first) {
						print ""
						print "# Added by lswxl-ramroot-standby installer; see"
						print "# /etc/default/lswxl-ramroot-standby.example for documentation."
						first = 0
					}
					print defaults[key]
				}
			}
		}
	' "$DEFAULT_CONFIG" "$CONFIG" > "$tmp"

	if ! cmp -s "$tmp" "$CONFIG"; then
		backup=$CONFIG.bak.$(date +%Y%m%d%H%M%S)
		cp -p "$CONFIG" "$backup"
		install -m 644 "$tmp" "$CONFIG"
		log "updated $CONFIG with missing keys; backup: $backup"
	fi
	rm -f "$tmp"
}

install_apt_deps

install -d -m 755 /usr/local/sbin
install -m 755 \
	"$PKG_DIR/files/usr/local/sbin/lswxl-ramroot-standby" \
	/usr/local/sbin/lswxl-ramroot-standby

build_wait_helper

install -d -m 755 /etc/default
install -m 644 \
	"$DEFAULT_CONFIG" \
	"$CONFIG_EXAMPLE"
log "installed $CONFIG_EXAMPLE"

if [ ! -e "$CONFIG" ]; then
	install -m 644 \
		"$DEFAULT_CONFIG" \
		"$CONFIG"
	log "installed $CONFIG"
else
	merge_missing_config_keys
	log "kept existing $CONFIG values"
fi

log "installed /usr/local/sbin/lswxl-ramroot-standby"
log "installed /usr/local/sbin/lswxl-wait-wake-switch"

if [ "$RUN_CHECK" = 1 ]; then
	log "running non-destructive check"
	/usr/local/sbin/lswxl-ramroot-standby check
fi
