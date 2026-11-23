#!/usr/bin/env python3
"""tests/fake-tux-burnerd.py - a stand-in for Tux Burner's service socket, for tests/gpu-tune-test.sh.

    fake-tux-burnerd.py SOCKET LOG MODE [--caps a,b,...]

Listens on the Unix socket SOCKET (one JSON object per line in, one line out), writes every request line it
receives to LOG, and answers with made-up data. It touches no GPU and no real service. MODE:

    amd       a running service that knows one AMD card (PCI 0000:03:00.0)
    nvidia    a running service that knows one NVIDIA card (PCI 00000000:01:00.0, NVIDIA's long form)
    both      index 0 = the AMD card, index 1 = the NVIDIA card
    nocard    a running service with no card
    denied    answers every connection at once with "daemon client uid is not allowed" and hangs up
    old       a service that speaks protocol 1
    silent    accepts the connection and never answers
    garbage   answers with a line that is not JSON
    hostile   one AMD card whose vendor word and version hold a quote, a backslash, a control character and bytes
              that are not UTF-8 (written to the socket as raw bytes): whatever SRS prints must still be valid JSON

Any request other than "status" and "gpu_capabilities" is logged and refused: the test then fails, because SRS
may only ever send those two. Prints "ready" when it listens; ends on SIGTERM.
"""
import json
import os
import signal
import socket
import sys
import threading

BASE_CAPS = ["client-identity-v1", "gpu-capabilities-v1", "gpu-telemetry-v1", "gpu-writes-v1", "runtime-spec-v1", "amd-tuning-v1"]

AMD = {
    "gpu_count": 1, "vendor": "amd", "locked_clock_control": False,
    "voltage_offset": {"current_mv": -110, "range_mv": [-200, 0]},
    "memory_clock_stock_max_mhz": 1258,
    "vddgfx_floor_mv": -150, "vddgfx_floor_source": "default", "vddgfx_floor_file": "/etc/tux-burner/amd-vddgfx-floor-mv",
    "identity": {"index": 0, "name": "Made-Up Radeon", "driver_version": "", "pci_bus_id": "0000:03:00.0", "pci_device_id": 0x7550, "uuid": "GPU-AMD-TEST"},
    "memory": None, "architecture": None,
    "power_limits": {"power_management_enabled": True, "power_limit_w": 374, "enforced_power_limit_w": 374,
                     "power_limit_default_w": 304, "power_limit_min_w": 212, "power_limit_max_w": 374},
    "clock_offset_ranges_mhz": {"memory": [-1161, 242], "gpc": [-500, 1000]},
    "clock_offsets_mhz": {"gpc": 300, "memory": 117},
    "fan": {"count": 1, "min_speed_pct": 15, "max_speed_pct": 100},
    "features": {"vf_curve": False, "voltage": True},
    "vf_summary": {"active_points": 0, "editable_core_points": 0},
}
NVIDIA = {
    "gpu_count": 1, "vendor": "nvidia", "locked_clock_control": False,
    "voltage_offset": {"current_mv": None, "range_mv": None},
    "memory_clock_stock_max_mhz": None,
    "vddgfx_floor_mv": -150, "vddgfx_floor_source": "default", "vddgfx_floor_file": "/etc/tux-burner/amd-vddgfx-floor-mv",
    "identity": {"index": 0, "name": "Made-Up GeForce", "driver_version": "000.00", "pci_bus_id": "00000000:01:00.0", "pci_device_id": 0x2684, "uuid": "GPU-NV-TEST"},
    "power_limits": {"power_management_enabled": True, "power_limit_w": 320, "enforced_power_limit_w": 320,
                     "power_limit_default_w": 450, "power_limit_min_w": 150, "power_limit_max_w": 600},
    "clock_offset_ranges_mhz": {"memory": [-2000, 6000], "gpc": [-1000, 1000]},
    "clock_offsets_mhz": {"gpc": 125, "memory": 500},
    "features": {"vf_curve": True, "voltage": True},
}


def main():
    path, log, mode = sys.argv[1], sys.argv[2], sys.argv[3]
    caps = list(BASE_CAPS)
    if "--caps" in sys.argv:
        caps += [c for c in sys.argv[sys.argv.index("--caps") + 1].split(",") if c]
    cards = {"amd": [AMD], "nvidia": [NVIDIA], "both": [AMD, NVIDIA], "nocard": []}.get(mode, [AMD])
    lock = threading.Lock()

    def note(line):
        with lock, open(log, "a", encoding="utf-8") as f:
            f.write(line + "\n")

    def answer(line):
        note(line)
        try:
            req = json.loads(line)
        except ValueError:
            return {"ok": False, "error": "request must be a JSON object"}
        method = req.get("method")
        if method == "status":
            return {"ok": True, "result": {"state": "idle", "active_job": None, "build": "testbld",
                                           "version": "0.99.0@CUT@" if mode == "hostile" else "0.99.0",
                                           "protocol_major": 1 if mode == "old" else 2, "protocol_minor": 0, "capabilities": caps}}
        if method == "gpu_capabilities":
            index = req.get("gpu_index", 0)
            if not isinstance(index, int) or index < 0 or index >= len(cards):
                return {"ok": False, "error": f"gpu index {index} is not available"}
            reply = dict(cards[index], gpu_index=index, gpu_count=len(cards))
            if mode == "hostile":
                reply["vendor"] = "@HOSTILE@"
            return {"ok": True, "result": reply}
        return {"ok": False, "error": f"REFUSED by the fake service: {method}"}

    def serve(conn):
        with conn:
            if mode == "denied":
                conn.sendall(b'{"ok":false,"error":"daemon client uid is not allowed"}\n')
                return
            data = b""
            while b"\n" not in data:
                chunk = conn.recv(4096)
                if not chunk:
                    return
                data += chunk
            line = data.split(b"\n", 1)[0].decode("utf-8", "replace")
            if mode == "silent":
                note(line)
                threading.Event().wait(30)
                return
            if mode == "garbage":
                note(line)
                conn.sendall(b"this is not JSON\n")
                return
            if mode == "hostile":
                # the answer as raw bytes: the markers become a quote, a backslash, a tab (all escaped, as JSON asks)
                # and bytes that are not UTF-8 (a lone 0xFF, and the first byte of a two-byte letter with no second)
                raw = json.dumps(answer(line)).encode()
                raw = raw.replace(b"@HOSTILE@", b'a\\"b\\\\c\\td\xffe\xc3').replace(b"@CUT@", b"x\xe2\x82")
                conn.sendall(raw + b"\n")
                return
            conn.sendall((json.dumps(answer(line)) + "\n").encode())

    if os.path.exists(path):
        os.unlink(path)
    srv = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
    srv.bind(path)
    srv.listen(8)
    signal.signal(signal.SIGTERM, lambda *_: os._exit(0))
    print("ready", flush=True)
    while True:
        conn, _ = srv.accept()
        threading.Thread(target=serve, args=(conn,), daemon=True).start()


if __name__ == "__main__":
    main()
