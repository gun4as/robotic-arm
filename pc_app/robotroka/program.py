"""Programmas (darbību secības) modelis un izpildītājs.

Solis var būt:
  move  - pārvietoties uz locītavu leņķiem (None = locītavu neaiztikt), ātrums %
  grip  - satvērējs 0..100 %
  wait  - pauze ms
  home  - HOME (0 = visas ar gala slēdzi, 1..5 = viena)
  cmd   - jebkura kontroliera komanda (teksts)
Fails: JSON (*.rrprog).
"""
from __future__ import annotations

import json
from dataclasses import asdict, dataclass, field
from typing import Optional

from PySide6.QtCore import QObject, QTimer, Signal

from .link import NUM_JOINTS, Link, Reply

STEP_TYPES = {
    "move": "Kustība",
    "grip": "Satvērējs",
    "wait": "Pauze",
    "home": "HOME",
    "cmd": "Komanda",
}
FILE_VERSION = 1


@dataclass
class Step:
    type: str = "move"
    joints: list = field(default_factory=lambda: [None] * NUM_JOINTS)   # grādi vai None
    speed: int = 50          # move: %
    value: float = 0         # grip: %, wait: ms, home: locītava (0 = visas)
    text: str = ""           # cmd
    comment: str = ""

    def command(self) -> Optional[str]:
        """Kontroliera komanda vai None (wait)."""
        if self.type == "move":
            vals = ["_" if v is None else f"{v:.2f}" for v in self.joints]
            return "MOVE " + " ".join(vals) + f" S{int(self.speed)}"
        if self.type == "grip":
            return f"GRIP {self.value:g}"
        if self.type == "home":
            return "HOME" if int(self.value) == 0 else f"HOME {int(self.value)}"
        if self.type == "cmd":
            return self.text.strip() or None
        return None

    def summary(self) -> str:
        if self.type == "move":
            js = "  ".join(f"J{i + 1}={'—' if v is None else f'{v:.1f}°'}" for i, v in enumerate(self.joints))
            return f"{js}   ātrums {int(self.speed)}%"
        if self.type == "grip":
            return f"{self.value:g}%  ({'atvērts' if self.value <= 0 else 'aizvērts' if self.value >= 100 else 'daļēji'})"
        if self.type == "wait":
            return f"{int(self.value)} ms"
        if self.type == "home":
            return "visas locītavas" if int(self.value) == 0 else f"J{int(self.value)}"
        return self.text

    def waits_for_motion(self) -> bool:
        if self.type in ("move", "home"):
            return True
        if self.type == "cmd":
            head = self.text.strip().split(" ")[0].upper()
            return head in ("MOVE", "MOVEJ", "REL", "HOME")
        return False

    @staticmethod
    def from_dict(d: dict) -> "Step":
        s = Step()
        s.type = d.get("type", "move") if d.get("type") in STEP_TYPES else "cmd"
        j = list(d.get("joints", [None] * NUM_JOINTS))[:NUM_JOINTS]
        s.joints = [None if v is None else float(v) for v in j] + [None] * (NUM_JOINTS - len(j))
        s.speed = int(d.get("speed", 50))
        s.value = float(d.get("value", 0))
        s.text = str(d.get("text", ""))
        s.comment = str(d.get("comment", ""))
        return s


@dataclass
class Program:
    steps: list = field(default_factory=list)
    repeat: int = 1          # 0 = bezgalīgi

    def to_json(self) -> str:
        return json.dumps({"format": "robotroka-program", "version": FILE_VERSION, "repeat": self.repeat,
                           "steps": [asdict(s) for s in self.steps]}, ensure_ascii=False, indent=1)

    @staticmethod
    def from_json(text: str) -> "Program":
        d = json.loads(text)
        if isinstance(d, list):          # web lapas eksports: [{pos, grip, speed}]
            p = Program()
            for pt in d:
                p.steps.append(Step(type="move", joints=list(pt["pos"])[:NUM_JOINTS], speed=int(pt.get("speed", 50))))
                p.steps.append(Step(type="grip", value=float(pt.get("grip", 0))))
            return p
        if d.get("format") != "robotroka-program":
            raise ValueError("nav robotroka programmas fails")
        return Program(steps=[Step.from_dict(s) for s in d.get("steps", [])], repeat=int(d.get("repeat", 1)))

    def to_text(self) -> str:
        """Komandu saraksts (var sūtīt ar jebkuru seriālo termināli)."""
        out = ["; RobotRoka programma"]
        for i, s in enumerate(self.steps, 1):
            c = s.command()
            note = f"  ; {s.comment}" if s.comment else ""
            out.append(f"WAIT {int(s.value)}{note}" if s.type == "wait" else f"{c}{note}")
        return "\n".join(out) + "\n"

    @staticmethod
    def from_text(text: str) -> "Program":
        p = Program()
        for raw in text.splitlines():
            line, _, comment = raw.partition(";")
            line = line.strip()
            if not line:
                continue
            head = line.split()[0].upper()
            if head == "WAIT":
                p.steps.append(Step(type="wait", value=float(line.split()[1]), comment=comment.strip()))
            else:
                p.steps.append(Step(type="cmd", text=line, comment=comment.strip()))
        return p


