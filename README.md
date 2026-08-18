Chilopod 0.8.2
===============

Chilopod is an NTRIP caster written in C. It is derived from the [Centipede-RTK](https://github.com/CentipedeRTK) project, a network of [RTK](https://en.wikipedia.org/wiki/Real-time_kinematic_positioning) bases in France (see https://centipede-rtk.org).

Chilopod uses the libevent2 library. This library keeps the memory footprint small. Chilopod can handle tens of thousands of NTRIP sessions on a minimal server. Chilopod runs on FreeBSD and Linux.

## Contents

- [Features](#features)
- [Requirements](#requirements)
- [Building](#building)
- [Building the UI](#building-the-ui)
- [Installation](#installation)
- [Running](#running)
- [How Chilopod Works](#how-chilopod-works)
- [Adding a Raw RTCM3 TCP Source](#adding-a-raw-rtcm3-tcp-source)
- [Configuration Reference](#configuration-reference)
- [Admin API](#admin-api)

Features
========

 * "Near" base algorithm: selects the nearest base from the source table
 * High performance
 * Small memory footprint
 * Support for IPv6 and IPv4
 * NTRIP proxy: fetches sources from an external caster
 * TLS/SSL support for server and client connections
 * Blocklist with quotas per IP prefix
 * On-demand stream subscription
 * "Wildcard" base configuration: lets unregistered sources send hidden streams
 * GELF/Graylog export with bulk mode
 * JSON API for remote administration and monitoring
 * API tool: `mapi`
 * Multi-threaded mode

Requirements
============

Building Chilopod from source requires a C compiler, `make`, and Go 1.24 or newer, plus:

| Library | Minimum version |
|---|---|
| libevent2 | (any recent) |
| libcyaml | (any recent) |
| json-c | 0.16 |
| openssl | 3.0.15 |

A minimal Debian/Ubuntu install has neither a compiler nor these libraries by default. On Debian and Ubuntu, `sudo ./install.sh` installs all of them automatically (see [Installation](#installation)) -- it calls `./configure.sh` first, which installs `build-essential`, `libcyaml-dev`, `libevent-dev`, `libjson-c-dev`, and `libssl-dev` via apt, plus a Go 1.24+ toolchain.

To install them by hand instead, or on another distribution, install them with apt: `sudo apt install build-essential libcyaml-dev libevent-dev libjson-c-dev libssl-dev`

Debian and Ubuntu's `golang-go` package is often older than 1.24. Check with `go version`, and if it reports an older version, install Go from the official tarball instead (check [go.dev/dl](https://go.dev/dl/) for the current release):

- `curl -LO https://go.dev/dl/go1.24.0.linux-amd64.tar.gz`
- `sudo rm -rf /usr/local/go`
- `sudo tar -C /usr/local -xzf go1.24.0.linux-amd64.tar.gz`
- `echo 'export PATH=$PATH:/usr/local/go/bin' >> ~/.profile`
- `source ~/.profile`

You'll also want `curl` for the admin API examples throughout this README, and `python3` if you use `scripts/rtcm_bridge.py` (see [Adding a Raw RTCM3 TCP Source](#adding-a-raw-rtcm3-tcp-source)). `configure.sh` does not install these two: `sudo apt install curl python3`

Building
========

```sh
cd caster; make clean all
```

This produces two binaries inside `caster/`: `caster` (the daemon itself) and `tests` (the unit test suite -- run `./tests` to verify the build before installing).

Chilopod's own RTCM3 decoding covers station position (1005/1006) and which message types have been seen, nothing more. To also decode satellite counts, per-constellation counts, and antenna/receiver info, build the optional sidecar from [rtcm-go](https://github.com/Monster0506/rtcm-go), bundled in this repository as a submodule at `rtcm-go/`, with a companion binary at `cmd/sidecar`. If you cloned without `--recurse-submodules`, fetch it first: `cd .. && git submodule update --init --recursive`

Then build it: `cd rtcm-go && go build -o sidecar ./cmd/sidecar && cd ..`

To send alarm notification emails, build the optional `ruckus` binary from [Ruckus](https://github.com/Monster0506/Ruckus), bundled in this repository as a submodule at `ruckus/`. The same `git submodule update --init --recursive` above fetches it too. Then build it: `cd ruckus && go build -o ruckus . && cd ..`

Building the UI
===============

The pre-built UI is in `ui/dist/` and is committed to the repository. No Node.js required to deploy.

To rebuild after making changes to the UI source (not required otherwise): `cd ui && npm install && npm run build`

Chilopod serves the output in `ui/dist/` at `/adm/ui/`. Copy this output to the `ui_dir` directory in the configuration: `cp -r ui/dist/* /usr/local/etc/chilopod/ui/`

Installation
============

These steps configure Chilopod as a dedicated system service. Run the steps as the root user.

```sh
sudo ./install.sh
```

This calls `./configure.sh` first (system user, submodules, OS packages, Go toolchain), then builds and installs the `caster` daemon and `mapi` tool, the sidecar and `ruckus` binaries, the sample configuration files, the pre-built UI (see [Building the UI](#building-the-ui)), and log rotation.

Or, without the scripts:

1. Create a `caster` user: `useradd --system --no-create-home --shell /usr/sbin/nologin caster`

2. Install the `caster` daemon and `mapi` tool (both go to `/usr/local/sbin/` by default, per the Makefile's `DEST_DIR`), the sidecar binary you built earlier, and the wrapper script that runs both together as one unit:
   - `cd caster && make install && cd ..`
   - `install -m 0755 rtcm-go/sidecar /usr/local/sbin/sidecar`
   - `install -m 0755 sample-config/chilopod-run.sh /usr/local/sbin/chilopod-run.sh`

   Note: By default, the Makefile's `DEST_DIR` variable installs `caster` and `mapi` to `/usr/local/sbin/`.

3. Create the configuration and log directories:
   - `mkdir -p /usr/local/etc/chilopod`
   - `mkdir -p /var/log/chilopod`
   - `chown caster /var/log/chilopod`

4. Copy the sample configuration files:
   - `cp sample-config/caster.yaml /usr/local/etc/chilopod/caster.yaml`
   - `cp sample-config/source.auth /usr/local/etc/chilopod/source.auth`
   - `cp sample-config/host.auth /usr/local/etc/chilopod/host.auth`
   - `cp sample-config/sourcetable.dat /usr/local/etc/chilopod/sourcetable.dat`
   - `cp sample-config/blocklist /usr/local/etc/chilopod/blocklist`

5. Install log rotation, so `/var/log/chilopod/*.log` doesn't grow unbounded: `sed 's#@LOG_DIR@#/var/log/chilopod#g' sample-config/chilopod-logrotate > /etc/logrotate.d/chilopod`

   This rotates daily (or immediately past 1G), keeps 14 compressed generations, and sends `caster` a `SIGHUP` after rotating -- which it already treats as "reopen log files and reload config" (see `signalhup_cb` in `caster.c`), so a rotation never interrupts a running caster.

Either way, once installed:

1. Edit `/usr/local/etc/chilopod/caster.yaml` and `/usr/local/etc/chilopod/source.auth` for your setup, including `sidecar_stats_file`. See [Configuration Reference](#configuration-reference).

2. Run Chilopod. The caster and the sidecar can run as two separate processes, or together as one systemd unit -- see [Running](#running).

Running
=======

Run `chilopod-run.sh` (installed in [Installation](#installation)) in a detached tmux session, if you want the caster to keep running after you log out without setting up a systemd unit:

```sh
tmux new-session -d -s chilopod /usr/local/sbin/chilopod-run.sh
```

Reattach with `tmux attach -t chilopod`; stop it with `tmux kill-session -t chilopod`.

See [docs/deployment.md](docs/deployment.md) for other deployment options.

How Chilopod Works
===================

A single running caster can fulfill 3 roles simultaneously, all configured from the same `caster.yaml`. Alongside these, the sidecar adds satellite and antenna info to the caster's own decoding, and `ruckus` sends alarm notification emails.

## Regular NTRIP caster

To configure this role, do the following:

- Configure `sourcetable.dat` for the local sources.
- Configure `source.auth` for the source authentication.
- Configure the `listen` section for the IP addresses to listen on.

## NTRIP proxy

Configure the `proxy` section with a reference caster.

The local caster fetches the sourcetable from the reference caster. The fetch interval is `table_refresh_delay` seconds. The local caster announces this sourcetable merged with its own sourcetable.

The caster fetches sources from the reference caster on demand and serves them to clients.

## "NEAR" base

(formerly called the "V" base)

Declare the NEAR base in the local sourcetable (see the default configuration). Set its "virtual" field (field 12) to "1".

When a client connects to this base and sends its location in $G*GGA NMEA lines, the caster serves the nearest base from the general sourcetable. This sourcetable includes local and proxy sources. As the client moves, the caster switches to a new nearest base over time.

## Satellite & Antenna Info (rtcm-go Sidecar)

See [docs/sidecar.md](docs/sidecar.md).

## Alarm Notifications (ruckus)

See [docs/alarms.md](docs/alarms.md).

Adding a Raw RTCM3 TCP Source
==============================

See [docs/raw-rtcm-sources.md](docs/raw-rtcm-sources.md).

Configuration Reference
=======================

See [docs/configuration.md](docs/configuration.md).

Admin API
=========

See [docs/admin-api.md](docs/admin-api.md).
