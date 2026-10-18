"""Savienojums ar kontrolieri caur USB seriālo portu (vai TCP virtuālajai rokai).

Plūsmas kontrole kā Printrun/Marlin: nākamā komanda tiek sūtīta tikai pēc
iepriekšējās atbildes ("ok" / "err ..."). Izņēmumi: JOG (bez atbildes) un
reāllaika simboli '!' (ESTOP) un '?' (statuss) - tie iet uzreiz.
"""
from __future__ import annotations

import queue
import threading
import time
from dataclasses import dataclass, field
from typing import Callable, Optional

import serial
from PySide6.QtCore import QObject, Signal

NUM_JOINTS = 5
STATUS_MS = 100
REPLY_TIMEOUT_S = 6.0
VIRTUAL_URL = "socket://127.0.0.1:5555"
NETWORK_URL = "socket://robotroka.local:23"   # ESP tilts mājas tīklā


@dataclass
class Status:
    state: str = "?"
    pos: list = field(default_factory=lambda: [0.0] * NUM_JOINTS)
    enabled: bool = False
    zeroed: list = field(default_factory=lambda: [False] * NUM_JOINTS)
    endstops: str = "-" * NUM_JOINTS      # '-' atslēgts, '0' brīvs, '1' nospiests
    queue: int = 0
    grip: int = 0
    run: str = ""                         # SD programma: "12/1" (rinda/cikls), "P12/1" = pauze
    seq: int = 0                          # pieaug ar katru statusa rindu
    at: float = 0.0                       # time.monotonic()

    @property
    def idle(self) -> bool:
        return self.state == "Idle" and self.queue == 0


def parse_status(line: str, prev: Status) -> Status:
    """'<Idle|P:1.00,...|E:1|Z:11111|S:-----|Q:0|G:0>' -> Status"""
    parts = line.strip()[1:-1].split("|")
    st = Status(state=parts[0], pos=list(prev.pos), enabled=prev.enabled, zeroed=list(prev.zeroed),
                endstops=prev.endstops, queue=prev.queue, grip=prev.grip, seq=prev.seq + 1, at=time.monotonic())
    for p in parts[1:]:
        key, _, val = p.partition(":")
        try:
            if key == "P":
                st.pos = [float(v) for v in val.split(",")]
            elif key == "E":
                st.enabled = val == "1"
            elif key == "Z":
                st.zeroed = [c == "1" for c in val]
            elif key == "S":
                st.endstops = val
            elif key == "Q":
                st.queue = int(val)
            elif key == "G":
                st.grip = int(val)
            elif key == "R":
                st.run = val
        except ValueError:
            pass
    return st


def with_checksum(line: str) -> str:
    cs = 0
    for ch in line:
        cs ^= ord(ch)
    return f"{line}*{cs:02X}"


@dataclass
class Reply:
    ok: bool
    code: str            # kļūdas kods ("" ja ok)
    lines: list          # rindas, kas saņemtas starp komandu un atbildi (CFG, ES, TMC izvade)
    status_seq: int      # statusa numurs atbildes brīdī: vēlāki statusi atspoguļo komandas sekas


ReplyCallback = Callable[[Reply], None]


@dataclass
class _Cmd:
    text: str
    callback: Optional[ReplyCallback]
    lines: list = field(default_factory=list)


