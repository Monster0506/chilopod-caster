Chilopod 0.8.2
===============

Chilopod is a high-performance NTRIP caster written in C for the [Centipede-RTK](https://github.com/CentipedeRTK) project, a network of [RTK](https://en.wikipedia.org/wiki/Real-time_kinematic_positioning) bases based in France (see https://centipede-rtk.org).

It uses libevent2 for a minimal memory footprint and can handle tens of thousands of NTRIP sessions on a minimal server. Currently runs on FreeBSD and Linux.

## Contents

- [Features](#features)
- [Requirements](#requirements)
- [Building](#building)
- [Building the UI](#building-the-ui)
- [Installation (Debian/Linux)](#installation-debianlinux)
- [Installation (FreeBSD)](#installation-freebsd)
- [Running](#running)
- [How Chilopod Works](#how-chilopod-works)
- [Adding a Raw RTCM3 TCP Source](#adding-a-raw-rtcm3-tcp-source)
- [Configuration Reference](#configuration-reference)
- [Admin API](#admin-api)

Features
========

 * "Virtual" "near" base algorithm which picks the nearest base from the source table
 * High performance
 * Low memory footprint
 * Supports IPv6 and IPv4
 * NTRIP proxy to fetch from an external caster
 * TLS/SSL server and client support
 * "blocklist" with quotas per IP prefix
 * On-demand stream subscription
 * "wildcard" base configuration to allow unregistered sources to send hidden streams
 * GELF/Graylog export with bulk mode
 * JSON API for remote administration and monitoring
 * API tool `mapi`
 * Multi-threaded mode

Requirements
============

Building Chilopod from source requires:

| Library | Minimum version |
|---|---|
| libevent2 | (any recent) |
| libcyaml | (any recent) |
| json-c | 0.16 |
| openssl | 3.0.15 |

Install them with your platform's package manager:

FreeBSD: `sudo pkg install libevent libcyaml json-c`

Debian: `sudo apt install libcyaml-dev libevent-dev libjson-c-dev libssl-dev`

Building
========

FreeBSD: `cd caster; make clean depend all`

Debian: `cd caster; make clean all`

This produces two binaries inside `caster/`: `caster` (the daemon itself) and `tests` (the unit test suite -- run `./tests` to verify the build before installing).

Building the UI
===============

The pre-built UI is in `ui/dist/` and is committed to the repository -- no Node.js required to deploy.

To rebuild after making changes to the UI source:

```sh
cd ui
npm install
npm run build
```

The output in `ui/dist/` is what gets served at `/adm/ui/`. Copy it to the configured `ui_dir`:

```sh
cp -r ui/dist/* /usr/local/etc/chilopod/ui/
```

Installation (Debian/Linux)
==========================

These steps set Chilopod up as a dedicated system service. Run them as root.

1. Create a `caster` user:
   ```sh
   useradd --system --no-create-home --shell /usr/sbin/nologin caster
   ```

2. Install the binary and `mapi` tool (both go to `/usr/local/sbin/` by default, per the Makefile's `DEST_DIR`):
   ```sh
   cd caster && make install
   ```

3. Create config and log directories:
   ```sh
   mkdir -p /usr/local/etc/chilopod
   mkdir -p /var/log/chilopod
   chown caster /var/log/chilopod
   ```

4. Copy sample config files:
   ```sh
   cp sample-config/caster.yaml    /usr/local/etc/chilopod/caster.yaml
   cp sample-config/source.auth    /usr/local/etc/chilopod/source.auth
   cp sample-config/host.auth      /usr/local/etc/chilopod/host.auth
   cp sample-config/sourcetable.dat /usr/local/etc/chilopod/sourcetable.dat
   cp sample-config/blocklist      /usr/local/etc/chilopod/blocklist
   ```

5. Edit `/usr/local/etc/chilopod/caster.yaml` and `/usr/local/etc/chilopod/source.auth` for your setup -- see [Configuration Reference](#configuration-reference).

6. Run the caster:
   ```sh
   /usr/local/sbin/caster -d
   ```
   Or create a systemd unit -- see [Running](#running).

Installation (FreeBSD)
======================

These steps set Chilopod up as a dedicated system service. Run them as root.

1. Create a `caster` user:
   ```sh
   pw useradd -n caster -d /nonexistent -s /bin/nologin
   ```

2. Install the binary and `mapi` tool (both go to `/usr/local/sbin/` by default, per the Makefile's `DEST_DIR`):
   ```sh
   cd caster && make install
   ```

3. Create config and log directories:
   ```sh
   mkdir -p /usr/local/etc/chilopod
   mkdir -p /var/log/chilopod
   chown caster /var/log/chilopod
   ```

4. Copy sample config files:
   ```sh
   cp sample-config/caster.yaml     /usr/local/etc/chilopod/caster.yaml
   cp sample-config/source.auth     /usr/local/etc/chilopod/source.auth
   cp sample-config/host.auth       /usr/local/etc/chilopod/host.auth
   cp sample-config/sourcetable.dat /usr/local/etc/chilopod/sourcetable.dat
   cp sample-config/blocklist       /usr/local/etc/chilopod/blocklist
   ```

5. Edit `/usr/local/etc/chilopod/caster.yaml` and `/usr/local/etc/chilopod/source.auth` for your setup -- see [Configuration Reference](#configuration-reference).

6. Install the rc script and enable at boot:
   ```sh
   install -m 0755 sample-config/caster.sh /usr/local/etc/rc.d/caster
   sysrc caster_enable=YES
   ```

Running
=======

**FreeBSD:** `service caster start`

**Linux (direct):** `/usr/local/sbin/caster -d`

**Linux (systemd):** Create `/etc/systemd/system/caster.service`:

```ini
[Unit]
Description=Chilopod NTRIP Caster
After=network.target

[Service]
ExecStart=/usr/local/sbin/caster -d
User=caster
Restart=on-failure

[Install]
WantedBy=multi-user.target
```

Then:
```sh
systemctl daemon-reload
systemctl enable --now caster
```

How Chilopod Works
===================

A single running caster can fulfill 3 roles simultaneously, all configured from the same `caster.yaml`.

## Regular NTRIP caster

Configure `sourcetable.dat` for the local sources, `source.auth` for their authentication, and the `listen` section for the IP addresses to listen on.

## NTRIP proxy

Configure the `proxy` section with a reference caster.

The local caster will fetch the sourcetable from the reference caster at `table_refresh_delay` (in seconds) intervals, and announce it merged with its own sourcetable.

Sources will be fetched and served to clients on-demand from the reference caster.

## "NEAR" base

(Previously known as the "V" base)

Should be declared in the local sourcetable (see default config) with its "virtual" field (12th field) set to "1".

When a NTRIP client connects to this base and announces its location through $G*GGA NMEA lines, the caster will serve it the nearest base from its general sourcetable (local + proxy), switching over time when the client moves.

Adding a Raw RTCM3 TCP Source
==============================

Chilopod's "source" side speaks the NTRIP `SOURCE`/`POST` push protocol. Some GNSS receivers (e.g. many Trimble base stations) instead offer a raw TCP socket that just emits RTCM3 bytes with no NTRIP handshake at all. To feed one of these into the caster as a mountpoint:

1. Pick a mountpoint name (e.g. `MOUNT1`) and generate a password:
   ```sh
   openssl rand -hex 12
   ```

2. Register it in your source authentication file -- the file pointed to by `source_auth_file:` in `caster.yaml` (`sample-config/source.auth` by default). One entry per line, colon-separated:
   ```
   MOUNTPOINT:username:password
   ```
   Add:
   ```
   MOUNT1:MOUNT1:<generated password>
   ```
   The username field must be present but is not actually checked for a raw TCP/NTRIP1 source push -- only the mountpoint and password are validated. (You can also add a wildcard entry `*::sharedpassword` to accept a push on *any* mountpoint not otherwise listed, instead of registering each one individually.)

3. Advertise it in your sourcetable file -- the file pointed to by `sourcetable_file:` in `caster.yaml` (`sample-config/sourcetable.dat` by default) -- with a `STR` line. This is the standard NTRIP1 source-table record, 19 semicolon-separated fields:

   | # | Field | Meaning |
   |---|---|---|
   | 1 | `STR` | literal record type |
   | 2 | mountpoint | must match what you registered in step 2 |
   | 3 | identifier | free-text station name |
   | 4 | format | e.g. `RTCM3` |
   | 5 | format-details | comma-separated RTCM message types, e.g. `1004,1006,1008,1012,1013,1033` |
   | 6 | carrier | `0` = no carrier phase, `1` = L1, `2` = L1+L2 |
   | 7 | nav-system | e.g. `GPS+GLO`, `GPS+GLO+GAL+BDS` |
   | 8 | network | free-text network name, or `NONE` |
   | 9 | country | ISO country code, or `NONE` |
   | 10 | latitude | decimal degrees, positive north |
   | 11 | longitude | decimal degrees, positive east (negative for west) |
   | 12 | **virtual flag** | Chilopod-specific: `0` for a real source, `1` for a "NEAR"/virtual base -- **must be `0` here**, or the caster will refuse the source push with a 404 |
   | 13 | solution | `0` = single base, `1` = network RTK |
   | 14 | generator | free-text, e.g. hardware/vendor name |
   | 15 | compr-encryp | compression/encryption, usually `none` |
   | 16 | authentication | `N`, `B` (basic), or `D` (digest) |
   | 17 | fee | `N` or `Y` |
   | 18 | bitrate | approximate stream bitrate in bits/sec, or `0` |
   | 19 | misc | free-text, often left empty |

   You likely won't know the exact RTCM message types or position yet -- a placeholder is fine, since a local (non-virtual) mountpoint only appears in the public sourcetable once a source actually connects and starts streaming:
   ```
   STR;MOUNT1;MOUNT1;RTCM3;1004,1006,1008,1012,1013,1033;2;GPS+GLO;NONE;NONE;0.000;0.000;0;0;bridge;none;N;N;0;
   ```

4. Reload the running caster (no restart needed):
   ```sh
   curl -X POST "http://localhost:2101/adm/api/v1/reload" --data "user=admin&password=admin"
   ```

5. Bridge the raw TCP stream into the NTRIP push protocol with `scripts/rtcm_bridge.py`. It connects to the remote TCP socket, performs the `SOURCE <password> /<mountpoint>` handshake against the caster, and relays bytes through, reconnecting both legs with backoff on any drop:
   ```sh
   python3 scripts/rtcm_bridge.py \
     --remote-host <device-ip> --remote-port <device-port> \
     --caster-host 127.0.0.1 --caster-port 2101 \
     --mountpoint MOUNT1 --password <generated password>
   ```
   Run it under whatever supervises long-lived processes on your system (systemd, a process manager, `nohup` + `&`, etc.) -- it runs forever, retrying on failure.

6. Verify it's live:
   ```sh
   curl "http://localhost:2101/adm/api/v1/net?user=admin&password=admin"       # look for type "source", mountpoint MOUNT1
   curl "http://localhost:2101/adm/api/v1/rtcm?user=admin&password=admin"      # decoded RTCM message types + position
   curl http://localhost:2101/                                                 # STR;MOUNT1;... now present
   ```

7. Once you can see the real decoded message types and position from step 6, go back and fix the placeholder `STR` line in `sourcetable_file` to match, then reload again.

If the connection is refused, check whether the source device's TCP output only allows a single simultaneous client (common on some receivers) -- it may already be in use, or need reconfiguring on the device itself to allow multiple connections.

If your source already speaks NTRIP natively, skip the bridge entirely -- just point it at the caster with the mountpoint/credentials from steps 1-2 directly.

Configuration Reference
=======================

All configuration lives in `caster.yaml`. Sample files are in `sample-config/`.

## Core

### `listen`

List of IP/port pairs to accept connections on. Supports IPv4 and IPv6.

```yaml
listen:
  - port: 2101
    ip: 0.0.0.0
  - port: 2443
    ip: ::0
    tls: true
    tls_full_certificate_chain: /path/to/fullchain.pem
    tls_private_key: /path/to/privkey.pem
```

### `source_auth_file`

Path to the source authentication file (`source.auth`). Controls which username/password pairs are accepted for NTRIP source connections and admin access.

Format -- one entry per line: `MOUNTPOINT:username:password`

```
# Allow a specific source to push to MOUNT1
MOUNT1:sourceuser:sourcepassword
# Admin access (must match admin_user below)
admin:admin:adminpassword
# Wildcard: any mountpoint accepts this password
*::sharedpassword
```

### `sourcetable_file`

Path to the local sourcetable in NTRIP STR format. See [Adding a Raw RTCM3 TCP Source](#adding-a-raw-rtcm3-tcp-source) for the full field-by-field format.

### `host_auth_file`

Path to the host authentication file (`host.auth`). Credentials used when connecting outbound to other casters (proxy mode).

Format: `HOST:username:password`

### `admin_user`

The key looked up in `source_auth_file` to authenticate `/adm` API requests. Defaults to `admin`.

```yaml
admin_user: admin
```

## Proxy & Clustering

### `proxy`

Optional upstream caster to proxy sources from. The remote sourcetable is fetched every `table_refresh_delay` seconds and merged with the local one. Sources are fetched on-demand when a client connects.

```yaml
proxy:
  - host: maincaster.example.com
    port: 2101
    table_refresh_delay: 600
```

### `syncer_auth`

Shared bearer token for cluster node synchronization via `POST /adm/api/v1/sync`. Only needed when running multiple caster nodes. All nodes must share the same value.

```yaml
syncer_auth: mysecrettoken
```

## Logging

### `log` / `access_log`

Paths for the main log and HTTP access log.

### `log_level`

Verbosity of the main log. One of: `EMERG`, `ALERT`, `CRIT`, `ERR`, `WARNING`, `NOTICE`, `INFO`, `DEBUG`, `EDEBUG`.

> **Warning:** `DEBUG` and `EDEBUG` leak passwords to the log file.

### `syslog`

Optional syslog output.

```yaml
syslog:
  - facility: local0
    log_level: INFO
```

### `graylog`

Optional Graylog/GELF log export.

```yaml
graylog:
  - host: graylogserver.example.com
    port: 7777
    uri: '/gelf'
    tls: true
    log_level: INFO
    retry_delay: 30        # seconds between reconnect attempts
    queue_max_size: 1000000
    drainfile: '/tmp/%Y%m%d-%H%M%S.log'
    bulk_max_size: 62000   # 0 to disable bulk mode
    authorization: 'token' # value for Authorization header
```

## Performance & Limits

### `hysteresis_m`

Distance hysteresis in meters for the NEAR base algorithm. Prevents rapid base switching when a client is near the boundary between two bases. Default: `500.0`.

### `backlog_socket`

Size of the kernel send buffer (`SO_SNDBUF`) for client sockets, in bytes. Default: `114688` (112 KB).

### `backlog_evbuffer`

Maximum in-process send backlog per client connection, in bytes. Clients exceeding this are dropped. Default: `16384`.

## Filtering & Access Control

### `rtcm_filter`

Optional RTCM packet filter and converter. Currently limited to one filter with one conversion rule.

```yaml
rtcm_filter:
  - apply: NEAR4          # mountpoint to apply to
    pass: 1005,1006,1033  # RTCM message types to pass through unchanged
    convert:
      - types: 1077,1087  # types to convert
        conversion: msm7_4  # msm7_4 = MSM7MSM4, msm7_3 = MSM7MSM3
```

### `blocklist_file`

Path to the IP blocklist file. Optional.

Admin API
=========

All admin routes are under `/adm/`, served on whichever port(s) you configure in [`listen`](#listen) -- `2101` by default in the sample config.

## Quick Reference

| Method | Route | Description |
|---|---|---|
| GET | `/adm/api/v1/net` | Current NTRIP connections |
| GET | `/adm/api/v1/rtcm` | RTCM stream statistics |
| GET | `/adm/api/v1/mem` | Memory usage statistics |
| GET | `/adm/api/v1/nodes` | Cluster node status |
| GET | `/adm/api/v1/livesources` | Active live sources |
| GET | `/adm/api/v1/sourcetables` | Merged sourcetable |
| POST | `/adm/api/v1/reload` | Reload configuration from disk |
| POST | `/adm/api/v1/drop` | Drop a connection by ID |
| POST | `/adm/api/v1/sync` | Internal cluster sync (token auth, not user/password) |

## Authentication

**v1 API routes** require credentials passed as query string parameters or as a URL-encoded POST body:

```
GET /adm/api/v1/net?user=admin&password=admin
```

**Legacy routes** accept HTTP Basic Auth:

```sh
curl -u admin:admin http://localhost:2101/adm/net
```

The username is looked up as a key in `source_auth_file`. The key used is the value of `admin_user` in `caster.yaml` (default: `admin`).

## v1 API Routes

### `GET /adm/api/v1/net`

Returns a JSON object of all current NTRIP connections -- clients, sources, and admin sessions.

```sh
curl "http://localhost:2101/adm/api/v1/net?user=admin&password=admin"
```

### `GET /adm/api/v1/rtcm`

Returns RTCM stream statistics.

```sh
curl "http://localhost:2101/adm/api/v1/rtcm?user=admin&password=admin"
```

### `GET /adm/api/v1/mem`

Returns memory usage statistics.

```sh
curl "http://localhost:2101/adm/api/v1/mem?user=admin&password=admin"
```

### `GET /adm/api/v1/nodes`

Returns cluster node status (relevant in multi-node deployments).

```sh
curl "http://localhost:2101/adm/api/v1/nodes?user=admin&password=admin"
```

### `GET /adm/api/v1/livesources`

Returns the list of currently active live sources.

```sh
curl "http://localhost:2101/adm/api/v1/livesources?user=admin&password=admin"
```

### `GET /adm/api/v1/sourcetables`

Returns the merged sourcetable (local + proxied sources).

```sh
curl "http://localhost:2101/adm/api/v1/sourcetables?user=admin&password=admin"
```

### `POST /adm/api/v1/reload`

Reloads configuration from disk without restarting. Send as `application/x-www-form-urlencoded`.

```sh
curl -X POST "http://localhost:2101/adm/api/v1/reload" \
  --data "user=admin&password=admin"
```

### `POST /adm/api/v1/drop`

Drops a specific connection by ID. Send as `application/x-www-form-urlencoded`.

```sh
curl -X POST "http://localhost:2101/adm/api/v1/drop" \
  --data "user=admin&password=admin&id=<connection-id>"
```

### `POST /adm/api/v1/sync`

Internal cluster synchronization endpoint. Uses `syncer_auth` token authentication (not the admin user/password). Content-Type must be `application/json`.

## Legacy Routes

These routes use HTTP Basic Auth and return the same data as their v1 equivalents.

| Route | Description |
|---|---|
| `GET /adm/net` | List of NTRIP connections |
| `GET /adm/mem` | Memory statistics |
| `GET /adm/mem.json` | Memory statistics (JSON format) |

## `mapi` Tool

`mapi` is a Python 3 command-line tool (installed to `/usr/local/sbin/mapi`) that wraps the admin API.

### Configuration

`mapi` reads credentials from `~/.mapi.conf` -- a JSON file with three keys:

```json
{
  "user": "admin",
  "password": "admin",
  "baseurl": "http://localhost:2101/adm/api/v1/"
}
```

Create it with:

```sh
cat > ~/.mapi.conf <<'EOF'
{"user": "admin", "password": "admin", "baseurl": "http://localhost:2101/adm/api/v1/"}
EOF
```

Change `baseurl` to point at your caster's address and port. TLS is supported -- use `https://` if the caster is configured with a TLS listener.

### Usage

```
mapi net              # list all connections
mapi rtcm             # RTCM stream statistics
mapi mem              # memory statistics
mapi nodes            # cluster node status
mapi livesources      # active live sources
mapi sourcetables     # merged sourcetable
mapi reload           # reload config from disk
mapi drop <id> [...]  # drop connection(s) by ID
mapi killall          # drop all current connections
```
