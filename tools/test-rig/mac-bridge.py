#!/usr/bin/env python3
"""
mac-bridge.py — Mac-side bridge for the autonomous USB-serial test rig.

Protocol reference: docs/AUTONOMOUS-TEST-RIG-DESIGN.md §5
Wire format: little-endian, 115200 baud 8N1, no flow control.

Command opcodes (Mac → Switch):
  0x01  PUSH_NSO       <u32 len> <len bytes>   — write .nso to SD
  0x02  RESTART_UMENU  (no payload)             — restart qlaunch
  0x04  PRESS_BUTTON   <u8 btn>                 — inject button press
  0x05  SCREENSHOT_REQ (no payload)             — capture framebuffer
  0x06  READ_LOG       (no payload)             — read uMenu log
  0xEE  PING           (no payload)             — echo test

Response opcodes (Switch → Mac):
  0x80  PUSH_NSO_OK    <u32 bytes_written>
  0x81  SCREENSHOT_DATA <u8 fmt> <u32 len> <len bytes>
  0x82  LOG_DATA       <u32 len> <len bytes>
  0xEF  PONG           (no payload)
  0xFE  ERROR          <u8 code> <u8 msg_len> <msg_len bytes>
  0xFF  DONE           (no payload)

Usage (CLI):
  python3 mac-bridge.py push uMenu.nso
  python3 mac-bridge.py ping
  python3 mac-bridge.py restart-umenu
  python3 mac-bridge.py screenshot --out /tmp/test.png
  python3 mac-bridge.py press-button A
  python3 mac-bridge.py read-log --out /tmp/umenu.log

Implemented in this file:
  push_nso()     — fully implemented: opens serial, writes bytes, awaits ACK.
  ping()         — fully implemented: PING / PONG echo.
  restart_umenu(), press_button(), screenshot(), read_log() — implemented for
  the command-dispatch layer; Switch-side support requires harness v2.0.0.
"""

import argparse
import glob
import os
import struct
import sys
import time
from typing import Optional

try:
    import serial
except ImportError:
    sys.exit(
        "[ERR] pyserial not installed. Run: pip3 install pyserial"
    )

# ── Protocol constants ────────────────────────────────────────────────────────

CMD_PUSH_NSO       = 0x01
CMD_RESTART_UMENU  = 0x02
CMD_PRESS_BUTTON   = 0x04
CMD_SCREENSHOT_REQ = 0x05
CMD_READ_LOG       = 0x06
CMD_PING           = 0xEE

RESP_PUSH_NSO_OK      = 0x80
RESP_SCREENSHOT_DATA  = 0x81
RESP_LOG_DATA         = 0x82
RESP_PONG             = 0xEF
RESP_ERROR            = 0xFE
RESP_DONE             = 0xFF

# Error codes returned by Switch in 0xFE ERROR responses
ERR_UMS_CONFLICT    = 0x01
ERR_FS_ERROR        = 0x02
ERR_USB_INIT_FAIL   = 0x03
ERR_SCREENSHOT_FAIL = 0x04
ERR_UNKNOWN_CMD     = 0x05

# Button codes for CMD_PRESS_BUTTON
BUTTON_CODES = {
    "A":     0x01,
    "B":     0x02,
    "X":     0x04,
    "Y":     0x08,
    "PLUS":  0x10,
    "MINUS": 0x20,
    "DUP":   0x40,
    "DDOWN": 0x80,
}

# Serial defaults
DEFAULT_BAUD   = 115200
# How long to wait for a response byte before timing out (seconds)
RECV_TIMEOUT   = 30
# Maximum .nso size: 8 MiB
MAX_NSO_BYTES  = 8 * 1024 * 1024
# Maximum log read: 1 MiB
MAX_LOG_BYTES  = 1 * 1024 * 1024
# Screenshot raw RGBA size: 1280×720×4
SCREENSHOT_RGBA_BYTES = 1280 * 720 * 4

# ── Serial port discovery ─────────────────────────────────────────────────────

