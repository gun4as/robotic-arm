"""Kontroliera konfigurācijas nolasīšana (CFG) un rakstīšana (CFG key n val)."""
from __future__ import annotations

import json
import re

from PySide6.QtCore import QObject, Signal

from .link import NUM_JOINTS, Link, Reply

# (atslēga CFG izdrukā, atslēga CFG komandā, nosaukums, tips, min, max, decimāles)
JOINT_PARAMS = [
    ("motor_steps", "MOTOR_STEPS", "Motora soļi/apgr. (200=1.8°, 400=0.9°)", "int", 1, 2000, 0),
    ("microsteps", "MICROSTEPS", "Mikrosoļi (RAMPS: kā jumperi)", "pow2", 1, 256, 0),
    ("gear", "GEAR", "Reduktors (piem. 9 = 9:1)", "float", 0.01, 1000, 3),
    ("min", "MIN", "Min leņķis, °", "float", -720, 720, 1),
    ("max", "MAX", "Max leņķis, °", "float", -720, 720, 1),
    ("speed", "SPEED", "Max ātrums, °/s", "float", 0.1, 1000, 1),
    ("accel", "ACCEL", "Paātrinājums, °/s²", "float", 0.1, 10000, 1),
    ("invert", "INVERT", "Invertēt virzienu", "bool", 0, 1, 0),
    ("current", "CURRENT", "Strāva (TMC), mA", "int", 100, 2000, 0),
    ("es", "ES", "Gala slēdzis ieslēgts", "bool", 0, 1, 0),
    ("es_inv", "ES_INV", "Slēdzis NC (normāli slēgts)", "bool", 0, 1, 0),
    ("home_dir", "HOME_DIR", "Slēdža puse", "dir", -1, 1, 0),
    ("home_pos", "HOME_POS", "Leņķis pie slēdža, °", "float", -720, 720, 2),
    ("home_speed", "HOME_SPEED", "HOME ātrums, °/s", "float", 0.1, 200, 1),
    ("backoff", "BACKOFF", "Atkāpšanās, °", "float", 0, 90, 1),
    ("home_order", "HOME_ORDER", "HOME secība", "int", 0, 255, 0),
]
GLOBAL_PARAMS = [
    ("grip_open", "GRIP_OPEN", "Satvērējs atvērts, µs", "int", 500, 2500, 0),
    ("grip_closed", "GRIP_CLOSED", "Satvērējs aizvērts, µs", "int", 500, 2500, 0),
]
# Kas pieslēgts kontrolierim (saglabājas ar SAVE)
HW_PARAMS = [
    ("lcd", "LCD", "Ekrāns (LCD + grozāmā poga) pieslēgts", "bool", 0, 1, 0),
    ("sd", "SD", "SD karte ekrānā pieslēgta", "bool", 0, 1, 0),
]
_KV = re.compile(r"(\w+)=(\S+)")
_INFO = re.compile(r"\[INFO\] (\S+) (\S+) board=(.*?) joints=(\d+) tmc=(\d)")


def _num(v: str):
    try:
        return int(v)
    except ValueError:
        try:
            return float(v)
        except ValueError:
            return v


class ControllerConfig(QObject):
    changed = Signal()
    info_changed = Signal()

    def __init__(self, link: Link):
        super().__init__()
        self.link = link
        self.joints = [dict(name=f"J{i + 1}", min=-180.0, max=180.0, speed=30.0, spd=0.0) for i in range(NUM_JOINTS)]
        self.globals = {"grip_open": 1000, "grip_closed": 2000, "tmc": 0}
        self.loaded = False
        self.info = {}   # name, version, board, joints, tmc

    def parse(self, lines) -> bool:
        got = 0
        for line in lines:
            m = re.match(r"\[CFG\] J(\d+) (.*)", line)
            if m:
                j = int(m.group(1)) - 1
                if 0 <= j < NUM_JOINTS:
                    for k, v in _KV.findall(m.group(2)):
                        self.joints[j][k] = v if k == "name" else _num(v)
                    got += 1
            elif line.startswith("[CFG] ") and "=" in line:
                for k, v in _KV.findall(line):
                    self.globals[k] = _num(v)
        if got:
            self.loaded = True
            self.changed.emit()
        return got == NUM_JOINTS

    def refresh_info(self) -> None:
        """INFO -> plate, firmware versija (kura plate pieslēgta, nosaka pats firmware)."""
        def cb(r: Reply):
            for line in r.lines:
                m = _INFO.search(line)
                if m:
                    self.info = dict(name=m.group(1), version=m.group(2), board=m.group(3),
                                     joints=int(m.group(4)), tmc=m.group(5) == "1")
                    self.info_changed.emit()
        self.link.send("INFO", cb)

    def refresh(self, done=None) -> None:
        self.refresh_info()

        def cb(r: Reply):
            ok = r.ok and self.parse(r.lines)
            if done:
                done(ok)
        self.link.send("CFG", cb)

    def commands_for(self, joints: list, globals_: dict) -> list:
        """CFG komandas tikai mainītajām vērtībām."""
        out = []
        for j, new in enumerate(joints):
            params = list(JOINT_PARAMS)
            # Ja diapazons pārbīdās uz augšu, MAX jāsūta pirms MIN (kontrolieris prasa min < max)
            if "max" in new and float(new["max"]) > float(self.joints[j].get("max", 0)):
                params.sort(key=lambda p: 0 if p[0] == "max" else 1)
            for key, ckey, _, typ, *_ in params:
                if key not in new:
                    continue
                if key == "current" and not self.globals.get("tmc"):
                    continue
                old = self.joints[j].get(key)
                if old is None or abs(float(new[key]) - float(old)) > 1e-6:
                    v = new[key]
                    out.append(f"CFG {ckey} {j + 1} {v:g}" if isinstance(v, float) else f"CFG {ckey} {j + 1} {int(v)}")
        for key, ckey, *_ in GLOBAL_PARAMS + HW_PARAMS:
            if key in globals_ and int(globals_[key]) != int(self.globals.get(key, -1)):
                out.append(f"CFG {ckey} {int(globals_[key])}")
        return out

    # Eksports/imports failā (lai var pārnest starp platēm)
    def to_json(self) -> str:
        keys = [p[0] for p in JOINT_PARAMS]
        return json.dumps({"format": "robotroka-config", "joints": [{k: j.get(k) for k in keys} for j in self.joints],
                           "globals": {p[0]: self.globals.get(p[0]) for p in GLOBAL_PARAMS}}, indent=1)

    @staticmethod
    def from_json(text: str):
        d = json.loads(text)
        if d.get("format") != "robotroka-config":
            raise ValueError("nav robotroka konfigurācijas fails")
        return d["joints"], d["globals"]
