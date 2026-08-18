# Alarm Notifications (ruckus)

Chilopod can watch its own local mountpoints for four conditions and email an operator when one crosses a threshold, using the [ruckus](https://github.com/Monster0506/Ruckus) helper binary (bundled as a submodule -- see [Building](../README.md#building)). A background check runs every 30 seconds. Configure the [`alarms`](#alarms) block to turn this on; each condition below is independently opt-in.

- **`station_offline`** -- a local, non-virtual mountpoint has had no live source connection for at least `after_minutes`. Re-alerts every `min_interval_minutes` while it stays down; this is not a one-shot.
- **`station_online`** -- the reverse transition: a mountpoint that was tracked offline gets a live connection again. Fires immediately on that transition, with no duration threshold.
- **`low_sv_count`** -- the [sidecar](sidecar.md)'s reported satellite count for a mountpoint stays below `min_sats` for at least `after_minutes`. Resets silently, with no separate "recovered" email, once the count is back at or above `min_sats`.
- **`position_drift`** -- the running average distance between a mountpoint's live-decoded RTCM position and its declared sourcetable position exceeds `lat_mm` or `lon_mm` (checked in that order), or its altitude drifts more than `alt_mm` from its own first observed value, for at least `after_minutes`. The NTRIP `STR` format has no declared height field, so altitude compares against a self-baseline instead of a declared value. Resets silently once back under threshold.

Every alert type shares one rate limit: the first email for a given mountpoint and condition sends immediately, but a repeat for the same still-ongoing condition waits at least `min_interval_minutes` since the last send before sending again. Rovers never trigger anything -- all four conditions apply to base stations only.

Chilopod spawns `ruckus` as a one-shot subprocess per alert and never blocks on it. Recent outcomes -- sent or failed, with `ruckus`'s own error text on failure -- are available at [`GET /adm/api/v1/alarms`](admin-api.md#get-admapiv1alarms), and on the admin UI's Dashboard.

### `alarms`

Configures the alarm notification system described above. Omit this block entirely to disable alarms. Each alert type below `alarms` is independently opt-in too -- an absent `station_offline`/`station_online`/`low_sv_count`/`position_drift` block means that alert never fires.

```yaml
alarms:
  smtp:
    host: smtp.example.com
    port: 587                 # optional, default 587
    tls: starttls              # required: none | starttls | smtps
    auth_file: smtp.auth       # optional -- omit for an unauthenticated relay
  recipients:
    - name: Ops                # optional display name
      email: ops@example.com
      alarm_types: [station_offline, low_sv_count]  # optional -- omit for every alarm type
  mountpoints:
    - mountpoint: BASE1
      alarm_types: [station_offline]  # BASE1 is only ever evaluated for this type
  subject: Chilopod Alarm      # optional, default "Chilopod Alarm"
  min_interval_minutes: 15     # optional, default 15
  ruckus_path: /usr/local/sbin/ruckus   # optional, default shown
  email_template: alarm-email.html      # optional, default shown

  station_offline:
    after_minutes: 5
  station_online:
    after_minutes: 0           # optional, default 0 -- {} is shorthand for this
  low_sv_count:
    min_sats: 9
    after_minutes: 2
  position_drift:
    lat_mm: 50
    lon_mm: 50
    alt_mm: 100
    after_minutes: 5
```

`smtp.auth_file` follows the same `host:username:password` format as [`host_auth_file`](configuration.md#host_auth_file), keeping credentials out of `caster.yaml`. Omit it entirely to send through an unauthenticated relay, for example a local Postfix or Exim in relay-only mode -- Chilopod does not require or assume any specific email provider. You can manage credentials from the Auth page in the admin UI (`/adm/ui/`), or edit this file by hand.

`ruckus_path` must point at a built [`ruckus`](#alarm-notifications-ruckus) binary. If it does not exist or fails to run, Chilopod logs the failure and records it at `GET /adm/api/v1/alarms`; it does not retry beyond what `ruckus` itself does internally.

`email_template` is the path to the HTML template used to build the notification email body (see [Alarm Notifications](#alarm-notifications-ruckus) for the `{{PLACEHOLDER}}` format). Falls back to the plain-text summary if the file can't be read.

`recipients[].alarm_types` restricts one recipient to a subset of alarm types (`station_offline`, `station_online`, `low_sv_count`, `position_drift`). Omit it and that recipient gets every type. If an alarm type ends up with zero subscribed recipients, Chilopod records the outcome at `GET /adm/api/v1/alarms` (`sent: false`) without spawning `ruckus` at all.

`mountpoints[].alarm_types` restricts which alarm types even get *evaluated* for one mountpoint, using the same type names as `recipients[].alarm_types`. A mountpoint absent from this list, or listed with `alarm_types` omitted, is checked against every type (the default). Unlike the recipient-level filter, this gates detection itself -- a suppressed type never starts accumulating threshold state for that mountpoint and never appears in its `GET /adm/api/v1/alarms` history at all, not even as an unsent entry.