def find_switch_port() -> str:
    """
    Glob /dev/cu.usbmodem* and return the first hit.
    Raises RuntimeError if no port is found.
    """
    candidates = sorted(glob.glob("/dev/cu.usbmodem*"))
    if not candidates:
        raise RuntimeError(
            "No /dev/cu.usbmodem* device found. "
            "Ensure the Switch is connected via USB-C (data cable), "
            "not in UMS mode, and the harness NRO is running in rig mode."
        )
    if len(candidates) > 1:
        print(
            f"[WARN] Multiple USB serial devices: {candidates}. "
            f"Using {candidates[0]}. Pass --port to override.",
            file=sys.stderr,
        )
    return candidates[0]


def open_port(port: Optional[str]) -> serial.Serial:
    """
    Open the serial port. If port is None, auto-discover.
    Returns an open serial.Serial instance.
    """
    if port is None:
        port = find_switch_port()
    print(f"[INFO] Opening {port} at {DEFAULT_BAUD} baud", file=sys.stderr)
    ser = serial.Serial(
        port=port,
        baudrate=DEFAULT_BAUD,
        bytesize=serial.EIGHTBITS,
        parity=serial.PARITY_NONE,
        stopbits=serial.STOPBITS_ONE,
        timeout=RECV_TIMEOUT,
        write_timeout=30,
        xonxoff=False,
        rtscts=False,
        dsrdtr=False,
    )
    return ser


# ── Response reading helpers ──────────────────────────────────────────────────

def _read_exact(ser: serial.Serial, n: int) -> bytes:
    """
    Read exactly n bytes from ser. Raises RuntimeError on timeout or short read.
    """
    buf = ser.read(n)
    if len(buf) != n:
        raise RuntimeError(
            f"Short read: expected {n} bytes, got {len(buf)}. "
            "Check cable, baud rate, and that harness is running."
        )
    return buf


def _read_response_byte(ser: serial.Serial) -> int:
    """Read one response opcode byte."""
    b = _read_exact(ser, 1)
    return b[0]


def _read_u32_le(ser: serial.Serial) -> int:
    raw = _read_exact(ser, 4)
    return struct.unpack("<I", raw)[0]


def _read_error_response(ser: serial.Serial) -> str:
    """Parse the payload of a 0xFE ERROR response and return a human string."""
    code = _read_exact(ser, 1)[0]
    msg_len = _read_exact(ser, 1)[0]
    msg = _read_exact(ser, msg_len).decode("ascii", errors="replace") if msg_len else ""
    code_names = {
        ERR_UMS_CONFLICT:    "UMS_CONFLICT",
        ERR_FS_ERROR:        "FS_ERROR",
        ERR_USB_INIT_FAIL:   "USB_INIT_FAIL",
        ERR_SCREENSHOT_FAIL: "SCREENSHOT_FAIL",
        ERR_UNKNOWN_CMD:     "UNKNOWN_CMD",
    }
    code_str = code_names.get(code, f"0x{code:02X}")
    return f"Switch error {code_str}: {msg}" if msg else f"Switch error {code_str}"


def _expect_done_or_error(ser: serial.Serial, context: str) -> None:
    """Read the next response byte; raise on ERROR, pass through on DONE."""
    resp = _read_response_byte(ser)
    if resp == RESP_DONE:
        return
    if resp == RESP_ERROR:
        raise RuntimeError(_read_error_response(ser))
    raise RuntimeError(
        f"{context}: unexpected response opcode 0x{resp:02X}"
    )


# ── push_nso ─────────────────────────────────────────────────────────────────

