import http.server
import socketserver
import os

class MyHttpRequestHandler(http.server.SimpleHTTPRequestHandler):
    def do_GET(self):
        # Check if the requested file exists
        if not os.path.exists(self.path[1:]):  # The path[1:] removes the leading '/' from the path
            # If the file does not exist, serve index.html
            self.path = '/index.html'
        # Call the superclass method to serve the requested path or index.html
        return http.server.SimpleHTTPRequestHandler.do_GET(self)

# Set the port
PORT = 8080

handler = MyHttpRequestHandler

with socketserver.TCPServer(("", PORT), handler) as httpd:
    print("Serving at port", PORT)
    httpd.serve_forever()