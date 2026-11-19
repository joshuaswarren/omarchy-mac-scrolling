#!/usr/bin/env python3
from __future__ import annotations
import os, socket, subprocess, sys, threading
_SIG = os.environ.get("HYPRLAND_INSTANCE_SIGNATURE", "")
_XDG = os.environ.get("XDG_RUNTIME_DIR", "")
if not _SIG or not _XDG:
    sys.stderr.write("HYPRLAND_INSTANCE_SIGNATURE and XDG_RUNTIME_DIR required\n")
    sys.exit(1)
_DIR = os.path.join(_XDG, "hypr", _SIG)
_SOCK = os.path.join(_DIR, ".socket.sock")
os.makedirs(_DIR, mode=0o700, exist_ok=True)
_VERSION = subprocess.check_output(["Hyprland", "--version-json"], text=True).strip()
def _handle(conn):
    data = b""
    while len(data) < 65536:
        chunk = conn.recv(4096)
        if not chunk:
            break
        data += chunk
        if b"\n" in data:
            break
    req = data.decode(errors="replace").strip().lower()
    if "plugins" in req:
        payload = "[]"
    elif "version" in req:
        payload = _VERSION
    else:
        payload = "ok"
    conn.sendall(payload.encode() + b"\n")
    conn.close()
server = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
server.bind(_SOCK)
os.chmod(_SOCK, 0o600)
server.listen(32)
while True:
    client, _ = server.accept()
    threading.Thread(target=_handle, args=(client,), daemon=True).start()
