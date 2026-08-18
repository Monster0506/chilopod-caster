Adding a Raw RTCM3 TCP Source
==============================

The source side of Chilopod uses the NTRIP `SOURCE`/`POST` push protocol. Some GNSS receivers instead offer a raw TCP socket. This socket sends RTCM3 bytes with no NTRIP handshake. Use the following steps to feed one of these sources into the caster as a mountpoint.

Note: You can do steps 2 through 4 (register the mountpoint and reload) in one call to [`POST /adm/api/v1/sources`](admin-api.md#post-admapiv1sources), or from the Sources page in the admin UI (`/adm/ui/`). Continue reading to learn what that call does, or to do the steps by hand.

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

7. When you can see the real decoded message types and position from step 6, update the placeholder `STR` line in `sourcetable_file` to match. Then reload the caster again. You can also call [`POST /adm/api/v1/sources/detect`](admin-api.md#post-admapiv1sourcesdetect) instead, which does this for you:
   ```sh
   curl -X POST "http://localhost:2101/adm/api/v1/sources/detect" --data "user=admin&password=admin&mountpoint=MOUNT1"
   ```
   You cannot know the real message types before this point. The caster discovers them by decoding the stream once the source is running. You cannot read these values from the device in advance. This detection works only for genuine RTCM3 sources. Chilopod does not parse other formats, for example CMR. For those formats, there is nothing to detect.

If the connection is refused, determine whether the source device's TCP output allows only one client at a time. This limit is common on some receivers. The port can already be in use. You can also reconfigure the device to allow multiple connections.

If your source already speaks NTRIP natively, do not use the bridge. Point the source directly at the caster, with the mountpoint and credentials from steps 1 and 2.