def push_nso(nso_path: str, port: Optional[str] = None) -> int:
    """
    Push a local .nso file to the Switch over USB serial.

    Protocol:
      Mac sends: 0x01 <u32 len LE> <len bytes of .nso>
      Switch responds:
        0x80 PUSH_NSO_OK  <u32 bytes_written LE>  — success
        0xFE ERROR <code> <msg_len> <msg>          — failure

    Returns the number of bytes confirmed written by the Switch.
    Raises RuntimeError on any error.

    Requirements on Switch side (harness v2.0.0):
      - Rig mode is active (sdmc:/switch/qos-rig-mode.flag present).
      - usbCommsInitialize() succeeded.
      - Harness writes incoming bytes to:
          sdmc:/atmosphere/titles/0100000000001000/exefs/main.nso
        creating directories as needed.
      - Responds with 0x80 + bytes_written after fdatasync.
    """
    if not os.path.isfile(nso_path):
        raise FileNotFoundError(f"NSO file not found: {nso_path}")

    nso_data = open(nso_path, "rb").read()
    nso_len  = len(nso_data)

    if nso_len == 0:
        raise ValueError(f"NSO file is empty: {nso_path}")
    if nso_len > MAX_NSO_BYTES:
        raise ValueError(
            f"NSO file too large: {nso_len} bytes > {MAX_NSO_BYTES} limit"
        )

    ser = open_port(port)
    try:
        # Send command opcode + 4-byte little-endian length
        header = bytes([CMD_PUSH_NSO]) + struct.pack("<I", nso_len)
        ser.write(header)

        # Stream the .nso in 4 KiB chunks so progress is visible
        CHUNK = 4096
        sent = 0
        start = time.monotonic()
        while sent < nso_len:
            end = min(sent + CHUNK, nso_len)
            ser.write(nso_data[sent:end])
            sent = end
            elapsed = time.monotonic() - start
            rate_kbs = sent / 1024 / max(elapsed, 0.001)
            # Progress on stderr so stdout stays clean for scripting
            print(
                f"\r[INFO] Sending {sent}/{nso_len} bytes  "
                f"({rate_kbs:.1f} KB/s)  ",
                end="",
                file=sys.stderr,
            )
        print(file=sys.stderr)  # newline after progress

        # Wait for ACK: 0x80 PUSH_NSO_OK <u32 bytes_written>
        # Switch may take a moment to flush to SD — use RECV_TIMEOUT
        resp = _read_response_byte(ser)
        if resp == RESP_ERROR:
            raise RuntimeError(_read_error_response(ser))
        if resp != RESP_PUSH_NSO_OK:
            raise RuntimeError(
                f"push_nso: unexpected response opcode 0x{resp:02X}"
            )
        bytes_written = _read_u32_le(ser)
        return bytes_written

    finally:
        ser.close()


# ── ping ──────────────────────────────────────────────────────────────────────

def ping(port: Optional[str] = None) -> float:
    """
    Send PING (0xEE), expect PONG (0xEF).
    Returns round-trip time in milliseconds.
    Raises RuntimeError on timeout or unexpected response.

    This is the first-milestone verification command.
    If this works, the USB-serial channel is functional.
    """
    ser = open_port(port)
    try:
        t0 = time.monotonic()
        ser.write(bytes([CMD_PING]))
        resp = _read_response_byte(ser)
        rtt_ms = (time.monotonic() - t0) * 1000.0
        if resp != RESP_PONG:
            raise RuntimeError(
                f"ping: expected PONG (0xEF), got 0x{resp:02X}"
            )
        return rtt_ms
    finally:
        ser.close()


# ── restart_umenu ─────────────────────────────────────────────────────────────

def restart_umenu(port: Optional[str] = None) -> None:
    """
    Tell the harness to restart uMenu (qlaunch).

    Protocol: Mac sends 0x02, Switch sends 0xFF DONE, then exits.
    Atmosphère re-launches qlaunch automatically within ~3 seconds.

    After calling this, wait at least 5 seconds before sending another command.
    The serial port will disappear and re-enumerate when the harness re-starts.

    Requires harness v2.0.0 on Switch.
    """
    ser = open_port(port)
    try:
        ser.write(bytes([CMD_RESTART_UMENU]))
        _expect_done_or_error(ser, "restart_umenu")
    finally:
        ser.close()


# ── press_button ──────────────────────────────────────────────────────────────

def press_button(button: str, port: Optional[str] = None) -> None:
    """
    Inject a single button press+release into the Switch HID layer.

    button: one of A, B, X, Y, PLUS, MINUS, DUP, DDOWN (case-insensitive).
    Protocol: Mac sends 0x04 <u8 btn_code>, Switch sends 0xFF DONE.

    Requires harness v2.0.0 on Switch with hiddbg initialized.
    """
    key = button.upper()
    if key not in BUTTON_CODES:
        raise ValueError(
            f"Unknown button '{button}'. "
            f"Valid: {', '.join(sorted(BUTTON_CODES))}"
        )
    code = BUTTON_CODES[key]
    ser = open_port(port)
    try:
        ser.write(bytes([CMD_PRESS_BUTTON, code]))
        _expect_done_or_error(ser, "press_button")
    finally:
        ser.close()


