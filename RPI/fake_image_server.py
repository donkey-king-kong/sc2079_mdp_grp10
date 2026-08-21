from http.server import BaseHTTPRequestHandler, HTTPServer
import time
import re
import json

class FakeImageServer(BaseHTTPRequestHandler):
    def do_POST(self):
        if self.path == '/detect':
            content_length = int(self.headers['Content-Length'])
            body = self.rfile.read(content_length)
            body_str = body.decode('utf-8', errors='ignore')
            
            match = re.search(r'name="object_id"\r\n\r\n(\S+)', body_str)

            obstacle_id_str = "0"
            if match:
                obstacle_id_str = match.group(1)
            print(f"[Fake Img Server] Received image for obstacle ID: {obstacle_id_str}")

            # --- Simulate a long processing time ---
            print("[Fake Img Server] Simulating 5-second image recognition...")
            time.sleep(5)

            # --- Send the response ---
            self.send_response(200)
            self.send_header('Content-type', 'application/json')
            self.end_headers()
            
            try:
                obstacle_id = int(obstacle_id_str)
                img_id = obstacle_id + 10 # Create a unique img_id based on obstacle_id
            except ValueError:
                img_id = -1

            # Return the FULL detection response, as requested for testing the C parser.
            # Note: The C parser is expected to fail on this, but this is for validation.
            response_data = {
                "success": True,
                "count": 1,
                "objects": [
                    {
                        "class_label": f"test_object_{obstacle_id_str}",
                        "img_id": img_id,
                        "confidence": 0.95,
                        "bbox": [10, 20, 30, 40]
                    }
                ]
            }
            
            # Create a compact JSON string without indentation.
            response_json = json.dumps(response_data)
            
            self.wfile.write(response_json.encode('utf-8'))
            print(f"[Fake Img Server] Sent response: {response_json}")
        else:
            self.send_response(404)
            self.end_headers()


def run_image_server():
    # Set to run on port 4000 as per user request.
    server_address = ('0.0.0.0', 4000)
    httpd = HTTPServer(server_address, FakeImageServer)
    print('Fake Image Recognition Server running on http://localhost:4000 ...')
    httpd.serve_forever()

if __name__ == '__main__':
    run_image_server()
