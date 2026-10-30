#!/usr/bin/env python3
"""Real process and TCP tests. Windows binaries run under Wine when requested."""
import argparse
import json
import os
from pathlib import Path
import random
import socket
import struct
import subprocess
import tempfile
import time

CONTENT = "2a" + "00" * 31


class Probe:
    def __init__(self, prefix, args):
        self.command = [*prefix, *args]
        self.events = []
        self.errors = []
        self.logs = tempfile.TemporaryDirectory(prefix="arcmp-probe-")
        self.output_path = Path(self.logs.name) / "stdout.txt"
        self.error_path = Path(self.logs.name) / "stderr.txt"
        self.output_writer = self.output_path.open("wb")
        self.error_writer = self.error_path.open("wb")
        # Separate open file descriptions: reader seeks cannot change the
        # target's output offset, including when Wine retains inherited handles.
        self.output = self.output_path.open("rt")
        self.diagnostics = self.error_path.open("rt")
        self.offset = 0
        self.stopped = False
        self.process = subprocess.Popen(self.command, stdout=self.output_writer, stderr=self.error_writer, text=True,
                                        env={**os.environ, "WINEDEBUG": "-all"})

    def read(self):
        # Wine services can retain inherited handles after the target exits.
        # Regular files keep observation and cleanup independent of pipe EOF.
        self.output.seek(self.offset)
        while line := self.output.readline():
            try:
                self.events.append(json.loads(line))
            except json.JSONDecodeError:
                break  # writer has not completed this line yet
            self.offset = self.output.tell()

    def wait(self, predicate, timeout=15):
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            self.read()
            for event in self.events:
                if predicate(event):
                    return event
            if self.process.poll() is not None:
                self.diagnostics.seek(0)
                raise AssertionError(f"process exited {self.command}: {self.events[-3:]}, {self.diagnostics.read()}")
            time.sleep(0.01)
        raise AssertionError(f"timeout {self.command}: {self.events[-3:]}, {self.errors}")

    def stop(self):
        if self.stopped:
            return
        if self.process.poll() is None:
            self.process.terminate()
            try:
                self.process.wait(timeout=5)
            except subprocess.TimeoutExpired:
                self.process.kill()
                self.process.wait(timeout=5)
        self.read()
        self.diagnostics.seek(0)
        errors = self.diagnostics.read()
        assert "AddressSanitizer" not in errors and "runtime error:" not in errors, errors
        self.output.close()
        self.diagnostics.close()
        self.output_writer.close()
        self.error_writer.close()
        self.logs.cleanup()
        self.stopped = True


def four_party(prefixes):
    processes = []
    try:
        host = Probe(prefixes[0], ["host", "0", "Host", CONTENT, "60", "exercise"])
        processes.append(host)
        port = host.wait(lambda e: e.get("phase") == 2)["port"]
        for i, prefix in enumerate(prefixes[1:], 1):
            processes.append(Probe(prefix, ["join", "127.0.0.1", str(port), f"Guest {i}", CONTENT, "60", "exercise"]))
        for process in processes:
            event = process.wait(lambda e: e.get("all_ready") and sum(p["online"] for p in e["players"]) == 4)
            assert {p["name"] for p in event["players"]} == {"Host", "Guest 1", "Guest 2", "Guest 3"}
            assert all(p["prototype"] >= 16067 for p in event["players"])
            for slot in range(4):
                process.wait(lambda e, slot=slot: e.get("event") == "chat" and e["slot"] == slot)
        guest_slot = next(e["slot"] for e in processes[2].events if e.get("phase") == 2)
        processes[2].stop()
        host.wait(lambda e: e.get("phase") == 2 and e["revision"] > event["revision"]
                  and not e["players"][guest_slot]["online"] and not e["all_ready"])
        # Use the opposite platform to replace a departed participant.
        replacement = Probe(prefixes[3], ["join", "127.0.0.1", str(port), "Replacement", CONTENT, "60", "exercise"])
        processes.append(replacement)
        replacement.wait(lambda e: e.get("phase") == 2 and e["slot"] == guest_slot)
        replacement.wait(lambda e: e.get("all_ready"))
        return {"case": "four_process_party", "commands": [p.command for p in processes],
                "verified": ["four_ready", "four_way_chat", "late_chat_history", "departure", "replacement"],
                "events": [p.events for p in processes]}
    finally:
        for p in processes:
            if not p.stopped:
                p.stop()