# ── screenshot ────────────────────────────────────────────────────────────────

def screenshot(out_path: str, port: Optional[str] = None) -> int:
    """
    Capture the Switch framebuffer and save to out_path.

    Protocol:
      Mac sends 0x05.
      Switch responds 0x81 SCREENSHOT_DATA <u8 fmt> <u32 len> <len bytes>.
        fmt=0x00 → raw RGBA 1280×720 (3686400 bytes)
        fmt=0x01 → PNG

    If fmt is raw RGBA and Pillow is installed, converts to PNG automatically.
    Returns the number of image bytes received.

    Requires harness v2.0.0 on Switch.
    """
    ser = open_port(port)
    try:
        ser.write(bytes([CMD_SCREENSHOT_REQ]))
        resp = _read_response_byte(ser)
        if resp == RESP_ERROR:
            raise RuntimeError(_read_error_response(ser))
        if resp != RESP_SCREENSHOT_DATA:
            raise RuntimeError(
                f"screenshot: unexpected response 0x{resp:02X}"
            )
        fmt      = _read_exact(ser, 1)[0]
        img_len  = _read_u32_le(ser)
        img_data = _read_exact(ser, img_len)
    finally:
        ser.close()

    if fmt == 0x00:
        # Raw RGBA 1280×720 — convert to PNG if Pillow is available
        try:
            from PIL import Image
            img = Image.frombytes("RGBA", (1280, 720), img_data)
            img.save(out_path, format="PNG")
        except ImportError:
            # No Pillow: write raw RGBA with .rgba extension
            raw_path = out_path.rsplit(".", 1)[0] + ".rgba"
            with open(raw_path, "wb") as f:
                f.write(img_data)
            print(
                f"[WARN] Pillow not installed; saved raw RGBA to {raw_path}. "
                "Run: pip3 install Pillow",
                file=sys.stderr,
            )
            return img_len
    elif fmt == 0x01:
        # Already PNG
        with open(out_path, "wb") as f:
            f.write(img_data)
    else:
        raise RuntimeError(f"screenshot: unknown image format 0x{fmt:02X}")

    return img_len


# ── read_log ──────────────────────────────────────────────────────────────────

def read_log(out_path: str, port: Optional[str] = None) -> int:
    """
    Read sdmc:/qos-shell/logs/uMenu.0.log from the Switch and save to out_path.

    Protocol:
      Mac sends 0x06.
      Switch responds 0x82 LOG_DATA <u32 len> <len bytes>.

    Returns the number of log bytes received.
    Requires harness v2.0.0 on Switch.
    """
    ser = open_port(port)
    try:
        ser.write(bytes([CMD_READ_LOG]))
        resp = _read_response_byte(ser)
        if resp == RESP_ERROR:
            raise RuntimeError(_read_error_response(ser))
        if resp != RESP_LOG_DATA:
            raise RuntimeError(
                f"read_log: unexpected response 0x{resp:02X}"
            )
        log_len  = _read_u32_le(ser)
        log_data = _read_exact(ser, log_len)
    finally:
        ser.close()

    with open(out_path, "wb") as f:
        f.write(log_data)
    return log_len


# ── compare ───────────────────────────────────────────────────────────────────

def compare(actual_path: str, expected_path: str, threshold: float = 0.02) -> bool:
    """
    Pixel-diff two PNG files. Returns True if they match within threshold.

    threshold: maximum allowed fraction of differing pixels (default 2%).
    Requires Pillow.
    """
    try:
        from PIL import Image, ImageChops
        import math
    except ImportError:
        raise RuntimeError(
            "compare requires Pillow. Run: pip3 install Pillow"
        )

    actual   = Image.open(actual_path).convert("RGB")
    expected = Image.open(expected_path).convert("RGB")

    if actual.size != expected.size:
        raise ValueError(
            f"Image size mismatch: actual {actual.size} vs expected {expected.size}"
        )

    diff = ImageChops.difference(actual, expected)
    total_pixels = actual.width * actual.height
    # Count pixels where any channel differs by more than 4/255 (noise tolerance)
    differing = sum(
        1
        for r, g, b in diff.getdata()
        if r > 4 or g > 4 or b > 4
    )
    match_frac = 1.0 - differing / total_pixels
    passes = match_frac >= (1.0 - threshold)
    print(
        f"[compare] {match_frac*100:.1f}% match  "
        f"({'PASS' if passes else 'FAIL'}  threshold={threshold*100:.0f}%)"
    )
    return passes


