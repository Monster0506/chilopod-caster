Millipede 0.8.2
===============


Millipede is a high-performance NTRIP caster written in C for the [Centipede-RTK](https://github.com/CentipedeRTK) project, a network of [RTK](https://en.wikipedia.org/wiki/Real-time_kinematic_positioning) bases based in France (see https://centipede-rtk.org).


Millipede uses libevent2 for minimal memory footprint.

It can easily handle tens of thousands of NTRIP sessions on a minimal server.

Currently runs on FreeBSD and Linux.

Features:
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

The current version requires:
 * libcyaml
 * libevent2
 * json-c >= 0.16
 * openssl >= 3.0.15

Dependencies
============

FreeBSD: `sudo pkg install libevent libcyaml json-c`

Debian: `sudo apt install libcyaml-dev libevent-dev libjson-c-dev libssl-dev`

Building
========

FreeBSD: `cd caster; make clean depend all`

Debian: `cd caster; make clean all`

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
cp -r ui/dist/* /usr/local/etc/millipede/ui/
```

Installation (Debian/Linux)
==========================

As root:

1. Create a `caster` user:
   ```sh
   useradd --system --no-create-home --shell /usr/sbin/nologin caster
   ```

2. Install the binary and `mapi` tool:
   ```sh
   cd caster && make install
   ```

3. Create config and log directories:
   ```sh
   mkdir -p /usr/local/etc/millipede
   mkdir -p /var/log/millipede
   chown caster /var/log/millipede
   ```

4. Copy sample config files:
   ```sh
   cp sample-config/caster.yaml    /usr/local/etc/millipede/caster.yaml
   cp sample-config/source.auth    /usr/local/etc/millipede/source.auth
   cp sample-config/host.auth      /usr/local/etc/millipede/host.auth
   cp sample-config/sourcetable.dat /usr/local/etc/millipede/sourcetable.dat
   cp sample-config/blocklist      /usr/local/etc/millipede/blocklist
   ```

5. Edit `/usr/local/etc/millipede/caster.yaml` and `/usr/local/etc/millipede/source.auth` for your setup (see Configuration Reference below).

6. Run the caster:
   ```sh
   /usr/local/sbin/caster -d
   ```
   Or create a systemd unit -- see Running below.

Installation (FreeBSD)
======================

As root:

1. Create a `caster` user:
   ```sh
   pw useradd -n caster -d /nonexistent -s /bin/nologin
   ```

2. Install the binary and `mapi` tool:
   ```sh
   cd caster && make install
   ```

3. Create config and log directories:
   ```sh
   mkdir -p /usr/local/etc/millipede
   mkdir -p /var/log/millipede
   chown caster /var/log/millipede
   ```

4. Copy sample config files:
   ```sh
   cp sample-config/caster.yaml     /usr/local/etc/millipede/caster.yaml
   cp sample-config/source.auth     /usr/local/etc/millipede/source.auth
   cp sample-config/host.auth       /usr/local/etc/millipede/host.auth
   cp sample-config/sourcetable.dat /usr/local/etc/millipede/sourcetable.dat
   cp sample-config/blocklist       /usr/local/etc/millipede/blocklist
   ```

5. Edit `/usr/local/etc/millipede/caster.yaml` and `/usr/local/etc/millipede/source.auth` for your setup (see Configuration Reference below).

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
Description=Millipede NTRIP Caster
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

Documentation
=============

There are 3 main functions the caster can fulfill simultaneously, configured from `caster.yaml`.

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

Configuration Reference
=======================

All configuration lives in `caster.yaml`. Sample files are in `sample-config/`.

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

### `proxy`

Optional upstream caster to proxy sources from. The remote sourcetable is fetched every `table_refresh_delay` seconds and merged with the local one. Sources are fetched on-demand when a client connects.

```yaml
proxy:
  - host: maincaster.example.com
    port: 2101
    table_refresh_delay: 600
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

Path to the local sourcetable in NTRIP STR format.

### `host_auth_file`

Path to the host authentication file (`host.auth`). Credentials used when connecting outbound to other casters (proxy mode).

Format: `HOST:username:password`

### `admin_user`

The key looked up in `source_auth_file` to authenticate `/adm` API requests. Defaults to `admin`.

```yaml
admin_user: admin
```

### `syncer_auth`

Shared bearer token for cluster node synchronization via `POST /adm/api/v1/sync`. Only needed when running multiple caster nodes. All nodes must share the same value.

```yaml
syncer_auth: mysecrettoken
```

### `blocklist_file`

Path to the IP blocklist file. Optional.

### `log` / `access_log`

Paths for the main log and HTTP access log.

### `log_level`

Verbosity of the main log. One of: `EMERG`, `ALERT`, `CRIT`, `ERR`, `WARNING`, `NOTICE`, `INFO`, `DEBUG`, `EDEBUG`.

> **Warning:** `DEBUG` and `EDEBUG` leak passwords to the log file.

### `hysteresis_m`

Distance hysteresis in meters for the NEAR base algorithm. Prevents rapid base switching when a client is near the boundary between two bases. Default: `500.0`.

### `backlog_socket`

Size of the kernel send buffer (`SO_SNDBUF`) for client sockets, in bytes. Default: `114688` (112 KB).

### `backlog_evbuffer`

Maximum in-process send backlog per client connection, in bytes. Clients exceeding this are dropped. Default: `16384`.

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

Admin API
=========

All admin routes are under `/adm/`. The server is running on port `2101` by default.

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
