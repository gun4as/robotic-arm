"""Cilne "Pults": pults dati tiešraidē un funkciju piešķiršana (glabājas tiltā).

Strādā tikai, ja PC programma pieslēgta caur tīklu (socket://<tilts>:23) - pults dati iet caur tiltu.
Tilta komandas: #rcwatch (tiešraide ~3 s), #rcmap (nolasīt), #rcmap_set ... (saglabāt), #rcmap_def.
"""
from __future__ import annotations

import time

from PySide6.QtCore import Qt, QTimer, Signal
from PySide6.QtWidgets import (QCheckBox, QComboBox, QDoubleSpinBox, QGridLayout, QGroupBox, QHBoxLayout, QLabel,
                               QPushButton, QSpinBox, QVBoxLayout, QWidget)

from . import dialogs
from .link import Link
from .widgets import COLORS, PositionBar

CHANNELS = ["labais ↔", "labais ↕", "kreisais ↕", "kreisais ↔", "potenciometrs"]
TRIM_TO = ["—", "J1", "J2", "J3", "J4", "J5", "satvērējs"]
SWITCHES = ["G ARM", "F avārija", "A J4/J5", "B", "D"]
SPEED = ["lēni", "vidēji", "ātri"]
MODE = ["vadība", "mācīšana", "atskaņošana"]


def parse_kv(line: str) -> dict:
    return dict(tok.split("=", 1) for tok in line.split()[1:] if "=" in tok)


def parse_pairs(v: str) -> list:
    return [tuple(int(x) for x in p.split(":")) for p in v.split(",")]


class Chip(QLabel):
    def __init__(self, text: str):
        super().__init__(text)
        self.setAlignment(Qt.AlignCenter)
        self.setMinimumWidth(44)
        self.set_on(False)

    def set_on(self, on: bool) -> None:
        if on:
            self.setStyleSheet(f"background:{COLORS['ok']}; color:#000; border-radius:5px; padding:3px 6px; font-weight:600;")
        else:
            self.setStyleSheet(f"color:{COLORS['dim']}; border:1px solid {COLORS['line']}; border-radius:5px; padding:3px 6px;")


