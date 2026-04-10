#!/usr/bin/env python3

import argparse
import json
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from urllib.parse import unquote


STATE = {
    "puts": 0,
    "deletes": 0,
    "entities": {},
}


class Handler(BaseHTTPRequestHandler):
    server_version = "MockCedarSync/1.0"

    def _json(self, status, payload):
        body = json.dumps(payload).encode("utf-8")
        self.send_response(status)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def do_GET(self):
        if self.path in ("/health", "/v1/", "/v1"):
          return self._json(200, {"status": "ok"})
        if self.path == "/v1/policies":
            return self._json(200, [])
        if self.path == "/v1/data":
            return self._json(
                200,
                {
                    "count": len(STATE["entities"]),
                    "entities": sorted(STATE["entities"].keys()),
                    "puts": STATE["puts"],
                    "deletes": STATE["deletes"],
                },
            )
        if self.path == "/stats":
            return self._json(
                200,
                {
                    "puts": STATE["puts"],
                    "deletes": STATE["deletes"],
                    "count": len(STATE["entities"]),
                },
            )
        return self._json(404, {"error": "not found"})

    def do_PUT(self):
        if not self.path.startswith("/v1/data/single/"):
            return self._json(404, {"error": "not found"})
        entity_id = unquote(self.path.split("/v1/data/single/", 1)[1])
        length = int(self.headers.get("Content-Length", "0"))
        payload = self.rfile.read(length).decode("utf-8") if length else ""
        STATE["entities"][entity_id] = payload
        STATE["puts"] += 1
        return self._json(200, {"ok": True, "entity_id": entity_id})

    def do_DELETE(self):
        if not self.path.startswith("/v1/data/single/"):
            return self._json(404, {"error": "not found"})
        entity_id = unquote(self.path.split("/v1/data/single/", 1)[1])
        STATE["entities"].pop(entity_id, None)
        STATE["deletes"] += 1
        return self._json(200, {"ok": True, "entity_id": entity_id})

    def log_message(self, fmt, *args):
        return


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=18180)
    args = parser.parse_args()

    server = ThreadingHTTPServer((args.host, args.port), Handler)
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        pass
    finally:
        server.server_close()


if __name__ == "__main__":
    main()
