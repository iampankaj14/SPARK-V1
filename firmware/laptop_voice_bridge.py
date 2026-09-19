#!/usr/bin/env python3
import http.server
import socketserver
import os

PORT = 8080
DIRECTORY = os.path.dirname(os.path.abspath(__file__))

class Handler(http.server.SimpleHTTPRequestHandler):
    def __init__(self, *args, **kwargs):
        super().__init__(*args, directory=DIRECTORY, **kwargs)

    def do_GET(self):
        if self.path == '/' or self.path == '/voice':
            self.path = '/voice_portal.html'
        return super().do_GET()

print(f"Starting Localhost Secure Voice Bridge on http://localhost:{PORT}")
with socketserver.TCPServer(("", PORT), Handler) as httpd:
    try:
        httpd.serve_forever()
    except KeyboardInterrupt:
        pass
