#!/bin/sh
set -e

cd "$(dirname "$0")"

if [ "$(id -u)" -ne 0 ]; then
	echo "configure.sh must run as root." >&2
	exit 1
fi

if ! id caster >/dev/null 2>&1; then
	useradd --system --no-create-home --shell /usr/sbin/nologin caster
fi

if [ ! -e rtcm-go/cmd/sidecar ] || [ ! -e ruckus/main.go ]; then
	git submodule update --init --recursive
fi

if command -v apt-get >/dev/null 2>&1; then
	apt-get install -y build-essential libcyaml-dev libevent-dev libjson-c-dev libssl-dev logrotate
fi

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

echo "Configured. Run ./install.sh to build and install."
