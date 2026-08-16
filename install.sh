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

if command -v apt-get >/dev/null 2>&1; then
	apt-get install -y build-essential libcyaml-dev libevent-dev libjson-c-dev libssl-dev
fi

# Debian/Ubuntu's golang-go package is often older than the Go version this
# project needs, so install straight from the official tarball instead --
# same approach the README documents manually.
GO_MIN_VERSION=1.24
GO_INSTALL_VERSION=1.24.0

current_go_version=""
if command -v go >/dev/null 2>&1; then
	current_go_version="$(go version | awk '{print $3}' | sed 's/^go//')"
fi

if [ -z "$current_go_version" ] || [ "$(printf '%s\n%s\n' "$GO_MIN_VERSION" "$current_go_version" | sort -V | head -n1)" != "$GO_MIN_VERSION" ]; then
	echo "Installing Go $GO_INSTALL_VERSION (found: ${current_go_version:-none}, need >= $GO_MIN_VERSION)..."
	case "$(uname -m)" in
		x86_64) go_arch=amd64 ;;
		aarch64|arm64) go_arch=arm64 ;;
		*)
			echo "Unsupported architecture for automatic Go install: $(uname -m)." >&2
			echo "Install Go $GO_MIN_VERSION+ manually from https://go.dev/dl/ and re-run." >&2
			exit 1
			;;
	esac
	go_tarball="go${GO_INSTALL_VERSION}.linux-${go_arch}.tar.gz"
	curl -LO "https://go.dev/dl/${go_tarball}"
	rm -rf /usr/local/go
	tar -C /usr/local -xzf "$go_tarball"
	rm -f "$go_tarball"
	echo "Go installed to /usr/local/go. Add /usr/local/go/bin to PATH in your shell profile for future sessions."
fi

export PATH="$PATH:/usr/local/go/bin"

mkdir -p "$DEST_DIR"

(cd caster && make clean all install DEST_DIR="$DEST_DIR/")
(cd rtcm-go && go build -o "$DEST_DIR/sidecar" ./cmd/sidecar)
(cd ruckus && go build -o "$DEST_DIR/ruckus" .)
install -m 0755 sample-config/chilopod-run.sh "$DEST_DIR/chilopod-run.sh"

mkdir -p "$CONF_DIR" "$LOG_DIR" "$CONF_DIR/ui"
chown caster "$LOG_DIR"

cp -r ui/dist/. "$CONF_DIR/ui/"

for f in caster.yaml source.auth host.auth sourcetable.dat blocklist alarm-email.html; do
	if [ -e "$CONF_DIR/$f" ]; then
		echo "Skipping $f: $CONF_DIR/$f already exists."
	else
		cp "sample-config/$f" "$CONF_DIR/$f"
	fi
done

echo "Installed. Edit $CONF_DIR/caster.yaml and $CONF_DIR/source.auth, then run $DEST_DIR/chilopod-run.sh directly."
