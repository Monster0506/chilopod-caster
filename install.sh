#!/bin/sh
set -e

DEST_DIR="${DEST_DIR:-/usr/local/sbin}"
CONF_DIR="${CONF_DIR:-/usr/local/etc/chilopod}"
LOG_DIR="${LOG_DIR:-/var/log/chilopod}"

cd "$(dirname "$0")"

if [ "$(id -u)" -ne 0 ]; then
	echo "install.sh must run as root." >&2
	exit 1
fi

if ! id caster >/dev/null 2>&1; then
	useradd --system --no-create-home --shell /usr/sbin/nologin caster
fi

if [ ! -e rtcm-go/cmd/sidecar ] || [ ! -e ruckus/main.go ]; then
	git submodule update --init --recursive
fi

mkdir -p "$DEST_DIR"

(cd caster && make clean all install DEST_DIR="$DEST_DIR/")
(cd rtcm-go && go build -o "$DEST_DIR/sidecar" ./cmd/sidecar)
(cd ruckus && go build -o "$DEST_DIR/ruckus" .)
install -m 0755 sample-config/chilopod-run.sh "$DEST_DIR/chilopod-run.sh"

mkdir -p "$CONF_DIR" "$LOG_DIR" "$CONF_DIR/ui"
chown caster "$LOG_DIR"

cp -r ui/dist/. "$CONF_DIR/ui/"

for f in caster.yaml source.auth host.auth sourcetable.dat blocklist; do
	if [ -e "$CONF_DIR/$f" ]; then
		echo "Skipping $f: $CONF_DIR/$f already exists."
	else
		cp "sample-config/$f" "$CONF_DIR/$f"
	fi
done

echo "Installed. Edit $CONF_DIR/caster.yaml and $CONF_DIR/source.auth, then run $DEST_DIR/chilopod-run.sh directly."
