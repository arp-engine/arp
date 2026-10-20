"""Finite real loopback smoke for the shipped C++ server example."""
import subprocess
import socket
import sys

flags = subprocess.CREATE_NO_WINDOW if sys.platform == "win32" else 0
with subprocess.Popen([sys.argv[1], "--port", "0", "--duration-ms", "2000"],
                      stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True,
                      creationflags=flags) as process:
    try:
        line = process.stdout.readline().strip()
        assert line.startswith("listening_port="), line
        port = int(line.split("=", 1)[1])
        for request, body in [
            (b"GET /health HTTP/1.1\r\nHost: localhost\r\n\r\n", b"healthy"),
            (b"GET /users/a%2Fb?q=raw HTTP/1.1\r\nHost: localhost\r\n\r\n", b"a%2Fb"),
            (b"POST /echo HTTP/1.1\r\nHost: localhost\r\nContent-Length: 4\r\n\r\necho", b"echo"),
        ]:
            with socket.create_connection(("127.0.0.1", port), timeout=3) as client:
                client.sendall(request)
                wire = bytearray()
                while block := client.recv(4096):
                    wire.extend(block)
                headers, received = bytes(wire).split(b"\r\n\r\n", 1)
                assert headers.startswith(b"HTTP/1.1 200 "), headers
                assert b"Connection: close" in headers and b"X-ARP: cpp-server" in headers
                assert received == body, received
        _, error = process.communicate(timeout=5)
        assert process.returncode == 0, error
    finally:
        if process.poll() is None:
            process.kill()
            process.wait()
