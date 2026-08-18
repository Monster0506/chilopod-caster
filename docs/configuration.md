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

The path to the local sourcetable, in NTRIP STR format. For the full field-by-field format, see [Adding a Raw RTCM3 TCP Source](raw-rtcm-sources.md).

### `host_auth_file`

The path to the host authentication file (`host.auth`). This file holds the credentials that the caster uses for outbound connections to other casters, in proxy mode.

Format: `HOST:username:password`

### `rover_auth_file`

Optional. The path to the rover authentication file. When set, any `GET` request for an actual RTCM stream (a real mountpoint or the virtual NEAR base) must present a username and password (HTTP Basic-Auth) matching an enabled entry in this file, or the caster replies `401`. The sourcetable itself (`GET /`) always stays open, with or without this setting.

Format: one entry per line, as `username:password:Y` or `username:password:N` -- the third field enables or disables that account without deleting it.

```
# abc can log in
abc:abc:Y
# temporarily revoked, entry kept for later re-enabling
oldrover:somepassword:N
```

Leaving this setting unset keeps RTCM streams open to any client, with no credentials required -- Chilopod's behavior before this setting existed.

You can manage accounts from the Auth page in the admin UI (`/adm/ui/`), or edit this file by hand.

### `admin_user`

The key that the caster looks up in `source_auth_file` to authenticate `/adm` API requests. The default value is `admin`.

```yaml
admin_user: admin
```

## UI & Sidecar

### `ui_dir`

The directory that Chilopod serves static admin UI files from, at `GET /adm/ui/...`. This is the contents of `ui/dist` after [building the UI](../README.md#building-the-ui). If you omit this setting, Chilopod disables static file serving.

```yaml
ui_dir: /usr/local/etc/chilopod/ui
```

### `sidecar_stats_file`

See [sidecar.md](sidecar.md).

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

A shared bearer token for cluster node synchronization, used by [`POST /adm/api/v1/sync`](admin-api.md#post-admapiv1sync). You need this token only when you run multiple caster nodes. All nodes must share the same value.

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

## Alarms

See [alarms.md](alarms.md).