class Link(QObject):
    """Viens savienojums ar kontrolieri. Signāli tiek piegādāti GUI pavedienā."""

    connection_changed = Signal(bool, str)     # savienots?, ziņa
    line_received = Signal(str)                 # katra rinda no kontroliera
    line_sent = Signal(str)
    status_changed = Signal(object)             # Status
    event = Signal(str)                         # [READY] [HOME] [ENDSTOP] [ESTOP] [CFG] [TMC] ...
    bridge_line = Signal(str)                   # tilta rindas "#..." (tikai caur tīklu: #rcraw, #rcmap ...)
    _reply = Signal(object, object)             # iekšējs: callback, Reply

    def __init__(self):
        super().__init__()
        self._ser: Optional[serial.SerialBase] = None
        self._wlock = threading.Lock()
        self._queue: "queue.Queue[_Cmd]" = queue.Queue()
        self._pending: Optional[_Cmd] = None
        self._reply_evt = threading.Event()
        self._ready_evt = threading.Event()
        self._stop = threading.Event()
        self._threads: list = []
        self.status = Status()
        self.url = ""
        self._reply.connect(self._deliver_reply)

    # ------------------------------------------------------------------ savienojums
    @property
    def connected(self) -> bool:
        return self._ser is not None

    def open(self, url: str, baud: int = 115200) -> None:
        self.close()
        ser = serial.serial_for_url(url, baudrate=baud, timeout=0.1, write_timeout=2)
        self._ser = ser
        self.url = url
        self._stop.clear()
        self._ready_evt.clear()
        self._queue = queue.Queue()
        self._pending = None
        self.status = Status()
        self._threads = [threading.Thread(target=self._reader, daemon=True),
                         threading.Thread(target=self._writer, daemon=True)]
        for t in self._threads:
            t.start()
        self.connection_changed.emit(True, f"Savienots: {url}")
        # Sākotnējie pieprasījumi (writer tos sūta pēc [READY] vai 2.5 s)
        self.send(f"AUTO {STATUS_MS}")

    def close(self, reason: str = "Atvienots") -> None:
        if not self._ser:
            return
        self._stop.set()
        self._reply_evt.set()
        try:
            self._ser.close()
        except Exception:
            pass
        self._ser = None
        for t in self._threads:
            if t is not threading.current_thread():
                t.join(timeout=1)
        self._threads = []
        self.connection_changed.emit(False, reason)

    # ------------------------------------------------------------------ sūtīšana
    def send(self, text: str, callback: Optional[ReplyCallback] = None) -> None:
        """Rindā; tiek sūtīta, kad iepriekšējā komanda saņēmusi atbildi."""
        if not self._ser:
            if callback:
                callback(Reply(False, "not_connected", [], 0))
            return
        self._queue.put(_Cmd(text.strip(), callback))

    def send_jog(self, values) -> None:
        """JOG bez atbildes - iet uzreiz, neatkarīgi no rindas."""
        self._write_raw("JOG " + " ".join(f"{v:.1f}" for v in values) + "\n")

    def send_bridge(self, text: str) -> None:
        """Tilta komanda (#rcmap, #rcwatch ...): bez rindas un bez ok/err gaidīšanas."""
        self._write_raw(text.strip() + "\n")

    def realtime(self, ch: str) -> None:
        self._write_raw(ch)
        if ch == "!":
            # ESTOP iztīra kontroliera rindu; savējo arī
            self.clear_queue()

    def clear_queue(self) -> None:
        try:
            while True:
                cmd = self._queue.get_nowait()
                if cmd.callback:
                    self._reply.emit(cmd.callback, Reply(False, "cancelled", [], self.status.seq))
        except queue.Empty:
            pass

    def _write_raw(self, data: str) -> None:
        ser = self._ser
        if not ser:
            return
        try:
            with self._wlock:
                ser.write(data.encode("ascii", "replace"))
        except Exception as e:  # noqa: BLE001
            self._fail(f"Rakstīšanas kļūda: {e}")
            return
        if data.strip() and not data.startswith(("JOG", "#rcwatch")) and data not in ("?",):
            self.line_sent.emit(data.strip())

    # ------------------------------------------------------------------ pavedieni
    def _writer(self) -> None:
        # Mega pārstartējas pēc porta atvēršanas -> gaidām [READY] (vai jebkuru rindu)
        self._ready_evt.wait(2.5)
        while not self._stop.is_set():
            try:
                cmd = self._queue.get(timeout=0.1)
            except queue.Empty:
                continue
            self._pending = cmd
            self._reply_evt.clear()
            self._write_raw(cmd.text + "\n")
            if not self._reply_evt.wait(REPLY_TIMEOUT_S) and not self._stop.is_set():
                self.event.emit(f"[HOST] nav atbildes uz '{cmd.text}'")
                if cmd.callback:
                    self._reply.emit(cmd.callback, Reply(False, "timeout", cmd.lines, self.status.seq))
                self._pending = None

    def _reader(self) -> None:
        buf = b""
        while not self._stop.is_set():
            ser = self._ser
            if not ser:
                break
            try:
                chunk = ser.read(256)
            except Exception as e:  # noqa: BLE001
                self._fail(f"Savienojums pārtrūka: {e}")
                return
            if not chunk:
                continue
            buf += chunk
            while b"\n" in buf:
                raw, buf = buf.split(b"\n", 1)
                line = raw.decode("utf-8", "replace").strip()
                if line:
                    self._handle_line(line)

    def _handle_line(self, line: str) -> None:
        self._ready_evt.set()
        if line.startswith("#"):
            # tilta rinda (ne kontroliera atbilde) - nejaucam ar komandu atbildēm
            self.bridge_line.emit(line)
            if not line.startswith("#rcraw"):
                self.line_received.emit(line)
            return
        if line.startswith("<") and line.endswith(">"):
            self.status = parse_status(line, self.status)
            self.status_changed.emit(self.status)
            self.line_received.emit(line)
            return
        self.line_received.emit(line)

        if line == "ok" or line.startswith("err "):
            cmd = self._pending
            self._pending = None
            if cmd and cmd.callback:
                ok = line == "ok"
                self._reply.emit(cmd.callback, Reply(ok, "" if ok else line[4:].strip(), cmd.lines, self.status.seq))
            self._reply_evt.set()
            return

        if line.startswith("[READY]"):
            # kontrolieris pārstartējās: iepriekšējā komanda pazuda
            if self._pending:
                self._pending = None
                self._reply_evt.set()
            self.status = Status()
            self.send(f"AUTO {STATUS_MS}")
        if self._pending is not None:
            self._pending.lines.append(line)
        if line.startswith("["):
            self.event.emit(line)

    def _fail(self, msg: str) -> None:
        if self._ser:
            self._stop.set()
            try:
                self._ser.close()
            except Exception:
                pass
            self._ser = None
            self._reply_evt.set()
            self.connection_changed.emit(False, msg)

    def _deliver_reply(self, callback, reply) -> None:
        callback(reply)


