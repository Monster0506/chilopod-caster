#!/bin/sh
/usr/local/sbin/sidecar -caster 127.0.0.1:2101 -out /usr/local/etc/chilopod/mountpoints.json -poll 30s &
exec /usr/local/sbin/caster
