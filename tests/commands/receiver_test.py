"""A bounded local-only smoke check of the provided receiver CLI."""
from pathlib import Path
import socket
import subprocess
import sys

root = Path(__file__).resolve().parents[2]
with subprocess.Popen(
    [sys.executable, str(root / "tools/udp_receiver.py"), "--bind", "127.0.0.1",
     "--port", "0", "--count", "1", "--timeout", "2"],
    stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True,
) as receiver:
    try:
        line = receiver.stdout.readline()
        assert line.startswith("Listening on 127.0.0.1:"), line
        port = int(line.split(":", 1)[1].split()[0])
        with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as sender:
            sender.sendto(b"CUELINK_B3", ("127.0.0.1", port))
        output, errors = receiver.communicate(timeout=4)
        assert receiver.returncode == 0, errors
        assert "Sender 127.0.0.1:" in output and "bytes=10" in output, output
        assert "43 55 45 4c 49 4e 4b 5f 42 33" in output, output
        assert "Text: 'CUELINK_B3'" in output, output
    finally:
        if receiver.poll() is None:
            receiver.kill()
            receiver.communicate()
print("Bounded Python UDP receiver check passed.")
