# Satellite & Antenna Info (rtcm-go Sidecar)

The sidecar connects to every mountpoint on the caster as a normal NTRIP client, decodes each stream, and writes satellite counts, per-constellation counts, and antenna/receiver info to the stats file at `sidecar_stats_file`. Chilopod reads this file on each `/adm/api/v1/rtcm` request and merges it into the response under a `sidecar` key, next to its own `types` and `pos` data.

Verify it's working:

```sh
curl "http://localhost:2101/adm/api/v1/rtcm?user=admin&password=admin"
```

When a mountpoint has live data, its entry gets a `"sidecar"` object:

```json
{
  "MOUNT1": {
    "types": "1005,1008,1077,...",
    "pos": { "lat": 41.5, "lon": -81.5 },
    "sidecar": {
      "connected": true,
      "constellations": { "GPS": 8, "GLONASS": 7, "Galileo": 7 },
      "satellite_count": 22,
      "antenna_descriptor": "SEPCHOKE_B3E6   SPKE",
      "antenna_serial": "5856",
      "receiver_type": "SEPT POLARX5",
      "last_updated": "2026-08-13T03:20:22Z"
    }
  }
}
```

You can also open the admin UI (`/adm/ui/`) and expand the RTCM detail panel for a mountpoint on the Mountpoints page. Satellite counts, constellation counts, and antenna info appear there automatically once the sidecar has data for the mountpoint.

### `sidecar_stats_file`

The path to the stats file that the [rtcm-go sidecar](#satellite--antenna-info-rtcm-go-sidecar) writes. When you set this path, Chilopod merges the satellite counts, constellation counts, and antenna info from the sidecar into `/adm/api/v1/rtcm`, and shows them in the admin UI. Chilopod resolves this path relative to the directory of `caster.yaml`, unless you give an absolute path. This path must match the sidecar's `-out` flag exactly.

```yaml
sidecar_stats_file: mountpoints.json
```
