Running
=======

**Direct:** Run the caster and the sidecar as two separate processes:

```sh
/usr/local/sbin/caster -d
/usr/local/sbin/sidecar -caster 127.0.0.1:2101 -out /usr/local/etc/chilopod/mountpoints.json -poll 30s
```

| Flag | Meaning |
|---|---|
| `-caster` | The caster's own `host:port`, as an ordinary NTRIP client would connect to it |
| `-out` | Path to write the stats file to |
| `-poll` | How often to re-check the caster's sourcetable for mountpoints added or removed (default `60s`) |

> **CAUTION: `-out` must point to the exact same file as `sidecar_stats_file` in `caster.yaml`.** Chilopod does not check this for you. If the paths do not match, the admin API omits the `"sidecar"` key for every mountpoint, and gives no error. Use an absolute path on both sides.

**systemd:** Run both from one unit, using the `chilopod-run.sh` wrapper script installed in [Installation](../README.md#installation):

```sh
#!/bin/sh
/usr/local/sbin/sidecar -caster 127.0.0.1:2101 -out /usr/local/etc/chilopod/mountpoints.json -poll 30s &
SIDECAR_PID=$!
trap 'kill "$SIDECAR_PID" 2>/dev/null' EXIT INT TERM
/usr/local/sbin/caster
```

The wrapper starts the sidecar in the background, then runs the caster in the foreground. The wrapper sets a trap for the exit signal. The trap kills the sidecar when caster stops. The caster runs without the `-d` flag here. A `Type=simple` unit requires its `ExecStart` process to stay in the foreground. The process must not fork or detach.

The `Type=simple` unit uses the default `KillMode=control-group` setting. This setting sends the stop signal to every process in the cgroup of the unit: the wrapper, caster, and sidecar. The command `systemctl status` shows the wrapper as `MainPID`, not caster. `Restart=on-failure` still works, because the wrapper's exit status matches caster's exit status.

Create `/etc/systemd/system/caster.service`:

```ini
[Unit]
Description=Chilopod NTRIP Caster (with rtcm-go sidecar)
After=network.target

[Service]
ExecStart=/usr/local/sbin/chilopod-run.sh
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
