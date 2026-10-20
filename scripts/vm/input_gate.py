#!/usr/bin/env python3
"""Exercise the VM's real VNC -> usb-tablet/keyboard -> evdev input path.

Used by INPUT=1 scripts/vm/smoke.sh. Only Python's standard library is
required. HMP takes screenshots; all input travels through RFB, since HMP's
relative mouse events can reach the unread PS/2 device instead of the tablet.
"""

import socket
import struct
import sys
import time
from pathlib import Path

from shot_gate import read_ppm


def receive(sock, count):
    result = b""
    while len(result) < count:
        chunk = sock.recv(count - len(result))
        if not chunk:
            raise RuntimeError("VNC disconnected")
        result += chunk
    return result


def connect_vnc():
    sock = socket.create_connection(("127.0.0.1", 5999), timeout=5)
    if receive(sock, 12) != b"RFB 003.008\n":
        raise RuntimeError("expected RFB 3.8")
    sock.sendall(b"RFB 003.008\n")
    count = receive(sock, 1)[0]
    if 1 not in receive(sock, count):
        raise RuntimeError("local VNC server must offer no authentication")
    sock.sendall(b"\x01")
    if receive(sock, 4) != b"\x00" * 4:
        raise RuntimeError("VNC authentication failed")
    sock.sendall(b"\x01")  # Shared connection.
    info = receive(sock, 24)
    receive(sock, struct.unpack(">I", info[20:24])[0])
    # SetEncodings also initializes QEMU's per-client absolute pointer mode.
    # Without this ordinary viewer handshake step, buttons can arrive while
    # motion is still routed as relative input to the unused PS/2 device.
    sock.sendall(struct.pack(">BBHi", 2, 0, 1, 0))  # Raw encoding.
    print("input_gate: connected VNC display %dx%d" % struct.unpack(">HH", info[:4]))
    return sock


def monitor_command(path, command):
    with socket.socket(socket.AF_UNIX) as sock:
        sock.settimeout(5)
        sock.connect(str(path))

        def prompt():
            data = b""
            while not data.endswith(b"(qemu) "):
                chunk = sock.recv(4096)
                if not chunk:
                    raise RuntimeError("QEMU monitor disconnected")
                data += chunk
            return data

        prompt()
        sock.sendall(command.encode() + b"\n")
        reply = prompt()
        if b"Error" in reply or b"unknown command" in reply:
            raise RuntimeError(f"QEMU monitor: {reply!r}")


class Shot:
    def __init__(self, run, name):
        self.path = (run / f"input-{name}.ppm").resolve()
        # The desktop loop and PTY both need a chance to finish their paint.
        time.sleep(0.3)
        monitor_command(run / "monitor.sock", f"screendump {self.path}")
        self.width, self.height, self.pixels = read_ppm(self.path)
        if len(self.pixels) != self.width * self.height * 3:
            raise RuntimeError(f"incomplete screenshot: {self.path}")

    def crop(self, x, y, width, height):
        return b"".join(
            self.pixels[(row * self.width + x) * 3 : (row * self.width + x + width) * 3]
            for row in range(y, y + height)
        )

    def check_pixel(self, x, y, expected):
        if self.crop(x, y, 1, 1) != bytes([expected]) * 3:
            raise RuntimeError(f"{self.path.name}: unexpected pixel at ({x}, {y})")


def main():
    if len(sys.argv) != 2:
        raise SystemExit(f"usage: {sys.argv[0]} VM_RUN_DIRECTORY")
    run = Path(sys.argv[1]).resolve()
    with connect_vnc() as vnc:
        def pointer(x, y, buttons=0):
            vnc.sendall(struct.pack(">BBHH", 5, buttons, x, y))
            time.sleep(0.06)

        def click(x, y):
            pointer(x, y, 1)
            pointer(x, y)

        before = Shot(run, "before")
        before.check_pixel(300, 200, 255)
        # First motion starts at the icon, without a trip to the origin.
        pointer(44, 112)
        click(44, 112)
        click(44, 112)
        opened = Shot(run, "opened")
        opened.check_pixel(300, 200, 0)
        opened.check_pixel(300, 45, 0)

        pointer(300, 45)
        pointer(300, 45, 1)
        # Capture must keep a drag beyond the desktop, then reverse cleanly.
        pointer(1000, 700, 1)
        edge = Shot(run, "edge")
        edge.check_pixel(300, 45, 255)
        edge.check_pixel(300, 65, 0)
        pointer(310, 55, 1)
        pointer(310, 55)
        dragged = Shot(run, "dragged")
        dragged.check_pixel(300, 45, 255)
        dragged.check_pixel(300, 57, 0)

        # A released drag must stay put while the pointer moves elsewhere.
        pointer(500, 300)
        released = Shot(run, "released")
        # The kernel console can blink its own cursor outside this window.
        if released.crop(30, 50, 600, 400) != dragged.crop(30, 50, 600, 400):
            raise RuntimeError("window moved after pointer release")

        for key in [*map(ord, "echo 24681357"), 0xFF0D]:
            vnc.sendall(struct.pack(">BBHI", 4, 1, 0, key))
            time.sleep(0.03)
            vnc.sendall(struct.pack(">BBHI", 4, 0, 0, key))
            time.sleep(0.03)
        typed = Shot(run, "typed")
        # At frame (30,50), content starts (35,70), with 7x14 cells.
        # The eight digits after '> echo ' must reappear at column zero on
        # the next row: this proves the shell executed, beyond local echo.
        echoed = False
        for row in range(25):
            # Exclude the focused terminal's outline at its top/left edge.
            command_digits = typed.crop(85, 72 + row * 14, 54, 10)
            output_digits = typed.crop(36, 72 + (row + 1) * 14, 54, 10)
            if command_digits.count(255) > 60 and command_digits == output_digits:
                echoed = True
                break
        if not echoed:
            raise RuntimeError("shell output did not repeat the typed digits")

        click(622, 58)
        closed = Shot(run, "closed")
        closed.check_pixel(300, 200, 255)
        closed.check_pixel(300, 57, 255)
    print("input_gate: PASS -- double click, drag, reverse, release, shell output, close")


if __name__ == "__main__":
    main()
