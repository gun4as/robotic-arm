"""Cilne "Vadība": manuāla kustība, soļi, mērķi, nulle/HOME, satvērējs."""
from __future__ import annotations

from PySide6.QtCore import Qt, QTimer, Signal
from PySide6.QtWidgets import (QComboBox, QDoubleSpinBox, QGridLayout, QGroupBox, QHBoxLayout, QLabel,
                               QPushButton, QSlider, QVBoxLayout, QWidget)

from .config import ControllerConfig
from .link import NUM_JOINTS, Link, Status
from . import dialogs
from .widgets import COLORS, AngleSpinBox, HoldButton, Led, PositionBar, mono_font

JOG_PERIOD_MS = 100   # kontroliera sargsuns ir 300 ms


class ControlTab(QWidget):
    message = Signal(str)

    def __init__(self, link: Link, cfg: ControllerConfig):
        super().__init__()
        self.link = link
        self.cfg = cfg
        self._jog_vec = None
        self._jog_timer = QTimer(self)
        self._jog_timer.timeout.connect(self._jog_tick)

        root = QHBoxLayout(self)

        # ---------------------------------------------------------- locītavas
        jbox = QGroupBox("Locītavas")
        grid = QGridLayout(jbox)
        grid.setHorizontalSpacing(8)
        heads = ["", "Pozīcija", "Stāvoklis", "Nulle", "Slēdzis", "Kustināt (turēt)", "Solis", "Mērķis", "", ""]
        for c, h in enumerate(heads):
            lab = QLabel(h)
            lab.setStyleSheet(f"color:{COLORS['dim']}")
            grid.addWidget(lab, 0, c)
        self.pos_labels, self.zero_leds, self.es_leds, self.targets, self.name_labels = [], [], [], [], []
        self.bars = []
        for j in range(NUM_JOINTS):
            r = j + 1
            name = QLabel(f"J{j + 1}")
            name.setTextFormat(Qt.RichText)
            self.name_labels.append(name)
            grid.addWidget(name, r, 0)

            pos = QLabel("0.00°")
            pos.setFont(mono_font(16, True))
            pos.setMinimumWidth(120)
            pos.setAlignment(Qt.AlignRight | Qt.AlignVCenter)
            self.pos_labels.append(pos)
            grid.addWidget(pos, r, 1)

            bar = PositionBar()
            bar.target_clicked.connect(lambda v, jj=j: self.targets[jj].setValue(v))
            self.bars.append(bar)
            grid.addWidget(bar, r, 2)

            zl = Led(COLORS["ok"])
            zl.setToolTip("Zaļš = nulle uzstādīta (pozīcija zināma)")
            self.zero_leds.append(zl)
            grid.addWidget(zl, r, 3, Qt.AlignCenter)
            el = Led(COLORS["bad"])
            el.setToolTip("Pelēks = slēdzis atslēgts, tumšs = brīvs, sarkans = nospiests")
            self.es_leds.append(el)
            grid.addWidget(el, r, 4, Qt.AlignCenter)

            jog = QHBoxLayout()
            for sign, txt in ((-1, "◀"), (1, "▶")):
                b = HoldButton(txt)
                b.held.connect(lambda down, jj=j, s=sign: self._jog(jj, s, down))
                jog.addWidget(b)
            grid.addLayout(jog, r, 5)

            stp = QHBoxLayout()
            for sign, txt in ((-1, "−"), (1, "+")):
                b = QPushButton(txt)
                b.setObjectName("jog")
                b.clicked.connect(lambda _=False, jj=j, s=sign: self._step(jj, s))
                stp.addWidget(b)
            grid.addLayout(stp, r, 6)

            t = AngleSpinBox()
            t.setSingleStep(1)
            t.setMinimumWidth(95)
            t.valueChanged.connect(lambda _v, jj=j: self._check_target(jj))
            t.valueChanged.connect(lambda v, jj=j: self.bars[jj].set_target(v))
            self.targets.append(t)
            grid.addWidget(t, r, 7)

            go = QPushButton("Iet")
            go.setToolTip("MOVEJ uz mērķi")
            go.clicked.connect(lambda _=False, jj=j: self._go([jj]))
            grid.addWidget(go, r, 8)

            more = QHBoxLayout()
            z = QPushButton("Nulle")
            z.setToolTip("Pašreizējā pozīcija = 0°")
            z.clicked.connect(lambda _=False, jj=j: self._send(f"ZERO {jj + 1}"))
            h = QPushButton("HOME")
            h.setToolTip("Atrast gala slēdzi")
            h.clicked.connect(lambda _=False, jj=j: self._send(f"HOME {jj + 1}"))
            more.addWidget(z)
            more.addWidget(h)
            grid.addLayout(more, r, 9)

        foot = QHBoxLayout()
        foot.addWidget(QLabel("Solis:"))
        self.step_size = QComboBox()
        for v in ("0.1", "0.5", "1", "5", "10", "45", "90"):
            self.step_size.addItem(f"{v}°", float(v))
        self.step_size.setCurrentIndex(3)
        foot.addWidget(self.step_size)
        foot.addStretch(1)
        b = QPushButton("Mērķi = pašreizējā pozīcija")
        b.clicked.connect(self._targets_from_pos)
        foot.addWidget(b)
        b = QPushButton("Iet uz visiem mērķiem")
        b.setObjectName("primary")
        b.clicked.connect(lambda: self._go(list(range(NUM_JOINTS))))
        foot.addWidget(b)
        grid.addLayout(foot, NUM_JOINTS + 1, 0, 1, len(heads))
        grid.setRowStretch(NUM_JOINTS + 2, 1)          # rindas augšā, brīvā vieta apakšā
        grid.setColumnStretch(2, 1)                    # josla aizņem brīvo platumu
        root.addWidget(jbox, 3)

        # ---------------------------------------------------------- labā puse
        side = QVBoxLayout()
        sbox = QGroupBox("Ātrums")
        sl = QVBoxLayout(sbox)
        self.speed_label = QLabel("50 %")
        self.speed_label.setFont(mono_font(14, True))
        self.speed_slider = QSlider(Qt.Horizontal)
        self.speed_slider.setRange(1, 100)
        self.speed_slider.setValue(50)
        self.speed_slider.valueChanged.connect(lambda v: self.speed_label.setText(f"{v} %"))
        sl.addWidget(self.speed_label)
        sl.addWidget(self.speed_slider)
        hint = QLabel("Turot bultiņas: % no locītavas max ātruma.\nKustībām (MOVE): S parametrs.")
        hint.setStyleSheet(f"color:{COLORS['dim']}")
        sl.addWidget(hint)
        side.addWidget(sbox)

        mbox = QGroupBox("Roka")
        ml = QGridLayout(mbox)
        btns = [
            ("Ieslēgt motorus", "ENABLE"), ("Atslēgt motorus", "DISABLE"),
            ("Nulle visām", "ZERO"), ("HOME visām", "HOME"),
            ("Apturēt (STOP)", "STOP"), ("RESET", "RESET"),
        ]
        for i, (txt, cmd) in enumerate(btns):
            b = QPushButton(txt)
            b.clicked.connect(lambda _=False, c=cmd: self._confirm_send(c))
            ml.addWidget(b, i // 2, i % 2)
        side.addWidget(mbox)

        gbox = QGroupBox("Satvērējs")
        gl = QVBoxLayout(gbox)
        self.grip_label = QLabel("0 %")
        self.grip_label.setFont(mono_font(14, True))
        self.grip_slider = QSlider(Qt.Horizontal)
        self.grip_slider.setRange(0, 100)
        self._grip_timer = QTimer(self)
        self._grip_timer.setSingleShot(True)
        self._grip_timer.timeout.connect(lambda: self._send(f"GRIP {self.grip_slider.value()}", quiet=True))
        self.grip_slider.valueChanged.connect(self._grip_changed)
        gl.addWidget(self.grip_label)
        gl.addWidget(self.grip_slider)
        row = QHBoxLayout()
        for txt, v in (("Atvērt", 0), ("50%", 50), ("Aizvērt", 100)):
            b = QPushButton(txt)
            b.clicked.connect(lambda _=False, val=v: self.grip_slider.setValue(val))
            row.addWidget(b)
        gl.addLayout(row)
        side.addWidget(gbox)
        side.addStretch(1)
        root.addLayout(side, 1)

        cfg.changed.connect(self._apply_config)

    # ------------------------------------------------------------ palīgi
    def speed(self) -> int:
        return self.speed_slider.value()

    def _send(self, cmd: str, quiet: bool = False) -> None:
        def cb(r):
            if not r.ok and not quiet:
                self.message.emit(f"{cmd}: {r.code}")
            elif not r.ok:
                self.message.emit(r.code)
        self.link.send(cmd, cb)

    def _confirm_send(self, cmd: str) -> None:
        texts = {"DISABLE": ("Atslēgt motorus", "Roka var nokrist no sava svara.", "Atslēgt"),
                 "ZERO": ("Nulle visām", "Pašreizējā pozīcija kļūs par 0° visām locītavām.", "Uzstādīt")}
        if cmd in texts and not dialogs.ask(self, *texts[cmd]):
            return
        self._send(cmd)

    def _jog(self, j: int, sign: int, down: bool) -> None:
        if down:
            self._jog_vec = [0.0] * NUM_JOINTS
            self._jog_vec[j] = sign * self.speed()
            self._jog_tick()
            self._jog_timer.start(JOG_PERIOD_MS)
        else:
            self.stop_jog()

    def _jog_tick(self) -> None:
        if self._jog_vec and self.link.connected:
            self.link.send_jog(self._jog_vec)

    def stop_jog(self) -> None:
        if self._jog_vec is not None:
            self._jog_timer.stop()
            self._jog_vec = None
            self.link.send_jog([0] * NUM_JOINTS)

    def _step(self, j: int, sign: int) -> None:
        vals = ["_"] * NUM_JOINTS
        vals[j] = f"{sign * self.step_size.currentData():g}"
        self._send("REL " + " ".join(vals) + f" S{self.speed()}")

    def _targets_from_pos(self) -> None:
        for t, p in zip(self.targets, self.link.status.pos):
            t.setValue(p)

    def _grip_changed(self, v: int) -> None:
        self.grip_label.setText(f"{v} %")
        if not self._syncing:          # lietotājs, nevis statusa sinhronizācija
            self._grip_timer.start(80)

    _syncing = False

    def _limits(self, j: int):
        c = self.cfg.joints[j]
        return float(c.get("min", -180)), float(c.get("max", 180))

    def _check_target(self, j: int) -> bool:
        return self.targets[j].set_limits_hint(*self._limits(j))

    def _go(self, joints: list) -> None:
        bad = [j for j in joints if not self._check_target(j)]
        if bad:
            j = bad[0]
            lo, hi = self._limits(j)
            self.message.emit(f"J{j + 1} mērķis {self.targets[j].value():g}° ir ārpus limita {lo:g}…{hi:g}°. "
                              "Limitus maina Konfigurācijā (Min/Max leņķis).")
            return
        if len(joints) == 1:
            j = joints[0]
            self._send(f"MOVEJ {j + 1} {self.targets[j].value():.2f} S{self.speed()}")
        else:
            self._send("MOVE " + " ".join(f"{t.value():.2f}" for t in self.targets) + f" S{self.speed()}")

    def _apply_config(self) -> None:
        for j in range(NUM_JOINTS):
            c = self.cfg.joints[j]
            lo, hi = self._limits(j)
            self.name_labels[j].setText(
                f"<span style='font-family:Consolas; font-size:12pt; font-weight:700'>{c.get('name', f'J{j + 1}')}</span>"
                f"<br><span style='color:{COLORS['dim']}; font-size:8pt'>{lo:g}…{hi:g}°</span>")
            self._check_target(j)
            self.bars[j].set_range(lo, hi)

    def update_status(self, st: Status) -> None:
        for j in range(NUM_JOINTS):
            self.pos_labels[j].setText(f"{st.pos[j]:.2f}°")
            self.zero_leds[j].set_state(bool(st.zeroed[j]))
            self.bars[j].set_state(st.pos[j], bool(st.zeroed[j]))
            ch = st.endstops[j] if j < len(st.endstops) else "-"
            self.es_leds[j].set_state(None if ch == "-" else ch == "1")
        if not self.grip_slider.isSliderDown() and not self._grip_timer.isActive():
            self._syncing = True
            self.grip_slider.setValue(st.grip)
            self._syncing = False
