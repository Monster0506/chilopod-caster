#!/usr/bin/env python3
"""Relay a raw TCP RTCM3 stream into a Chilopod mountpoint via NTRIP SOURCE push."""
import argparse
import socket
import sys
import time


def log(msg):
    print(f"{time.strftime('%Y-%m-%d %H:%M:%S')} {msg}", file=sys.stderr, flush=True)


def relay_once(remote_host, remote_port, caster_host, caster_port, mountpoint, password, source_timeout):
    remote = socket.create_connection((remote_host, remote_port), timeout=10)
    remote.settimeout(source_timeout)
    caster = socket.create_connection((caster_host, caster_port), timeout=10)
    caster.settimeout(10)
    handshake = f"SOURCE {password} /{mountpoint}\r\nUser-Agent: rtcm_bridge/1.0\r\n\r\n".encode()
    caster.sendall(handshake)
    ack = caster.recv(256)
    if b"200" not in ack and b"OK" not in ack:
        raise RuntimeError(f"caster rejected SOURCE handshake: {ack!r}")
    log(f"connected: {remote_host}:{remote_port} -> {caster_host}:{caster_port}/{mountpoint}")
    caster.settimeout(None)
    total = 0
    while True:
        try:
            data = remote.recv(4096)
        except socket.timeout:
            raise RuntimeError(f"no data from {remote_host}:{remote_port} for {source_timeout}s")
        if not data:
            raise RuntimeError("remote closed connection")
        caster.sendall(data)
        total += len(data)


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--remote-host", required=True)
    ap.add_argument("--remote-port", type=int, required=True)
    ap.add_argument("--caster-host", default="127.0.0.1")
    ap.add_argument("--caster-port", type=int, default=2101)
    ap.add_argument("--mountpoint", required=True)
    ap.add_argument("--password", required=True)
    ap.add_argument("--source-timeout", type=int, default=15, help="seconds without data before reconnecting")
    args = ap.parse_args()

    backoff = 1
    while True:
        try:
            relay_once(args.remote_host, args.remote_port, args.caster_host, args.caster_port,
                       args.mountpoint, args.password, args.source_timeout)
        except (OSError, RuntimeError) as e:
            log(f"error: {e}, reconnecting in {backoff}s")
            time.sleep(backoff)
            backoff = min(backoff * 2, 30)
        else:
            backoff = 1


if __name__ == "__main__":
    main()
