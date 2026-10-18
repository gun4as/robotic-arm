"""Cilne "Konfigurācija": locītavu parametri, gala slēdži, HOME, satvērējs."""
from __future__ import annotations

import os

from PySide6.QtCore import QSettings, Qt, Signal
from PySide6.QtWidgets import (QCheckBox, QComboBox, QDoubleSpinBox, QFileDialog, QGridLayout, QGroupBox,
                               QHBoxLayout, QHeaderView, QLabel, QPushButton, QSpinBox, QTableWidget,
                               QVBoxLayout, QWidget)

from .config import GLOBAL_PARAMS, HW_PARAMS, JOINT_PARAMS, ControllerConfig
from .link import NUM_JOINTS, Link, Status
from . import dialogs
from .widgets import COLORS, AngleSpinBox, Led, error_text

CHANGED_STYLE = f"background:{COLORS['accent']}33;"


class ConfigTab(QWidget):
    message = Signal(str)
    log = Signal(str)

    def __init__(self, link: Link, cfg: ControllerConfig):
        super().__init__()
        self.link = link
        self.cfg = cfg
        self._loading = False

        root = QHBoxLayout(self)
        left = QVBoxLayout()

        self.table = QTableWidget(len(JOINT_PARAMS) + 1, NUM_JOINTS)
        self.table.setHorizontalHeaderLabels([f"J{j + 1}" for j in range(NUM_JOINTS)])
        self.table.setVerticalHeaderLabels(["Soļi uz grādu (aprēķināts)"] + [p[2] for p in JOINT_PARAMS])
        self.table.horizontalHeader().setSectionResizeMode(QHeaderView.Stretch)
        self.table.verticalHeader().setDefaultSectionSize(32)
        self.cells = {}   # (j, key) -> widget
        self.spd_labels = []
        for j in range(NUM_JOINTS):
            lab = QLabel("—")
            lab.setAlignment(Qt.AlignCenter)
            lab.setStyleSheet(f"color:{COLORS['dim']}")
            self.spd_labels.append(lab)
            self.table.setCellWidget(0, j, lab)
            for r, (key, _ck, _name, typ, lo, hi, dec) in enumerate(JOINT_PARAMS, start=1):
                w = self._make_editor(typ, lo, hi, dec)
                self.cells[(j, key)] = w
                self.table.setCellWidget(r, j, w)
        left.addWidget(self.table, 1)

        btns = QHBoxLayout()
        for txt, fn, tip, obj in (
            ("Nolasīt", self.read, "Nolasa pašreizējos iestatījumus no kontroliera", ""),
            ("Ierakstīt", lambda: self.write(save=False), "Nosūta mainītās vērtības (līdz restartam)", ""),
            ("Ierakstīt + SAVE", lambda: self.write(save=True), "Nosūta un saglabā kontroliera atmiņā", "primary"),
            ("Rūpnīcas iestatījumi", self.factory, "Noklusējumi no firmware config.h", ""),
            ("Eksportēt…", self.export_file, "Saglabāt iestatījumus failā", ""),
            ("Importēt…", self.import_file, "Ielādēt failu tabulā (pēc tam 'Ierakstīt')", ""),
        ):
            b = QPushButton(txt)
            b.setToolTip(tip)
            if obj:
                b.setObjectName(obj)
            b.clicked.connect(fn)
            btns.addWidget(b)
        btns.addStretch(1)
        left.addLayout(btns)
        hint = QLabel("Mainītās vērtības iekrāsotas. 'Ierakstīt' darbojas tikai, kad roka stāv. "
                      "Mainot motoru/mikrosoļus/reduktoru, pozīcija grādos paliek tā pati.")
        hint.setWordWrap(True)
        hint.setStyleSheet(f"color:{COLORS['dim']}")
        left.addWidget(hint)
        root.addLayout(left, 3)

        # ---------------------------------------------------------- labā puse
        right = QVBoxLayout()
        hw = QGroupBox("Aparatūra")
        hl = QGridLayout(hw)
        self.board_label = QLabel("—")
        self.board_label.setStyleSheet("font-weight:700")
        self.fw_label = QLabel("—")
        self.tmc_label = QLabel("—")
        for r, (name, w) in enumerate((("Plate:", self.board_label), ("Firmware:", self.fw_label),
                                       ("TMC2209 UART:", self.tmc_label))):
            hl.addWidget(QLabel(name), r, 0)
            hl.addWidget(w, r, 1)
        self.hwcells = {}
        for i, (key, _ck, name, *_r) in enumerate(HW_PARAMS):
            cb = QCheckBox(name)
            cb.toggled.connect(self._mark)
            self.hwcells[key] = cb
            hl.addWidget(cb, 3 + i, 0, 1, 2)
        hw.setToolTip("Plati nosaka pats firmware: RAMPS un SKR ir dažādi procesori, katram savs firmware.\n"
                      "Ekrānu ieslēdz tikai, ja tas tiešām pieslēgts (SKR bez ekrāna pogas pins 'peld').\n"
                      "Pēc izmaiņām spied 'Ierakstīt + SAVE'.")
        right.addWidget(hw)
        es = QGroupBox("Gala slēdžu pārbaude")
        el = QGridLayout(es)
        self.es_leds = []
        for j in range(NUM_JOINTS):
            led = Led(COLORS["bad"], 18)
            self.es_leds.append(led)
            el.addWidget(QLabel(f"J{j + 1}"), 0, j, Qt.AlignCenter)
            el.addWidget(led, 1, j, Qt.AlignCenter)
        es.setToolTip("HOME: roka brauc uz slēdža pusi, atkāpjas, lēnām pieiet vēlreiz un uzstāda\n"
                      "'Leņķis pie slēdža'. Secība: mazākais 'HOME secība' numurs pirmais.")
        t = QLabel("Nospied slēdzi ar roku - jāiedegas.\nPelēks = atslēgts. Deg pats - ieslēdz 'NC'.")
        t.setStyleSheet(f"color:{COLORS['dim']}")
        el.addWidget(t, 2, 0, 1, NUM_JOINTS)
        right.addWidget(es)

        gb = QGroupBox("Satvērēja servo")
        gl = QGridLayout(gb)
        self.gcells = {}
        for r, (key, _ck, name, typ, lo, hi, dec) in enumerate(GLOBAL_PARAMS):
            w = self._make_editor(typ, lo, hi, dec)
            self.gcells[key] = w
            gl.addWidget(QLabel(name), r, 0)
            gl.addWidget(w, r, 1)
            b = QPushButton("Pārbaudīt")
            b.setToolTip("Nosūta šo impulsu servo (SERVO us)")
            b.clicked.connect(lambda _=False, ww=w: self.link.send(f"SERVO {ww.value()}"))
            gl.addWidget(b, r, 2)
        right.addWidget(gb)

        tb = QGroupBox("Diagnostika")
        tl = QVBoxLayout(tb)
        for txt, cmd in (("TMC2209 draiveri (SKR)", "TMC"), ("TMC pārkonfigurēt", "TMC INIT"),
                         ("Ielādēt saglabāto (LOAD)", "LOAD")):
            b = QPushButton(txt)
            b.clicked.connect(lambda _=False, c=cmd: self._diag(c))
            tl.addWidget(b)
        right.addWidget(tb)
        right.addStretch(1)
        root.addLayout(right, 1)

        cfg.changed.connect(self._fill)
        cfg.info_changed.connect(self._fill_info)

    # ------------------------------------------------------------ redaktori
    def _make_editor(self, typ, lo, hi, dec):
        if typ == "bool":
            w = QCheckBox()
            w.setStyleSheet("margin-left:45%;")
            w.toggled.connect(self._mark)
        elif typ == "dir":
            w = QComboBox()
            w.addItem("− (negatīvā)", -1)
            w.addItem("+ (pozitīvā)", 1)
            w.currentIndexChanged.connect(self._mark)
        elif typ == "pow2":
            w = QComboBox()
            for v in (1, 2, 4, 8, 16, 32, 64, 128, 256):
                w.addItem(f"1/{v}" if v > 1 else "pilns solis", v)
            w.setToolTip("RAMPS: jāsakrīt ar jumperiem zem draivera (A4988 3 jumperi = 1/16).\n"
                         "SKR + TMC2209: firmware ieraksta draiverī pats.")
            w.currentIndexChanged.connect(self._mark)
        elif typ == "int":
            w = QSpinBox()
            w.setRange(int(lo), int(hi))
            w.valueChanged.connect(self._mark)
        else:
            w = AngleSpinBox(dec)        # pieņem gan komatu, gan punktu
            w.setSuffix("")
            w.setRange(lo, hi)
            w.valueChanged.connect(self._mark)
        return w

    @staticmethod
    def _get(w):
        if isinstance(w, QCheckBox):
            return 1 if w.isChecked() else 0
        if isinstance(w, QComboBox):
            return w.currentData()
        if isinstance(w, QSpinBox):
            return w.value()
        return round(w.value(), 3)

    @staticmethod
    def _set(w, v):
        if v is None:
            return
        if isinstance(w, QCheckBox):
            w.setChecked(bool(int(v)))
        elif isinstance(w, QComboBox):
            i = w.findData(int(v))
            if i >= 0:
                w.setCurrentIndex(i)
        elif isinstance(w, QSpinBox):
            w.setValue(int(v))
        else:
            w.setValue(float(v))

    def _fill(self) -> None:
        self._loading = True
        for j in range(NUM_JOINTS):
            c = self.cfg.joints[j]
            self.spd_labels[j].setText(f"{c.get('spd', 0):g}")
            for key, *_ in JOINT_PARAMS:
                self._set(self.cells[(j, key)], c.get(key))
            self.cells[(j, "current")].setEnabled(bool(self.cfg.globals.get("tmc")))
        for key, *_ in GLOBAL_PARAMS:
            self._set(self.gcells[key], self.cfg.globals.get(key))
        for key, *_ in HW_PARAMS:
            self._set(self.hwcells[key], self.cfg.globals.get(key, 0))
        self.tmc_label.setText("ir" if self.cfg.globals.get("tmc") else "nav (strāvu iestata ar Vref)")
        self._loading = False
        self._mark()

    def _fill_info(self) -> None:
        i = self.cfg.info
        self.board_label.setText(i.get("board", "—"))
        self.fw_label.setText(f"{i.get('name', '')} {i.get('version', '')}")

    def _values(self):
        joints = [{key: self._get(self.cells[(j, key)]) for key, *_ in JOINT_PARAMS} for j in range(NUM_JOINTS)]
        globals_ = {key: self._get(self.gcells[key]) for key, *_ in GLOBAL_PARAMS}
        globals_.update({key: self._get(self.hwcells[key]) for key, *_ in HW_PARAMS})
        return joints, globals_

    def _mark(self, *_) -> None:
        if self._loading:
            return
        joints, globals_ = self._values()
        for j in range(NUM_JOINTS):
            for key, *_ in JOINT_PARAMS:
                old = self.cfg.joints[j].get(key)
                changed = old is not None and abs(float(joints[j][key]) - float(old)) > 1e-6
                if key == "current" and not self.cfg.globals.get("tmc"):
                    changed = False          # RAMPS: strāvu iestata ar Vref, lauks atslēgts
                self.cells[(j, key)].setStyleSheet(CHANGED_STYLE if changed else "")
        for key, *_ in GLOBAL_PARAMS:
            changed = int(globals_[key]) != int(self.cfg.globals.get(key, globals_[key]))
            self.gcells[key].setStyleSheet(CHANGED_STYLE if changed else "")
        for key, *_ in HW_PARAMS:
            changed = int(globals_[key]) != int(self.cfg.globals.get(key, globals_[key]))
            self.hwcells[key].setStyleSheet(f"color:{COLORS['accent']}; font-weight:700" if changed else "")

    # ------------------------------------------------------------ darbības
    def read(self) -> None:
        self.cfg.refresh(lambda ok: None if ok else self.message.emit("Neizdevās nolasīt konfigurāciju"))

    def write(self, save: bool) -> None:
        if not self.link.connected:
            self.message.emit("Nav savienojuma")
            return
        joints, globals_ = self._values()
        for j, v in enumerate(joints):
            if v["min"] >= v["max"]:
                dialogs.inform(self, "Kļūda", f"J{j + 1}: min jābūt mazākam par max")
                return
        cmds = self.cfg.commands_for(joints, globals_)
        if save:
            cmds.append("SAVE")
        if not cmds:
            self.message.emit("Nav izmaiņu")
            return
        errors = []

        def cb(cmd):
            def f(r):
                if not r.ok:
                    errors.append(f"{cmd}: {error_text(r.code)}")
                self.log.emit(f"{cmd} -> {'ok' if r.ok else r.code}")
            return f

        for c in cmds:
            self.link.send(c, cb(c))

        def done(_ok):
            if errors:
                dialogs.inform(self, "Daļa iestatījumu netika ierakstīti", "\n".join(errors))
            else:
                self.message.emit("Saglabāts kontroliera atmiņā" if save else "Ierakstīts (līdz restartam - spied SAVE)")
        self.cfg.refresh(done)

    def factory(self) -> None:
        if not dialogs.ask(self, "Rūpnīcas iestatījumi",
                           "Atjaunot noklusējumus no firmware? Saglabātie iestatījumi paliks atmiņā, "
                           "kamēr nenospiedīsi SAVE.", "Atjaunot"):
            return
        self.link.send("FACTORY", lambda r: self.message.emit("Noklusējumi atjaunoti" if r.ok else error_text(r.code)))
        self.read()

    def _diag(self, cmd: str) -> None:
        def cb(r):
            for line in r.lines:
                self.log.emit(line)
            self.message.emit(f"{cmd}: " + ("skat. konsoli" if r.ok else error_text(r.code)))
            if cmd == "LOAD" and r.ok:
                self.read()
        self.link.send(cmd, cb)

    def export_file(self) -> None:
        d = QSettings().value("config/dir", os.path.expanduser("~"))
        path, _ = QFileDialog.getSaveFileName(self, "Eksportēt iestatījumus", os.path.join(d, "robotroka-config.json"),
                                              "JSON (*.json)")
        if path:
            with open(path, "w", encoding="utf-8") as f:
                f.write(self.cfg.to_json())
            QSettings().setValue("config/dir", os.path.dirname(path))

    def import_file(self) -> None:
        d = QSettings().value("config/dir", os.path.expanduser("~"))
        path, _ = QFileDialog.getOpenFileName(self, "Importēt iestatījumus", d, "JSON (*.json)")
        if not path:
            return
        try:
            with open(path, encoding="utf-8") as f:
                joints, globals_ = ControllerConfig.from_json(f.read())
        except Exception as e:  # noqa: BLE001
            dialogs.inform(self, "Kļūda", str(e))
            return
        for j in range(min(NUM_JOINTS, len(joints))):
            for key, *_ in JOINT_PARAMS:
                self._set(self.cells[(j, key)], joints[j].get(key))
        for key, *_ in GLOBAL_PARAMS:
            self._set(self.gcells[key], globals_.get(key))
        self.message.emit("Ielādēts tabulā - spied 'Ierakstīt + SAVE'")

    def update_status(self, st: Status) -> None:
        for j, led in enumerate(self.es_leds):
            ch = st.endstops[j] if j < len(st.endstops) else "-"
            led.set_state(None if ch == "-" else ch == "1")
