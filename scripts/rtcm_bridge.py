#!/usr/bin/env python3
"""Relay a raw TCP RTCM3 stream into a Chilopod mountpoint via NTRIP SOURCE push."""
import argparse
import socket
import sys
import time


def log(msg):
    print(f"{time.strftime('%Y-%m-%d %H:%M:%S')} {msg}", file=sys.stderr, flush=True)


def remote_login(remote, login_user, login_pass):
    remote.settimeout(10)
    data = b""
    while b"login:" not in data:
        chunk = remote.recv(1024)
        if not chunk:
            raise RuntimeError("remote closed connection during login")
        data += chunk
    remote.sendall((login_user + "\n").encode())
    data = b""
    while b"Password:" not in data:
        chunk = remote.recv(1024)
        if not chunk:
            raise RuntimeError("remote closed connection waiting for password prompt")
        data += chunk
    remote.sendall((login_pass + "\n").encode())
    data = b""
    while b"Logged in" not in data:
        chunk = remote.recv(1024)
        if not chunk:
            raise RuntimeError("remote closed connection during login confirmation")
        data += chunk
        if b"incorrect" in data.lower():
            raise RuntimeError(f"remote rejected login: {data!r}")
    line_end = data.find(b"\n", data.index(b"Logged in"))
    if line_end < 0:
        return data, b""
    return data[:line_end + 1], data[line_end + 1:]


def relay_once(remote_host, remote_port, caster_host, caster_port, mountpoint, password, source_timeout,
                remote_login_user=None, remote_login_pass=None):
    remote = socket.create_connection((remote_host, remote_port), timeout=10)
    leftover = b""
    if remote_login_user:
        login_resp, leftover = remote_login(remote, remote_login_user, remote_login_pass)
        log(f"remote login ok: {login_resp!r}")
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
    if leftover:
        caster.sendall(leftover)
        total += len(leftover)
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
    ap.add_argument("--remote-login-user", help="username for remote GRIL-style console login, if required")
    ap.add_argument("--remote-login-pass", help="password for remote GRIL-style console login, if required")
    args = ap.parse_args()

    backoff = 1
    while True:
        try:
            relay_once(args.remote_host, args.remote_port, args.caster_host, args.caster_port,
                       args.mountpoint, args.password, args.source_timeout,
                       args.remote_login_user, args.remote_login_pass)
        except (OSError, RuntimeError) as e:
            log(f"error: {e}, reconnecting in {backoff}s")
            time.sleep(backoff)
            backoff = min(backoff * 2, 30)
        else:
            backoff = 1


if __name__ == "__main__":
    main()
