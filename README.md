Chilopod 0.8.2
===============

Chilopod is an NTRIP caster written in C. It is derived from the [Centipede-RTK](https://github.com/CentipedeRTK) project, a network of [RTK](https://en.wikipedia.org/wiki/Real-time_kinematic_positioning) bases in France (see https://centipede-rtk.org).

Chilopod uses the libevent2 library. This library keeps the memory footprint small. Chilopod can handle tens of thousands of NTRIP sessions on a minimal server. Chilopod runs on FreeBSD and Linux.

## Contents

- [Features](#features)
- [Requirements](#requirements)
- [Building](#building)
- [Building the UI](#building-the-ui)
- [Installation (Debian/Linux)](#installation-debianlinux)
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

Building Chilopod from source requires a C compiler and `make`, plus:

| Library | Minimum version |
|---|---|
| libevent2 | (any recent) |
| libcyaml | (any recent) |
| json-c | 0.16 |
| openssl | 3.0.15 |

A minimal Debian or FreeBSD install does not include a compiler or these libraries by default. Install them with the package manager for your platform.

Debian: 

```sh
sudo apt install build-essential libcyaml-dev libevent-dev libjson-c-dev libssl-dev
```

This README also uses `curl` in the admin API examples. If you use `scripts/rtcm_bridge.py`, you also need `python3` (see [Adding a Raw RTCM3 TCP Source](#adding-a-raw-rtcm3-tcp-source)). A minimal install does not include either tool. Install both with:

```sh
sudo apt install curl python3
```

Building
========

Debian: 

```sh
cd caster; make clean all
```

This produces two binaries in `caster/`: `caster`, the daemon, and `tests`, the unit test suite. Run `./tests` to confirm the build before you install it.

Building the UI
===============

The pre-built UI files are in `ui/dist/`. These files are committed to the repository. You do not need Node.js to deploy Chilopod.

To rebuild the UI after a change to the source, run: `cd ui; npm install; npm run build`

Chilopod serves the output in `ui/dist/` at `/adm/ui/`. Copy this output to the `ui_dir` directory in the configuration:

```sh
cp -r ui/dist/* /usr/local/etc/chilopod/ui/
```

Installation (Debian/Linux)
==========================

These steps configure Chilopod as a dedicated system service. Run the steps as the root user.

1. Create a `caster` user:
   ```sh
   useradd --system --no-create-home --shell /usr/sbin/nologin caster
   ```

2. Install the binary and the `mapi` tool:
   ```sh
   cd caster && make install
   ```
   Note: By default, the Makefile's `DEST_DIR` variable installs both to `/usr/local/sbin/`.

3. Create the configuration and log directories:
   ```sh
   mkdir -p /usr/local/etc/chilopod
   mkdir -p /var/log/chilopod
   chown caster /var/log/chilopod
   ```

4. Copy the sample configuration files:
   ```sh
   cp sample-config/caster.yaml    /usr/local/etc/chilopod/caster.yaml
   cp sample-config/source.auth    /usr/local/etc/chilopod/source.auth
   cp sample-config/host.auth      /usr/local/etc/chilopod/host.auth
   cp sample-config/sourcetable.dat /usr/local/etc/chilopod/sourcetable.dat
   cp sample-config/blocklist      /usr/local/etc/chilopod/blocklist
   ```

5. Edit `/usr/local/etc/chilopod/caster.yaml` and `/usr/local/etc/chilopod/source.auth` for your setup. For more information, see [Configuration Reference](#configuration-reference).

6. Run the caster:
   ```sh
   /usr/local/sbin/caster -d
   ```
   You can also create a systemd unit instead. See [Running](#running).

   ```

Running
=======


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

A single caster instance can perform three roles at the same time. You configure all three roles in the same `caster.yaml` file.

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

Adding a Raw RTCM3 TCP Source
==============================

The source side of Chilopod uses the NTRIP `SOURCE`/`POST` push protocol. Some GNSS receivers instead offer a raw TCP socket. This socket sends RTCM3 bytes with no NTRIP handshake. Use the following steps to feed one of these sources into the caster as a mountpoint.

Note: You can do steps 2 through 4 (register the mountpoint and reload) in one call to [`POST /adm/api/v1/sources`](#post-admapiv1sources), or from the Sources page in the admin UI (`/adm/ui/`). Continue reading to learn what that call does, or to do the steps by hand.

1. Pick a mountpoint name, for example `MOUNT1`. Generate a password:
   ```sh
   openssl rand -hex 12
   ```

2. Register the mountpoint in the source authentication file. This is the file set by `source_auth_file` in `caster.yaml` (`sample-config/source.auth` by default). Use one entry per line, in this colon-separated format:
   ```
   MOUNTPOINT:username:password
   ```
   Add:
   ```
   MOUNT1:MOUNT1:<generated password>
   ```
   The username field must be present. For a raw TCP or NTRIP1 source push, the caster does not check the username. The caster validates only the mountpoint and the password. Note: You can also add a wildcard entry, `*::sharedpassword`, to accept a push on any mountpoint that is not otherwise listed. This lets you skip registering each mountpoint individually.

3. Advertise the mountpoint in the sourcetable file with a `STR` line. This is the file set by `sourcetable_file` in `caster.yaml` (`sample-config/sourcetable.dat` by default). The `STR` line is the standard NTRIP1 source-table record. It has 19 semicolon-separated fields:

   | # | Field | Meaning |
   |---|---|---|
   | 1 | `STR` | literal record type |
   | 2 | mountpoint | must match what you registered in step 2 |
   | 3 | identifier | free-text station name |
   | 4 | format | for example `RTCM3` |
   | 5 | format-details | comma-separated RTCM message types, for example `1004,1006,1008,1012,1013,1033` |
   | 6 | carrier | `0` = no carrier phase, `1` = L1, `2` = L1+L2 |
   | 7 | nav-system | for example `GPS+GLO`, `GPS+GLO+GAL+BDS` |
   | 8 | network | free-text network name, or `NONE` |
   | 9 | country | ISO country code, or `NONE` |
   | 10 | latitude | decimal degrees, positive north |
   | 11 | longitude | decimal degrees, positive east (negative for west) |
   | 12 | **virtual flag** | Chilopod-specific. `0` = real source, `1` = "NEAR" virtual base. This field **must be `0`** here. If it is not `0`, the caster refuses the source push with a 404 error. |
   | 13 | solution | `0` = single base, `1` = network RTK |
   | 14 | generator | free-text, for example hardware or vendor name |
   | 15 | compr-encryp | compression/encryption, usually `none` |
   | 16 | authentication | `N`, `B` (basic), or `D` (digest) |
   | 17 | fee | `N` or `Y` |
   | 18 | bitrate | approximate stream bitrate in bits/sec, or `0` |
   | 19 | misc | free-text, often left empty |

   At this point, you do not yet know the exact RTCM message types or position. A placeholder value is fine. A local, non-virtual mountpoint appears in the public sourcetable only after a source connects and starts streaming:
   ```
   STR;MOUNT1;MOUNT1;RTCM3;1004,1006,1008,1012,1013,1033;2;GPS+GLO;NONE;NONE;0.000;0.000;0;0;bridge;none;N;N;0;
   ```

4. Reload the running caster. A restart is not necessary:
   ```sh
   curl -X POST "http://localhost:2101/adm/api/v1/reload" --data "user=admin&password=admin"
   ```

5. Use `scripts/rtcm_bridge.py` to bridge the raw TCP stream into the NTRIP push protocol. This script connects to the remote TCP socket. It performs the `SOURCE <password> /<mountpoint>` handshake with the caster. It then relays bytes between the two connections. If either connection drops, the script reconnects with a backoff delay:
   ```sh
   python3 scripts/rtcm_bridge.py \
     --remote-host <device-ip> --remote-port <device-port> \
     --caster-host 127.0.0.1 --caster-port 2101 \
     --mountpoint MOUNT1 --password <generated password>
   ```
   Run the script under a process supervisor, for example systemd, another process manager, or `nohup` with `&`. The script runs continuously and retries after a failure.

   Some devices do not stream to a bare TCP connection. These devices show a `login:` and `Password:` prompt first. They send data only after authentication. For these devices, add `--remote-login-user` and `--remote-login-pass`. The bridge performs the login handshake once for each connection, before it relays data. The bridge also forwards any stream bytes that arrive with the login confirmation, instead of dropping them:
   ```sh
   python3 scripts/rtcm_bridge.py \
     --remote-host <device-ip> --remote-port <device-port> \
     --caster-host 127.0.0.1 --caster-port 2101 \
     --mountpoint MOUNT1 --password <generated password> \
     --remote-login-user <device-username> --remote-login-pass <device-password>
   ```
   Whether a device needs this option, and what login messages it expects, depends on the device. Try a bare connection first to find out. See the note below about a refused connection, for the case where nothing listens on the port.

6. Make sure that the source is live:
   ```sh
   curl "http://localhost:2101/adm/api/v1/net?user=admin&password=admin"       # look for type "source", mountpoint MOUNT1
   curl "http://localhost:2101/adm/api/v1/rtcm?user=admin&password=admin"      # decoded RTCM message types + position
   curl http://localhost:2101/                                                 # STR;MOUNT1;... now present
   ```

7. When you can see the real decoded message types and position from step 6, update the placeholder `STR` line in `sourcetable_file` to match. Then reload the caster again. You can also call [`POST /adm/api/v1/sources/detect`](#post-admapiv1sourcesdetect) instead, which does this for you:
   ```sh
   curl -X POST "http://localhost:2101/adm/api/v1/sources/detect" --data "user=admin&password=admin&mountpoint=MOUNT1"
   ```
   You cannot know the real message types before this point. The caster discovers them by decoding the stream once the source is running. You cannot read these values from the device in advance. This detection works only for genuine RTCM3 sources. Chilopod does not parse other formats, for example CMR. For those formats, there is nothing to detect.

If the connection is refused, determine whether the source device's TCP output allows only one client at a time. This limit is common on some receivers. The port can already be in use. You can also reconfigure the device to allow multiple connections.

If your source already speaks NTRIP natively, do not use the bridge. Point the source directly at the caster, with the mountpoint and credentials from steps 1 and 2.

Configuration Reference
=======================

All configuration lives in `caster.yaml`. Sample files are in `sample-config/`.

## Core

### `listen`

A list of IP and port pairs to accept connections on. This list supports IPv4 and IPv6.

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

The path to the source authentication file (`source.auth`). This file controls which username and password pairs the caster accepts for NTRIP source connections and admin access.

Format: one entry per line, as `MOUNTPOINT:username:password`

```
# Allow a specific source to push to MOUNT1
MOUNT1:sourceuser:sourcepassword
# Admin access (must match admin_user below)
admin:admin:adminpassword
# Wildcard: any mountpoint accepts this password
*::sharedpassword
```

### `sourcetable_file`

The path to the local sourcetable, in NTRIP STR format. For the full field-by-field format, see [Adding a Raw RTCM3 TCP Source](#adding-a-raw-rtcm3-tcp-source).

### `host_auth_file`

The path to the host authentication file (`host.auth`). This file holds the credentials that the caster uses for outbound connections to other casters, in proxy mode.

Format: `HOST:username:password`

### `admin_user`

The key that the caster looks up in `source_auth_file` to authenticate `/adm` API requests. The default value is `admin`.

```yaml
admin_user: admin
```

## Proxy & Clustering

### `proxy`

An optional upstream caster to proxy sources from. The caster fetches the remote sourcetable every `table_refresh_delay` seconds and merges it with the local sourcetable. The caster fetches sources on demand, when a client connects.

```yaml
proxy:
  - host: maincaster.example.com
    port: 2101
    table_refresh_delay: 600
```

### `syncer_auth`

A shared bearer token for cluster node synchronization, used by [`POST /adm/api/v1/sync`](#post-admapiv1sync). You need this token only when you run multiple caster nodes. All nodes must share the same value.

```yaml
syncer_auth: mysecrettoken
```

## Logging

### `log` / `access_log`

Paths for the main log and HTTP access log.

### `log_level`

Verbosity of the main log. One of: `EMERG`, `ALERT`, `CRIT`, `ERR`, `WARNING`, `NOTICE`, `INFO`, `DEBUG`, `EDEBUG`.

> **WARNING:** Do not use `DEBUG` or `EDEBUG` in production. These levels write passwords to the log file.

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

The distance hysteresis, in meters, for the NEAR base algorithm. This value prevents rapid base switching when a client is near the boundary between two bases. The default value is `500.0`.

### `backlog_socket`

The size of the kernel send buffer (`SO_SNDBUF`) for client sockets, in bytes. The default value is `114688` (112 KB).

### `backlog_evbuffer`

The maximum in-process send backlog for each client connection, in bytes. The caster drops clients that exceed this value. The default value is `16384`.

## Filtering & Access Control

### `rtcm_filter`

An optional RTCM packet filter and converter. This version supports only one filter with one conversion rule.

```yaml
rtcm_filter:
  - apply: NEAR4          # mountpoint to apply to
    pass: 1005,1006,1033  # RTCM message types to pass through unchanged
    convert:
      - types: 1077,1087  # types to convert
        conversion: msm7_4  # msm7_4 = MSM7MSM4, msm7_3 = MSM7MSM3
```

### `blocklist_file`

The path to the IP blocklist file. This setting is optional.

Admin API
=========

All admin routes are under `/adm/`. The caster serves them on the port or ports that you configure in [`listen`](#listen). The sample configuration uses port `2101` by default.

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
| POST | `/adm/api/v1/sources` | Add a mountpoint (writes source_auth_file + sourcetable_file, reloads) |
| POST | `/adm/api/v1/sources/remove` | Remove a mountpoint and drop its active connection, if any |
| POST | `/adm/api/v1/sources/detect` | Detect a connected RTCM3 source's real message types (and position, if sent) and update its `STR` line |
| POST | `/adm/api/v1/sync` | Internal cluster sync (token auth, not user/password) |

## Authentication

**v1 API routes** require credentials. Pass them as query string parameters, or in a URL-encoded POST body:

```
GET /adm/api/v1/net?user=admin&password=admin
```

**Legacy routes** accept HTTP Basic Auth:

```sh
curl -u admin:admin http://localhost:2101/adm/net
```

The caster looks up the username as a key in `source_auth_file`. This key is the value of `admin_user` in `caster.yaml` (default: `admin`).

## v1 API Routes

### `GET /adm/api/v1/net`

Returns a JSON object with all current NTRIP connections: clients, sources, and admin sessions.

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

### `POST /adm/api/v1/sources`

Adds a mountpoint. This route appends an entry to `source_auth_file` and a `STR` line to `sourcetable_file`, then reloads the caster. It is the form-based equivalent of editing those files by hand and calling `reload`. For the meaning of each field, see [Adding a Raw RTCM3 TCP Source](#adding-a-raw-rtcm3-tcp-source). The fields `mountpoint` and `source_password` are required. The fields `lat` and `lon` are required and must be numeric. Every other field is optional. Optional fields default the same way as the manual `STR` line, for example `format` defaults to `RTCM3` and `generator` defaults to `unknown`. If the mountpoint already exists, or a field contains `;`, `:`, or a newline, the route fails with `{"result": -1, "error": "..."}`.

```sh
curl -X POST "http://localhost:2101/adm/api/v1/sources" \
  --data "user=admin&password=admin&mountpoint=MOUNT1&source_password=secret&lat=41.5&lon=-81.5"
```

> **CAUTION:** For any field value that contains `+`, for example `nav_system=GPS+GLO`, use `--data-urlencode` instead of `--data`. `curl --data` sends `+` as a literal character. Form-urlencoded bodies decode `+` as a space. As a result, the value arrives as `GPS GLO` instead of `GPS+GLO`.
> ```sh
> curl -X POST "http://localhost:2101/adm/api/v1/sources" \
>   --data-urlencode "user=admin" --data-urlencode "password=admin" \
>   --data-urlencode "mountpoint=MOUNT1" --data-urlencode "source_password=secret" \
>   --data-urlencode "nav_system=GPS+GLO" --data-urlencode "lat=41.5" --data-urlencode "lon=-81.5"
> ```
> This is not an issue with the Add Source form in the admin UI. The browser encodes `+` correctly.

### `POST /adm/api/v1/sources/remove`

Removes a mountpoint. This route deletes its entries from `source_auth_file` and `sourcetable_file`, drops any connection currently pushing to it, then reloads the caster. If no matching entry exists, the route fails with `{"result": -1, "error": "mountpoint not found"}`.

```sh
curl -X POST "http://localhost:2101/adm/api/v1/sources/remove" \
  --data "user=admin&password=admin&mountpoint=MOUNT1"
```

### `POST /adm/api/v1/sources/detect`

Looks up what the caster has decoded for a mountpoint. This is the same data that [`GET /adm/api/v1/rtcm`](#get-admapiv1rtcm) reports. The route rewrites the mountpoint's `STR` line to match, then reloads the caster. This is the automated version of the "update the placeholder `STR` line" step in [Adding a Raw RTCM3 TCP Source](#adding-a-raw-rtcm3-tcp-source). The route updates two things independently:

- **Format details**: always, from the decoded RTCM3 message types.
- **Position**: only if the source has sent a 1005 or 1006 message, with station coordinates. When this happens, the response includes `lat` and `lon`. Otherwise, the response omits them. You cannot know a source's real surveyed position in advance. Like the message types, the caster observes the position from the stream itself. For this reason, the `lat` and `lon` values on [`POST /adm/api/v1/sources`](#post-admapiv1sources) need to be approximate only.

The route leaves all other fields on the line unchanged. If the mountpoint is not connected yet, or does not speak RTCM3 at all, for example a CMR source, the route fails with `{"result": -1, "error": "no RTCM3 data observed yet for this mountpoint -- ..."}`. Chilopod parses only RTCM3 framing, so there is nothing to detect from a non-RTCM3 stream.

```sh
curl -X POST "http://localhost:2101/adm/api/v1/sources/detect" \
  --data "user=admin&password=admin&mountpoint=MOUNT1"
# {"result": 0, "types": "1004,1006,1008,1012,1013,1033", "lat": 41.233276, "lon": -81.776917}
```

### `POST /adm/api/v1/sync`

An internal cluster synchronization endpoint. This route uses `syncer_auth` token authentication, not the admin username and password. The `Content-Type` header must be `application/json`.

## Legacy Routes

These routes use HTTP Basic Auth and return the same data as their v1 equivalents.

| Route | Description |
|---|---|
| `GET /adm/net` | List of NTRIP connections |
| `GET /adm/mem` | Memory statistics |
| `GET /adm/mem.json` | Memory statistics (JSON format) |

## `mapi` Tool

`mapi` is a Python 3 command-line tool that wraps the admin API. The install process places it at `/usr/local/sbin/mapi`.

### Configuration

`mapi` reads credentials from `~/.mapi.conf`. This is a JSON file with three keys:

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

Change `baseurl` to point at the address and port of your caster. Chilopod supports TLS. If the caster has a TLS listener configured, use `https://`.

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