class Runner(QObject):
    """Izpilda programmu pa solim: sūta komandu, gaida atbildi un (kustībām)
    gaida, kamēr kontroliera statuss pēc atbildes rāda Idle ar tukšu rindu."""

    step_started = Signal(int)
    finished = Signal(bool, str)        # veiksmīgi?, ziņa
    state_changed = Signal(str)         # "running" / "paused" / "idle"

    def __init__(self, link: Link):
        super().__init__()
        self.link = link
        self.program: Optional[Program] = None
        self.index = 0
        self.loop_no = 0
        self.state = "idle"
        self._wait_seq: Optional[int] = None    # gaidām Idle statusu ar seq > šo
        self._pause_requested = False
        self._single = False
        self._timer = QTimer(self)
        self._timer.setSingleShot(True)
        self._timer.timeout.connect(self._step_done)
        self._token = 0                          # novecojušu atbilžu ignorēšanai
        link.status_changed.connect(self._on_status)

    # ------------------------------------------------------------- vadība
    def start(self, program: Program, start_index: int = 0, single_step: bool = False) -> None:
        if self.state == "running" or not program.steps:
            return
        self.program = program
        self.index = max(0, min(start_index, len(program.steps) - 1))
        self.loop_no = 1
        self._single = single_step
        self._pause_requested = False
        self._set_state("running")
        self._run_current()

    def pause(self) -> None:
        if self.state == "running":
            self._pause_requested = True     # pabeidz pašreizējo soli, tad apstājas

    def resume(self, single_step: bool = False) -> None:
        if self.state == "paused":
            self._single = single_step
            self._pause_requested = False
            self._set_state("running")
            self._run_current()

    def stop(self, send_stop: bool = True) -> None:
        if self.state == "idle":
            return
        self._token += 1
        self._timer.stop()
        self._wait_seq = None
        if send_stop:
            self.link.send("STOP")
        self._set_state("idle")
        self.finished.emit(False, "Programma apturēta")

    # ------------------------------------------------------------- iekšēji
    def _set_state(self, s: str) -> None:
        self.state = s
        self.state_changed.emit(s)

    def _run_current(self) -> None:
        step: Step = self.program.steps[self.index]
        self.step_started.emit(self.index)
        self._token += 1
        token = self._token
        if step.type == "wait":
            self._timer.start(max(0, int(step.value)))
            return
        cmd = step.command()
        if not cmd:
            self._step_done()
            return
        self.link.send(cmd, lambda r, t=token, s=step: self._on_reply(r, t, s))

    def _on_reply(self, r: Reply, token: int, step: Step) -> None:
        if token != self._token or self.state != "running":
            return
        if not r.ok:
            missing = []
            if r.code == "not_zeroed" and step.type == "move":
                missing = [f"J{j + 1}" for j, v in enumerate(step.joints)
                           if v is not None and not self.link.status.zeroed[j]]
            if missing:
                self._fail(f"Solis {self.index + 1}: {', '.join(missing)} nav nulles - solī noņem "
                           f"šo locītavu ķeksīšus vai Vadībā uzstādi nulli (ZERO/HOME)")
            else:
                self._fail(f"Solis {self.index + 1}: kļūda '{r.code}'")
            return
        if step.waits_for_motion():
            self._wait_seq = r.status_seq
            self._on_status(self.link.status)
        else:
            self._step_done()

    def _on_status(self, st) -> None:
        if self._wait_seq is None or self.state != "running":
            return
        if st.state == "Alarm":
            self._wait_seq = None
            self._fail(f"Solis {self.index + 1}: kontrolieris Alarm stāvoklī")
            return
        if st.seq > self._wait_seq and st.idle:
            self._wait_seq = None
            self._step_done()

    def _step_done(self) -> None:
        if self.state != "running":
            return
        self.index += 1
        if self.index >= len(self.program.steps):
            rep = self.program.repeat
            if rep == 0 or self.loop_no < rep:
                self.loop_no += 1
                self.index = 0
            else:
                self._set_state("idle")
                self.finished.emit(True, f"Programma pabeigta ({self.loop_no}x)")
                return
        if self._pause_requested or self._single:
            self._pause_requested = False
            self._set_state("paused")
            self.step_started.emit(self.index)
            return
        self._run_current()

    def _fail(self, msg: str) -> None:
        self._token += 1
        self._timer.stop()
        self._set_state("idle")
        self.finished.emit(False, msg)