# ── CLI ───────────────────────────────────────────────────────────────────────

def _build_parser() -> argparse.ArgumentParser:
    p = argparse.ArgumentParser(
        prog="mac-bridge.py",
        description="Autonomous USB-serial test rig bridge for Q OS Switch development.",
    )
    p.add_argument(
        "--port",
        default=None,
        help="Serial port (e.g. /dev/cu.usbmodem14201). Auto-detected if omitted.",
    )
    sub = p.add_subparsers(dest="cmd", required=True)

    # push
    sp = sub.add_parser("push", help="Push a .nso file to the Switch over USB.")
    sp.add_argument("nso", help="Path to the .nso file to push.")

    # ping
    sub.add_parser("ping", help="Send PING and verify PONG — channel health check.")

    # restart-umenu
    sub.add_parser("restart-umenu", help="Restart uMenu (qlaunch) on the Switch.")

    # screenshot
    sp = sub.add_parser("screenshot", help="Capture framebuffer from Switch.")
    sp.add_argument("--out", default="/tmp/switch-screenshot.png", help="Output PNG path.")

    # press-button
    sp = sub.add_parser("press-button", help="Inject a button press.")
    sp.add_argument(
        "button",
        help=f"Button name. One of: {', '.join(sorted(BUTTON_CODES))}",
    )

    # read-log
    sp = sub.add_parser("read-log", help="Read uMenu.0.log from Switch SD.")
    sp.add_argument("--out", default="/tmp/umenu.log", help="Output path.")

    # compare
    sp = sub.add_parser("compare", help="Pixel-diff two PNG screenshots.")
    sp.add_argument("actual",   help="Actual screenshot PNG.")
    sp.add_argument("expected", help="Expected reference PNG.")
    sp.add_argument(
        "--threshold",
        type=float,
        default=0.02,
        help="Max fraction of differing pixels (default 0.02 = 2%%).",
    )

    return p


def main() -> int:
    parser = _build_parser()
    args   = parser.parse_args()
    port   = args.port

    if args.cmd == "push":
        try:
            n = push_nso(args.nso, port=port)
            print(f"[OK] pushed {n} bytes")
            return 0
        except (RuntimeError, FileNotFoundError, ValueError) as e:
            print(f"[ERR] {e}", file=sys.stderr)
            return 1

    elif args.cmd == "ping":
        try:
            rtt = ping(port=port)
            print(f"[OK] PONG received  RTT={rtt:.1f} ms")
            return 0
        except RuntimeError as e:
            print(f"[ERR] {e}", file=sys.stderr)
            return 1

    elif args.cmd == "restart-umenu":
        try:
            restart_umenu(port=port)
            print("[OK] restart sent — wait ~5 s for uMenu to reload")
            return 0
        except RuntimeError as e:
            print(f"[ERR] {e}", file=sys.stderr)
            return 1

    elif args.cmd == "screenshot":
        try:
            n = screenshot(args.out, port=port)
            print(f"[OK] screenshot saved to {args.out}  ({n} bytes)")
            return 0
        except RuntimeError as e:
            print(f"[ERR] {e}", file=sys.stderr)
            return 1

    elif args.cmd == "press-button":
        try:
            press_button(args.button, port=port)
            print(f"[OK] button {args.button.upper()} pressed")
            return 0
        except (RuntimeError, ValueError) as e:
            print(f"[ERR] {e}", file=sys.stderr)
            return 1

    elif args.cmd == "read-log":
        try:
            n = read_log(args.out, port=port)
            print(f"[OK] log saved to {args.out}  ({n} bytes)")
            return 0
        except RuntimeError as e:
            print(f"[ERR] {e}", file=sys.stderr)
            return 1

    elif args.cmd == "compare":
        try:
            ok = compare(args.actual, args.expected, threshold=args.threshold)
            return 0 if ok else 1
        except (RuntimeError, ValueError) as e:
            print(f"[ERR] {e}", file=sys.stderr)
            return 1

    return 0


if __name__ == "__main__":
    sys.exit(main())