PROTOCOL_VERSION = 45


def frame(kind, payload, sequence=1, version=PROTOCOL_VERSION):
    return struct.pack("!4sHHII", b"ACMP", version, kind, len(payload), sequence) + payload


def read_frame(sock):
    def exact(size):
        result = b""
        while len(result) < size:
            piece = sock.recv(size - len(result))
            assert piece, "early EOF"
            result += piece
        return result
    magic, version, kind, size, sequence = struct.unpack("!4sHHII", exact(16))
    assert magic == b"ACMP" and version == PROTOCOL_VERSION and size <= 1024
    return kind, exact(size), sequence


def wire_faults(prefix):
    host = Probe(prefix, ["host", "0", "Host", CONTENT, "60"])
    sockets = []
    try:
        port = host.wait(lambda e: e.get("phase") == 2)["port"]
        def connect():
            s = socket.create_connection(("127.0.0.1", port), timeout=3)
            s.settimeout(3)
            sockets.append(s)
            return s
        # Fragment every byte across arbitrary TCP boundaries.
        s = connect()
        hello = frame(1, bytes.fromhex(CONTENT) + bytes.fromhex('42' * 16) + b"Raw traveller".ljust(32, b"\0"))
        for byte in hello:
            s.sendall(bytes([byte]))
        kind, payload, _ = read_frame(s)
        assert kind == 2 and len(payload) == 995 and payload[0] == 1 and payload[5:7] == b'\0\0'
        # Coalesced frames must be decoded individually and in order.
        character = frame(6, struct.pack("!I", 16067) + b"Merwin".ljust(32, b"\0"), 2)
        s.sendall(character + frame(5, b"\1", 3) + frame(3, b"Raw chat\0", 4))
        received = [read_frame(s) for _ in range(3)]
        assert [r[0] for r in received] == [2, 2, 4]
        assert b"Raw chat\0" in received[-1][1]
        # Replay, huge length and illegal direction must close only that peer.
        s.sendall(frame(5, b"\1", 4))
        assert s.recv(1) == b""
        for packet in [struct.pack("!4sHHII", b"ACMP", PROTOCOL_VERSION, 1, 0xffffffff, 1),
                       frame(2, b"forged roster"), frame(1, b"bad hello"),
                       frame(16, b"application message before HELLO"),
                       frame(1, bytes.fromhex(CONTENT) + bytes.fromhex('43' * 16) + b"Name".ljust(32, b"\0"), version=PROTOCOL_VERSION - 1),
                       frame(1, bytes.fromhex(CONTENT) + bytes.fromhex('43' * 16) + b"Name".ljust(32, b"\0"), version=99)]:
            bad = connect()
            bad.sendall(packet)
            data = bad.recv(4096)
            if data:
                assert data.startswith(b"ACMP")
            bad.close()
            time.sleep(0.02)
        # A silent or partial HELLO cannot occupy a connection forever.
        silent = connect()
        silent.sendall(hello[:8])
        silent.settimeout(7)
        assert silent.recv(1) == b""
        # Deterministic malformed input corpus through real sockets.
        rng = random.Random(20261020)
        for _ in range(40):
            bad = connect()
            data = rng.randbytes(rng.randrange(16, 1500))
            bad.sendall(data)
            try:
                bad.recv(2048)
            except ConnectionResetError:
                pass
            bad.close()
        good = connect()
        good.sendall(hello)
        assert read_frame(good)[0] == 2
        assert host.process.poll() is None
        return {"case": "wire_faults", "verified": ["byte_fragmentation", "frame_coalescing", "replayed_sequence",
                "oversized_length", "forged_authority", "engine_message_before_handshake", "version_mismatch", "handshake_timeout", "40_malformed_connections", "host_survives"]}
    finally:
        for s in sockets:
            s.close()
        host.stop()


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--linux", type=Path, required=True)
    parser.add_argument("--windows", type=Path)
    parser.add_argument("--output", type=Path, default=Path("evidence/network-integration.json"))
    args = parser.parse_args()
    linux = [str(args.linux.resolve())]
    results = [wire_faults(linux)]
    if args.windows:
        windows = ["wine", str(args.windows.resolve())]
        results += [four_party([linux, linux, windows, windows]), four_party([windows, linux, linux, linux])]
    else:
        results.append(four_party([linux] * 4))
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps({"results": results}, indent=2))
    print("PASS:", ", ".join(r["case"] for r in results), "->", args.output)
