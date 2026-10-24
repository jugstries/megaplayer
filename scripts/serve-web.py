"""Server di sviluppo per la build WebAssembly (build-wasm/).

Le pthread nel browser vogliono SharedArrayBuffer, che c'è solo nelle pagine
isolate cross-origin: il server manda le due intestazioni che servono.

Uso: python3 scripts/serve-web.py [DIRECTORY] [PORTA]
"""
import functools
import http.server
import sys


class IsolatedHandler(http.server.SimpleHTTPRequestHandler):
    def end_headers(self):
        self.send_header("Cross-Origin-Opener-Policy", "same-origin")
        self.send_header("Cross-Origin-Embedder-Policy", "require-corp")
        self.send_header("Cache-Control", "no-store")
        super().end_headers()


def main():
    directory = sys.argv[1] if len(sys.argv) > 1 else "build-wasm"
    port = int(sys.argv[2]) if len(sys.argv) > 2 else 8765
    handler = functools.partial(IsolatedHandler, directory=directory)
    with http.server.ThreadingHTTPServer(("127.0.0.1", port), handler) as server:
        print(f"megaplayer web: http://127.0.0.1:{port}/ (da {directory})", flush=True)
        server.serve_forever()


if __name__ == "__main__":
    main()
