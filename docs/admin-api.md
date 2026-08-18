Admin API
=========

All admin routes are under `/adm/`. The caster serves them on the port or ports that you configure in [`listen`](configuration.md#listen). The sample configuration uses port `2101` by default.

## Quick Reference

| Method | Route | Description |
|---|---|---|
| GET | `/adm/api/v1/net` | Current NTRIP connections |
| GET | `/adm/api/v1/rtcm` | RTCM stream statistics |
| GET | `/adm/api/v1/mem` | Memory usage statistics |
| GET | `/adm/api/v1/nodes` | Cluster node status |
| GET | `/adm/api/v1/livesources` | Active live sources |
| GET | `/adm/api/v1/sourcetables` | Merged sourcetable |
| GET | `/adm/api/v1/alarms` | Recent alarm outcomes, most-recent-first |
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

Returns RTCM stream statistics: the message types seen for each mountpoint (`types`). It also returns the station position, if Chilopod has decoded a 1005 or 1006 message (`pos`). If you set [`sidecar_stats_file`](sidecar.md) and the [rtcm-go sidecar](sidecar.md) has data for a mountpoint, the response merges in a `sidecar` object too. This object has satellite counts, constellation counts, and antenna and receiver info. If the sidecar has data for a mountpoint but Chilopod has not decoded anything for it, the mountpoint still gets an entry. Only the `sidecar` field is filled in.

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

### `GET /adm/api/v1/alarms`

Returns recent [alarm](alarms.md) outcomes, most-recent-first, up to the last 200. Each entry has `mountpoint`, `type` (one of `station_offline`, `station_online`, `low_sv_count`, `position_drift`), `summary`, `sent` (`true`/`false`), `exit_code`, `time`, and an `error` field when `sent` is `false`.

```sh
curl "http://localhost:2101/adm/api/v1/alarms?user=admin&password=admin"
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

Adds a mountpoint. This route appends an entry to `source_auth_file` and a `STR` line to `sourcetable_file`, then reloads the caster. It is the form-based equivalent of editing those files by hand and calling `reload`. For the meaning of each field, see [Adding a Raw RTCM3 TCP Source](raw-rtcm-sources.md). The fields `mountpoint` and `source_password` are required. The fields `lat` and `lon` are required and must be numeric. Every other field is optional. Optional fields default the same way as the manual `STR` line, for example `format` defaults to `RTCM3` and `generator` defaults to `unknown`. If the mountpoint already exists, or a field contains `;`, `:`, or a newline, the route fails with `{"result": -1, "error": "..."}`.

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

Looks up what the caster has decoded for a mountpoint. This is the same data that [`GET /adm/api/v1/rtcm`](#get-admapiv1rtcm) reports. The route rewrites the mountpoint's `STR` line to match, then reloads the caster. This is the automated version of the "update the placeholder `STR` line" step in [Adding a Raw RTCM3 TCP Source](raw-rtcm-sources.md). The route updates two things independently:

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
