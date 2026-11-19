#!/usr/bin/env python3
"""本地 OTA 固件服务器 (供 ESP32-S3 的 manifest.json / .fw 下载与结果上报测试)。

用法:
    python tools/firmware_server.py --root build/packages --port 8000
然后浏览器打开 http://<PC-IP>:8000/manifest.json 验证。
ESP32-S3 与 PC 必须在同一局域网; 若用手机热点, 请用 PC 在该网段下的 IP。
"""

import argparse
import json
from functools import partial
from http.server import SimpleHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path


class OTAHandler(SimpleHTTPRequestHandler):
    def do_POST(self):
        if self.path.rstrip("/") != "/report":
            self.send_error(404, "only /report is accepted")
            return
        length = int(self.headers.get("Content-Length", "0") or "0")
        body = self.rfile.read(min(length, 4096))
        try:
            payload = json.loads(body.decode("utf-8"))
            print(f"[REPORT] {json.dumps(payload, ensure_ascii=False)}", flush=True)
        except (ValueError, UnicodeDecodeError):
            print(f"[REPORT] raw={body!r}", flush=True)
        response = b'{"ok":true}\n'
        self.send_response(200)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(response)))
        self.end_headers()
        self.wfile.write(response)

    def end_headers(self):
        self.send_header("Cache-Control", "no-store")
        super().end_headers()

    def log_message(self, fmt, *args):
        print("[HTTP] " + (fmt % args), flush=True)


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, default=Path("build/packages"),
                        help="directory containing manifest.json and .fw files")
    parser.add_argument("--host", default="0.0.0.0")
    parser.add_argument("--port", type=int, default=8000)
    args = parser.parse_args()

    root = args.root.resolve()
    root.mkdir(parents=True, exist_ok=True)
    manifest = root / "manifest.json"
    if not manifest.exists():
        print(f"WARNING: {manifest} not found. Run tools/fwpack.py with "
              "--manifest to generate it.")
    handler = partial(OTAHandler, directory=str(root))
    server = ThreadingHTTPServer((args.host, args.port), handler)
    print(f"Serving {root} on http://{args.host}:{args.port}", flush=True)
    print("Firmware base URL: "
          f"http://<PC-LAN-IP>:{args.port}/", flush=True)
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        print("\nstopped")
    finally:
        server.server_close()


if __name__ == "__main__":
    main()
