import socket, base64, time

def make_source():
    s = socket.socket()
    s.connect(('127.0.0.1', 2101))
    s.sendall(b'SOURCE admin /admin\r\nUser-Agent: test-source/1.0\r\n\r\n')
    time.sleep(0.2)
    s.setblocking(False)
    try: s.recv(128)
    except BlockingIOError: pass
    s.setblocking(True)
    return s

def make_client():
    s = socket.socket()
    s.connect(('127.0.0.1', 2101))
    auth = base64.b64encode(b'admin:admin').decode()
    s.sendall(b'GET /admin HTTP/1.0\r\nAuthorization: Basic ' + auth.encode() + b'\r\nUser-Agent: test-client/1.0\r\n\r\n')
    time.sleep(0.2)
    s.setblocking(False)
    try: s.recv(256)
    except BlockingIOError: pass
    s.setblocking(True)
    return s

socks = []
src = make_source()
socks.append(src)
print('source connected', flush=True)
time.sleep(0.5)
cli = make_client()
socks.append(cli)
print('client connected', flush=True)

time.sleep(600)
for s in socks:
    s.close()
