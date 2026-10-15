#!/usr/bin/env python3
"""Tiny HTTP file server for serving a directory to an old in-guest browser.

Point it at a directory and a port; it serves a web-1.0-style listing (no CSS,
no JS, plain links) so an ancient browser inside the emulated device (Pocket IE
and friends) can browse the folder and download files over the emulated network.

Usage:
    python fileserver.py <directory> [port]
    python fileserver.py C:\\dumps 8080
    python fileserver.py .                 # defaults to port 8000
"""

import argparse
import html
import os
import socket
import sys
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from urllib.parse import quote, unquote


def lan_ips():
    """Best-effort list of this host's LAN IPv4 addresses, primary first."""
    ips = []
    # The UDP-connect trick picks the interface the OS would route out of;
    # no packet is actually sent.
    try:
        s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        s.connect(("8.8.8.8", 80))
        ips.append(s.getsockname()[0])
        s.close()
    except OSError:
        pass
    try:
        for info in socket.getaddrinfo(socket.gethostname(), None, socket.AF_INET):
            ip = info[4][0]
            if ip not in ips and not ip.startswith("127."):
                ips.append(ip)
    except OSError:
        pass
    return ips


class FileServer(ThreadingHTTPServer):
    # HTTP/1.0 so the oldest clients (which never send Connection: keep-alive)
    # get a clean connection close after every response.
    def __init__(self, addr, handler, root: Path):
        self.root = root
        super().__init__(addr, handler)


class Handler(BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.0"

    def _resolve(self, url_path: str):
        """Map a request path to a filesystem path under root, or None if it
        escapes the served directory (path-traversal guard)."""
        rel = unquote(url_path.split("?", 1)[0].split("#", 1)[0]).lstrip("/")
        root = self.server.root
        target = (root / rel).resolve()
        if target != root and root not in target.parents:
            return None
        return target

    def do_GET(self):
        target = self._resolve(self.path)
        if target is None:
            self.send_error(403, "Forbidden")
            return
        if target.is_dir():
            self._send_listing(target)
        elif target.is_file():
            self._send_file(target)
        else:
            self.send_error(404, "Not Found")

    def do_HEAD(self):
        target = self._resolve(self.path)
        if target is None or not target.exists():
            self.send_error(404, "Not Found")
            return
        if target.is_file():
            self.send_response(200)
            self.send_header("Content-Type", "application/octet-stream")
            self.send_header("Content-Length", str(target.stat().st_size))
            self.end_headers()
        else:
            self.send_response(200)
            self.send_header("Content-Type", "text/html")
            self.end_headers()

    def _send_listing(self, directory: Path):
        root = self.server.root
        rel = directory.relative_to(root)
        disp = "/" + rel.as_posix() if rel.as_posix() != "." else "/"

        rows = []
        if directory != root:
            rows.append('<a href="../">../</a>')

        try:
            entries = sorted(
                directory.iterdir(),
                key=lambda p: (not p.is_dir(), p.name.lower()),
            )
        except OSError as exc:
            self.send_error(500, f"Cannot list directory: {exc}")
            return

        for entry in entries:
            name = entry.name
            href = quote(name)
            if entry.is_dir():
                rows.append(f'<a href="{href}/">{html.escape(name)}/</a>')
            else:
                try:
                    size = entry.stat().st_size
                except OSError:
                    size = 0
                rows.append(
                    f'<a href="{href}">{html.escape(name)}</a> '
                    f"({size} bytes)"
                )

        body = (
            '<!DOCTYPE HTML PUBLIC "-//W3C//DTD HTML 3.2 Final//EN">\n'
            "<html><head><title>Index of {title}</title></head>\n"
            "<body><h2>Index of {title}</h2><hr>\n{rows}\n<hr></body></html>\n"
        ).format(
            title=html.escape(disp),
            rows="<br>\n".join(rows),
        )
        data = body.encode("ascii", "xmlcharrefreplace")
        self.send_response(200)
        self.send_header("Content-Type", "text/html")
        self.send_header("Content-Length", str(len(data)))
        self.end_headers()
        self.wfile.write(data)

    def _send_file(self, path: Path):
        try:
            size = path.stat().st_size
            f = path.open("rb")
        except OSError as exc:
            self.send_error(404, f"Cannot open file: {exc}")
            return
        with f:
            # Force a download rather than inline render: an old browser must
            # save the dump, not try to display it.
            self.send_response(200)
            self.send_header("Content-Type", "application/octet-stream")
            self.send_header("Content-Length", str(size))
            self.send_header(
                "Content-Disposition",
                f'attachment; filename="{path.name}"',
            )
            self.end_headers()
            self._copy(f)

    def _copy(self, f):
        while True:
            chunk = f.read(64 * 1024)
            if not chunk:
                break
            try:
                self.wfile.write(chunk)
            except (BrokenPipeError, ConnectionResetError):
                break

    def log_message(self, fmt, *args):
        sys.stderr.write(
            "%s - %s\n" % (self.address_string(), fmt % args)
        )


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("directory", help="directory to serve")
    parser.add_argument(
        "port", nargs="?", type=int, default=8000, help="port (default 8000)"
    )
    parser.add_argument(
        "--host",
        default="0.0.0.0",
        help="bind address (default 0.0.0.0 = all interfaces)",
    )
    args = parser.parse_args()

    root = Path(args.directory).resolve()
    if not root.is_dir():
        parser.error(f"not a directory: {root}")

    server = FileServer((args.host, args.port), Handler, root)
    print(f"Serving {root}  (bound to {args.host}:{args.port})")
    print(f"  this PC:        http://127.0.0.1:{args.port}/")
    for ip in lan_ips():
        print(f"  on the LAN:     http://{ip}:{args.port}/")
    print(f"  inside CERF:    http://10.0.2.2:{args.port}/   (only if the "
          f"browser runs in the emulated guest, via libslirp NAT)")
    print("If a real device on your network can't connect, allow the port "
          "through Windows Firewall and set the network profile to Private.")
    print("Ctrl-C to stop.")
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        print("\nStopping.")
        server.shutdown()


if __name__ == "__main__":
    main()