class RemoteTab(QWidget):
    message = Signal(str)

    def __init__(self, link: Link):
        super().__init__()
        self.link = link
        self.last_raw = 0.0
        self.watch_since = 0.0
        root = QHBoxLayout(self)

        # ---------------------------------------------------------- tiešraide
        live = QGroupBox("Tiešraidē")
        lv = QVBoxLayout(live)
        self.state_label = QLabel("—")
        self.state_label.setWordWrap(True)
        lv.addWidget(self.state_label)
        grid = QGridLayout()
        self.bars, self.values = [], []
        for i, name in enumerate(CHANNELS):
            bar = PositionBar()
            bar.set_range(-1000, 1000)
            bar.unit = ""
            bar.setToolTip("")
            bar.setCursor(Qt.ArrowCursor)
            self.bars.append(bar)
            val = QLabel("—")
            val.setMinimumWidth(48)
            val.setAlignment(Qt.AlignRight | Qt.AlignVCenter)
            self.values.append(val)
            grid.addWidget(QLabel(name), i, 0)
            grid.addWidget(bar, i, 1)
            grid.addWidget(val, i, 2)
        grid.setColumnStretch(1, 1)
        lv.addLayout(grid)

        lv.addWidget(self._dim("Pogas (nospiežot iedegas)"))
        self.btn_chips = [Chip("H (Shift)" if i == 12 else f"P{i}") for i in range(13)]
        lv.addLayout(self._chip_rows(self.btn_chips, 7))
        lv.addWidget(self._dim("Slēdži"))
        self.sw_chips = [Chip(n) for n in SWITCHES]
        self.speed_chip, self.mode_chip = Chip("E ātrums —"), Chip("C režīms —")
        lv.addLayout(self._chip_rows(self.sw_chips + [self.speed_chip, self.mode_chip], 4))
        lv.addWidget(self._dim("Nospied pults trima pogu - labajā tabulā iezīmēsies tās rinda."))
        lv.addStretch(1)
        root.addWidget(live, 2)

        # ---------------------------------------------------------- iestatījumi
        cfg = QGroupBox("Funkciju piešķiršana (glabājas tiltā)")
        cv = QVBoxLayout(cfg)
        tg = QGridLayout()
        tg.addWidget(self._dim("poga"), 0, 0)
        tg.addWidget(self._dim("ko dara"), 0, 1)
        tg.addWidget(self._dim("virziens"), 0, 2)
        self.trim_labels, self.trim_joint, self.trim_dir = [], [], []
        for i in range(12):
            lab = QLabel(f"P{i}")
            joint = QComboBox()
            for k, t in enumerate(TRIM_TO):
                joint.addItem(t, k)
            d = QComboBox()
            d.addItem("−", -1)
            d.addItem("+", 1)
            self.trim_labels.append(lab)
            self.trim_joint.append(joint)
            self.trim_dir.append(d)
            r, c = i % 6, (i // 6) * 3
            tg.addWidget(lab, r + 1, c)
            tg.addWidget(joint, r + 1, c + 1)
            tg.addWidget(d, r + 1, c + 2)
        cv.addLayout(tg)
        cv.addWidget(self._dim("Turot H (Shift): trims \"+\" = nulle šai locītavai, \"−\" = HOME šai locītavai. "
                               "Tikai H 2 s = HOME visām (avārijas stāvoklī - RESET)."))

        sg = QGridLayout()
        sg.addWidget(self._dim("A izslēgts"), 0, 1)
        sg.addWidget(self._dim("A ieslēgts"), 0, 3)
        self.stick = [[None] * 5, [None] * 5]
        self.stick_inv = [[None] * 5, [None] * 5]
        for j in range(5):
            sg.addWidget(QLabel(f"J{j + 1}"), j + 1, 0)
            for b in range(2):
                combo = QComboBox()
                combo.addItem("—", -1)
                for k, n in enumerate(CHANNELS):
                    combo.addItem(n, k)
                inv = QCheckBox("apgriezt")
                self.stick[b][j] = combo
                self.stick_inv[b][j] = inv
                sg.addWidget(combo, j + 1, 1 + b * 2)
                sg.addWidget(inv, j + 1, 2 + b * 2)
        cv.addWidget(self._dim("Stiki"))
        cv.addLayout(sg)

        og = QGridLayout()
        og.addWidget(QLabel("Ātrums (slēdzis E), %"), 0, 0)
        self.speed = []
        for k in range(3):
            sp = QSpinBox()
            sp.setRange(1, 100)
            sp.setToolTip(SPEED[k])
            self.speed.append(sp)
            og.addWidget(sp, 0, 1 + k)
        og.addWidget(QLabel("Trima solis, °"), 1, 0)
        self.step = QDoubleSpinBox()
        self.step.setRange(0.1, 20.0)
        self.step.setSingleStep(0.1)
        self.step.setDecimals(1)
        og.addWidget(self.step, 1, 1)
        og.addWidget(QLabel("Satvērēja pogu solis, %"), 2, 0)
        self.grip = QSpinBox()
        self.grip.setRange(1, 50)
        og.addWidget(self.grip, 2, 1)
        cv.addLayout(og)

        row = QHBoxLayout()
        b = QPushButton("Nolasīt no tilta")
        b.clicked.connect(lambda: self.link.send_bridge("#rcmap"))
        row.addWidget(b)
        b = QPushButton("Saglabāt")
        b.setObjectName("primary")
        b.clicked.connect(self.save)
        row.addWidget(b)
        b = QPushButton("Noklusējumi")
        b.clicked.connect(self.defaults)
        row.addWidget(b)
        row.addStretch(1)
        cv.addLayout(row)
        cv.addStretch(1)
        root.addWidget(cfg, 3)

        self.timer = QTimer(self)
        self.timer.timeout.connect(self._tick)
        self.timer.start(1000)
        link.bridge_line.connect(self.on_bridge_line)

    # ------------------------------------------------------------ palīgi
    def _dim(self, text: str) -> QLabel:
        lab = QLabel(text)
        lab.setWordWrap(True)
        lab.setStyleSheet(f"color:{COLORS['dim']}")
        return lab

    def _chip_rows(self, chips, per_row):
        g = QGridLayout()
        for i, c in enumerate(chips):
            g.addWidget(c, i // per_row, i % per_row)
        return g

    # ------------------------------------------------------------ tilts
    def _tick(self) -> None:
        if not self.isVisible() or not self.link.connected:
            self.watch_since = 0.0
            return
        if not self.watch_since:
            self.watch_since = time.monotonic()
            self.link.send_bridge("#rcmap")
        self.link.send_bridge("#rcwatch")
        if time.monotonic() - self.last_raw > 3 and time.monotonic() - self.watch_since > 3:
            self.state_label.setText("Nav pults datu. Tie pieejami tikai, ja PC programma pieslēgta caur tīklu "
                                     "(\"Tilts tīklā\" / socket://<tilta IP>:23), nevis tieši pa USB.")

    def on_bridge_line(self, line: str) -> None:
        if line.startswith("#rcraw"):
            self.show_raw(parse_kv(line))
        elif line.startswith("#rcmap "):
            self.show_map(parse_kv(line))
        elif line == "#ok rcmap":
            self.message.emit("Pults iestatījumi saglabāti tiltā")
        elif line == "#err rcmap":
            self.message.emit("Tilts noraidīja pults iestatījumus")

    def show_raw(self, kv: dict) -> None:
        self.last_raw = time.monotonic()
        if kv.get("link") != "1":
            self.state_label.setText("Pults nav savienota ar tiltu")
            return
        bat = int(kv.get("bat", 0)) / 1000
        self.state_label.setText(f"Pults savienota · {bat:.1f} V" + (" · ARM" if kv.get("armed") == "1" else ""))
        for i, v in enumerate(int(x) for x in kv["ch"].split(",")[:5]):
            self.bars[i].set_state(v, True)
            self.values[i].setText(str(v))
        btn, sw = int(kv.get("btn", 0)), int(kv.get("sw", 0))
        for i, c in enumerate(self.btn_chips):
            on = bool(btn & (1 << i))
            c.set_on(on)
            if i < 12:
                self.trim_labels[i].setStyleSheet(
                    f"background:{COLORS['ok']}; color:#000; font-weight:700; border-radius:4px; padding:0 4px;" if on else "")
        for i, c in enumerate(self.sw_chips):
            c.set_on(bool(sw & (1 << i)))
        s3 = [int(x) for x in kv.get("s3", "1,1").split(",")]
        self.speed_chip.setText("E ātrums " + SPEED[min(s3[0], 2)])
        self.mode_chip.setText("C režīms " + MODE[min(s3[1], 2)])

    def show_map(self, kv: dict) -> None:
        for i, (j, d) in enumerate(parse_pairs(kv["t"])[:12]):
            self.trim_joint[i].setCurrentIndex(self.trim_joint[i].findData(j))
            self.trim_dir[i].setCurrentIndex(self.trim_dir[i].findData(-1 if d < 0 else 1))
        for b, key in enumerate(("a", "b")):
            for j, (ch, d) in enumerate(parse_pairs(kv[key])[:5]):
                self.stick[b][j].setCurrentIndex(self.stick[b][j].findData(ch))
                self.stick_inv[b][j].setChecked(d < 0)
        for k, v in enumerate(kv["s"].split(",")[:3]):
            self.speed[k].setValue(int(v))
        self.step.setValue(int(kv["st"]) / 10)
        self.grip.setValue(int(kv["g"]))

    def map_line(self) -> str:
        t = ",".join(f"{self.trim_joint[i].currentData()}:{self.trim_dir[i].currentData()}" for i in range(12))
        banks = [",".join(f"{self.stick[b][j].currentData()}:{-1 if self.stick_inv[b][j].isChecked() else 1}"
                          for j in range(5)) for b in range(2)]
        s = ",".join(str(sp.value()) for sp in self.speed)
        return f"#rcmap_set t={t} a={banks[0]} b={banks[1]} s={s} st={round(self.step.value() * 10)} g={self.grip.value()}"

    def save(self) -> None:
        if not self.link.connected:
            self.message.emit("Nav savienojuma")
            return
        self.link.send_bridge(self.map_line())

    def defaults(self) -> None:
        if dialogs.ask(self, "Pults noklusējumi", "Atjaunot pults funkciju piešķiršanu no programmas noklusējumiem?",
                       "Atjaunot"):
            self.link.send_bridge("#rcmap_def")