def virtual_com_pairs(ports=None) -> list:
    """com0com virtuālo COM portu pāri [(programmas_ports, rokas_ports)], piem. [("COM10", "COM11")].
    com0com porti ir CNCA<n> un CNCB<n>: ko raksta vienā, to saņem otrā."""
    import re
    if ports is None:
        from serial.tools import list_ports as lp
        ports = lp.comports()
    sides = {}
    for p in ports:
        text = f"{p.hwid} {p.description}"
        if "com0com" not in text.lower():
            continue
        m = re.search(r"CNC([AB])(\d+)", text, re.IGNORECASE)
        if m:
            sides[(m.group(1).upper(), m.group(2))] = p.device
    pairs = []
    for (side, n), dev in sorted(sides.items()):
        if side == "A" and ("B", n) in sides:
            pairs.append((dev, sides[("B", n)]))
    return pairs


def list_ports() -> list:
    """[(url, apraksts)] - seriālie porti + virtuālā roka."""
    from serial.tools import list_ports as lp
    ports = sorted(lp.comports(), key=lambda p: p.device)
    pairs = virtual_com_pairs(ports)
    arm_side = {b for _, b in pairs}
    app_side = dict(pairs)
    out = []
    for p in ports:
        if p.device in arm_side:
            continue                    # to izmanto pati virtuālā roka
        if p.device in app_side:
            out.append((p.device, f"{p.device} - Virtuālā roka (com0com ↔ {app_side[p.device]})"))
        else:
            out.append((p.device, f"{p.device} - {p.description}"))
    out.append((NETWORK_URL, "Tilts tīklā (robotroka.local) - vai ieraksti socket://IP:23"))
    out.append((VIRTUAL_URL, "Virtuālā roka (TCP localhost:5555)"))
    return out