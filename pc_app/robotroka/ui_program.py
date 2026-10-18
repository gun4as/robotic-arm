"""Cilne "Programma": darbību secības sastādīšana, mācīšana un izpilde."""
from __future__ import annotations

import copy
import os

from PySide6.QtCore import QSettings, Qt, Signal
from PySide6.QtGui import QBrush, QColor
from PySide6.QtWidgets import (QAbstractItemView, QCheckBox, QComboBox, QDoubleSpinBox, QFileDialog, QFormLayout,
                               QGridLayout, QGroupBox, QHBoxLayout, QHeaderView, QLabel, QLineEdit,
                               QPushButton, QSpinBox, QStackedWidget, QTableWidget, QTableWidgetItem, QVBoxLayout,
                               QWidget)

from .link import NUM_JOINTS, Link
from .program import STEP_TYPES, Program, Runner, Step
from . import dialogs
from .widgets import COLORS, AngleSpinBox, error_text

FILE_FILTER = "RobotRoka programma (*.rrprog);;JSON (*.json);;Visi faili (*)"


class ProgramTab(QWidget):
    message = Signal(str)
    title_changed = Signal(str)

    def __init__(self, link: Link):
        super().__init__()
        self.link = link
        self.program = Program()
        self.path = ""
        self.dirty = False
        self._updating = False
        self.runner = Runner(link)
        self.runner.step_started.connect(self._on_step_started)
        self.runner.finished.connect(self._on_finished)
        self.runner.state_changed.connect(self._on_runner_state)

        root = QHBoxLayout(self)

        # ---------------------------------------------------------- saraksts
        left = QVBoxLayout()
        files = QHBoxLayout()
        for txt, fn in (("Jauna", self.new), ("Atvērt…", self.open), ("Saglabāt", self.save),
                        ("Saglabāt kā…", self.save_as), ("Eksportēt tekstu…", self.export_text),
                        ("Importēt tekstu…", self.import_text)):
            b = QPushButton(txt)
            b.clicked.connect(fn)
            files.addWidget(b)
        files.addStretch(1)
        left.addLayout(files)

        self.table = QTableWidget(0, 4)
        self.table.setHorizontalHeaderLabels(["#", "Darbība", "Parametri", "Komentārs"])
        self.table.verticalHeader().setVisible(False)
        self.table.setSelectionBehavior(QAbstractItemView.SelectRows)
        self.table.setSelectionMode(QAbstractItemView.SingleSelection)
        self.table.setEditTriggers(QAbstractItemView.NoEditTriggers)
        hh = self.table.horizontalHeader()
        hh.setSectionResizeMode(0, QHeaderView.ResizeToContents)
        hh.setSectionResizeMode(1, QHeaderView.ResizeToContents)
        hh.setSectionResizeMode(2, QHeaderView.Stretch)
        hh.setSectionResizeMode(3, QHeaderView.Interactive)
        hh.resizeSection(3, 180)
        self.table.currentCellChanged.connect(lambda *_: self._load_editor())
        self.table.cellDoubleClicked.connect(lambda *_: self._exec_selected())
        left.addWidget(self.table, 1)

        add = QHBoxLayout()
        add.addWidget(QLabel("Pievienot:"))
        for txt, fn, tip in (
            ("+ Pozīcija", self.add_position, "Ieraksta rokas pašreizējo pozīciju (mācīšana)"),
            ("+ Satvērējs", lambda: self._insert(Step(type="grip", value=self.link.status.grip)), ""),
            ("+ Pauze", lambda: self._insert(Step(type="wait", value=500)), ""),
            ("+ HOME", lambda: self._insert(Step(type="home", value=0)), ""),
            ("+ Komanda", lambda: self._insert(Step(type="cmd", text="")), "Jebkura kontroliera komanda"),
        ):
            b = QPushButton(txt)
            b.setToolTip(tip)
            b.clicked.connect(fn)
            add.addWidget(b)
        add.addStretch(1)
        left.addLayout(add)
        add = QHBoxLayout()
        add.addWidget(QLabel("Izvēlētais solis:"))
        for txt, fn in (("↑", lambda: self._move_row(-1)), ("↓", lambda: self._move_row(1)),
                        ("Dublēt", self._duplicate), ("Dzēst", self._delete)):
            b = QPushButton(txt)
            b.clicked.connect(fn)
            add.addWidget(b)
        add.addStretch(1)
        left.addLayout(add)
        root.addLayout(left, 3)

        # ---------------------------------------------------------- labā puse
        right = QVBoxLayout()
        run = QGroupBox("Izpilde")
        rl = QGridLayout(run)
        self.btn_run = QPushButton("▶ Palaist")
        self.btn_run.setObjectName("primary")
        self.btn_run.clicked.connect(self._run)
        self.btn_pause = QPushButton("⏸ Pauze")
        self.btn_pause.clicked.connect(self._pause_resume)
        self.btn_step = QPushButton("⏭ Viens solis")
        self.btn_step.setToolTip("Izpilda vienu soli un apstājas")
        self.btn_step.clicked.connect(self._single_step)
        self.btn_stop = QPushButton("■ Apturēt")
        self.btn_stop.clicked.connect(lambda: self.runner.stop())
        rl.addWidget(self.btn_run, 0, 0)
        rl.addWidget(self.btn_pause, 0, 1)
        rl.addWidget(self.btn_step, 1, 0)
        rl.addWidget(self.btn_stop, 1, 1)
        rl.addWidget(QLabel("Atkārtot (0 = bezgalīgi):"), 2, 0)
        self.repeat = QSpinBox()
        self.repeat.setRange(0, 100000)
        self.repeat.setValue(1)
        self.repeat.valueChanged.connect(self._repeat_changed)
        rl.addWidget(self.repeat, 2, 1)
        self.from_selected = QCheckBox("Sākt no izvēlētā soļa")
        rl.addWidget(self.from_selected, 3, 0, 1, 2)
        rl.addWidget(QLabel("Ātrums jaunām pozīcijām, %:"), 4, 0)
        self.default_speed = QSpinBox()
        self.default_speed.setRange(1, 100)
        self.default_speed.setValue(50)
        rl.addWidget(self.default_speed, 4, 1)
        self.run_label = QLabel("Gatavs")
        self.run_label.setStyleSheet(f"color:{COLORS['dim']}")
        rl.addWidget(self.run_label, 5, 0, 1, 2)
        b = QPushButton("Izpildīt izvēlēto soli")
        b.setToolTip("Tikai šo vienu soli (arī dubultklikšķis sarakstā)")
        b.clicked.connect(self._exec_selected)
        rl.addWidget(b, 6, 0, 1, 2)
        right.addWidget(run)

        self.editor = self._build_editor()
        right.addWidget(self.editor)
        right.addStretch(1)
        root.addLayout(right, 2)

        self._refresh_table()
        self._on_runner_state("idle")

    # ------------------------------------------------------------ redaktors
    def _build_editor(self) -> QGroupBox:
        box = QGroupBox("Solis")
        lay = QVBoxLayout(box)
        form = QFormLayout()
        self.ed_type = QComboBox()
        for k, v in STEP_TYPES.items():
            self.ed_type.addItem(v, k)
        self.ed_type.currentIndexChanged.connect(self._editor_changed)
        form.addRow("Darbība:", self.ed_type)
        lay.addLayout(form)

        self.stack = QStackedWidget()
        # move
        w = QWidget()
        g = QGridLayout(w)
        g.setContentsMargins(0, 0, 0, 0)
        self.ed_use, self.ed_deg = [], []
        for j in range(NUM_JOINTS):
            cb = QCheckBox(f"J{j + 1}")
            cb.setToolTip("Neatzīmēta = locītava paliek, kur ir")
            sp = AngleSpinBox()
            cb.toggled.connect(self._editor_changed)
            sp.valueChanged.connect(self._editor_changed)
            self.ed_use.append(cb)
            self.ed_deg.append(sp)
            g.addWidget(cb, j, 0)
            g.addWidget(sp, j, 1)
        g.addWidget(QLabel("Ātrums, %"), NUM_JOINTS, 0)
        self.ed_speed = QSpinBox()
        self.ed_speed.setRange(1, 100)
        self.ed_speed.valueChanged.connect(self._editor_changed)
        g.addWidget(self.ed_speed, NUM_JOINTS, 1)
        b = QPushButton("Ņemt pašreizējo pozīciju")
        b.clicked.connect(self._editor_take_pos)
        g.addWidget(b, NUM_JOINTS + 1, 0, 1, 2)
        self.stack.addWidget(w)
        # grip
        self.ed_grip = QSpinBox()
        self.ed_grip.setRange(0, 100)
        self.ed_grip.setSuffix(" %")
        self.ed_grip.valueChanged.connect(self._editor_changed)
        self.stack.addWidget(self._labeled("Satvērējs (0 atvērts, 100 aizvērts):", self.ed_grip))
        # wait
        self.ed_wait = QSpinBox()
        self.ed_wait.setRange(0, 3600000)
        self.ed_wait.setSingleStep(100)
        self.ed_wait.setSuffix(" ms")
        self.ed_wait.valueChanged.connect(self._editor_changed)
        self.stack.addWidget(self._labeled("Pauze:", self.ed_wait))
        # home
        self.ed_home = QComboBox()
        self.ed_home.addItem("Visas (ar gala slēdzi)", 0)
        for j in range(NUM_JOINTS):
            self.ed_home.addItem(f"J{j + 1}", j + 1)
        self.ed_home.currentIndexChanged.connect(self._editor_changed)
        self.stack.addWidget(self._labeled("Locītava:", self.ed_home))
        # cmd
        self.ed_cmd = QLineEdit()
        self.ed_cmd.setPlaceholderText("piem. MOVEJ 3 45 S20   vai   CFG SPEED 1 30")
        self.ed_cmd.textChanged.connect(self._editor_changed)
        self.stack.addWidget(self._labeled("Kontroliera komanda:", self.ed_cmd))
        lay.addWidget(self.stack)

        form2 = QFormLayout()
        self.ed_comment = QLineEdit()
        self.ed_comment.textChanged.connect(self._editor_changed)
        form2.addRow("Komentārs:", self.ed_comment)
        lay.addLayout(form2)
        return box

    @staticmethod
    def _labeled(text, widget) -> QWidget:
        w = QWidget()
        lay = QVBoxLayout(w)
        lay.setContentsMargins(0, 0, 0, 0)
        lay.addWidget(QLabel(text))
        lay.addWidget(widget)
        lay.addStretch(1)
        return w

    def _current(self):
        r = self.table.currentRow()
        return (r, self.program.steps[r]) if 0 <= r < len(self.program.steps) else (-1, None)

    def _load_editor(self) -> None:
        r, s = self._current()
        self.editor.setEnabled(s is not None)
        if s is None:
            return
        self._updating = True
        self.ed_type.setCurrentIndex(list(STEP_TYPES).index(s.type))
        self.stack.setCurrentIndex(list(STEP_TYPES).index(s.type))
        for j in range(NUM_JOINTS):
            v = s.joints[j]
            self.ed_use[j].setChecked(v is not None)
            self.ed_deg[j].setEnabled(v is not None)
            self.ed_deg[j].setValue(0 if v is None else v)
        self.ed_speed.setValue(int(s.speed))
        self.ed_grip.setValue(int(s.value) if s.type == "grip" else 0)
        self.ed_wait.setValue(int(s.value) if s.type == "wait" else 500)
        self.ed_home.setCurrentIndex(int(s.value) if s.type == "home" else 0)
        self.ed_cmd.setText(s.text)
        self.ed_comment.setText(s.comment)
        self.editor.setTitle(f"Solis {r + 1}")
        self._updating = False

    def _editor_changed(self, *_) -> None:
        if self._updating:
            return
        r, s = self._current()
        if s is None:
            return
        s.type = self.ed_type.currentData()
        self.stack.setCurrentIndex(self.ed_type.currentIndex())
        for j in range(NUM_JOINTS):
            use = self.ed_use[j].isChecked()
            self.ed_deg[j].setEnabled(use)
            s.joints[j] = round(self.ed_deg[j].value(), 2) if use else None
        s.speed = self.ed_speed.value()
        s.value = {"grip": self.ed_grip.value(), "wait": self.ed_wait.value(),
                   "home": self.ed_home.currentData()}.get(s.type, 0)
        s.text = self.ed_cmd.text()
        s.comment = self.ed_comment.text()
        self._set_row(r, s)
        self._mark_dirty()

    def _editor_take_pos(self) -> None:
        r, s = self._current()
        if s is None:
            return
        self._updating = True
        st = self.link.status
        for j in range(NUM_JOINTS):
            self.ed_use[j].setChecked(bool(st.zeroed[j]))   # bez nulles kustināt nevar
            self.ed_deg[j].setValue(st.pos[j])
        self._updating = False
        self._editor_changed()

    # ------------------------------------------------------------ tabula
    def _set_row(self, r: int, s: Step) -> None:
        items = [str(r + 1), STEP_TYPES.get(s.type, s.type), s.summary(), s.comment]
        for c, txt in enumerate(items):
            it = self.table.item(r, c)
            if it is None:
                it = QTableWidgetItem()
                self.table.setItem(r, c, it)
            it.setText(txt)

    def _refresh_table(self, select: int = -1) -> None:
        self.table.blockSignals(True)
        self.table.setRowCount(len(self.program.steps))
        for r, s in enumerate(self.program.steps):
            self._set_row(r, s)
        self.table.blockSignals(False)
        if select >= 0 and self.program.steps:
            self.table.setCurrentCell(min(select, len(self.program.steps) - 1), 0)
        self._load_editor()

    def _insert(self, step: Step) -> None:
        r, _ = self._current()
        at = r + 1 if r >= 0 else len(self.program.steps)
        self.program.steps.insert(at, step)
        self._refresh_table(at)
        self._mark_dirty()

    def add_position(self) -> None:
        if not self.link.connected:
            self.message.emit("Nav savienojuma - pozīciju nevar nolasīt")
            return
        st = self.link.status
        if not any(st.zeroed):
            self.message.emit("Nevienai locītavai nav nulles - vispirms HOME vai ZERO")
            return
        # tikai locītavas ar nulli (pārējās kontrolieris atteiktos kustināt)
        self._insert(Step(type="move", joints=[round(p, 2) if z else None for p, z in zip(st.pos, st.zeroed)],
                          speed=self.default_speed.value()))

    def _move_row(self, d: int) -> None:
        r, s = self._current()
        n = r + d
        if s is None or not 0 <= n < len(self.program.steps):
            return
        st = self.program.steps
        st[r], st[n] = st[n], st[r]
        self._refresh_table(n)
        self._mark_dirty()

    def _duplicate(self) -> None:
        r, s = self._current()
        if s is not None:
            self._insert(copy.deepcopy(s))

    def _delete(self) -> None:
        r, s = self._current()
        if s is None:
            return
        del self.program.steps[r]
        self._refresh_table(r)
        self._mark_dirty()

    def _repeat_changed(self, v: int) -> None:
        if not self._updating:
            self.program.repeat = v
            self._mark_dirty()

    # ------------------------------------------------------------ izpilde
    def _run(self) -> None:
        if not self.link.connected:
            self.message.emit("Nav savienojuma")
            return
        if not self.program.steps:
            return
        start = max(0, self.table.currentRow()) if self.from_selected.isChecked() else 0
        self.runner.start(self.program, start)

    def _single_step(self) -> None:
        if self.runner.state == "paused":
            self.runner.resume(single_step=True)
        elif self.runner.state == "idle" and self.link.connected and self.program.steps:
            self.runner.start(self.program, max(0, self.table.currentRow()), single_step=True)

    def _pause_resume(self) -> None:
        if self.runner.state == "running":
            self.runner.pause()
            self.run_label.setText("Pauze pēc pašreizējā soļa…")
        elif self.runner.state == "paused":
            self.runner.resume()

    def _exec_selected(self) -> None:
        _, s = self._current()
        if s is None or self.runner.state == "running":
            return
        cmd = s.command()
        if cmd:
            self.link.send(cmd, lambda rep: None if rep.ok else self.message.emit(f"{cmd}: {error_text(rep.code)}"))

    def stop_program(self) -> None:
        if self.runner.state != "idle":
            self.runner.stop(send_stop=False)

    def _on_step_started(self, i: int) -> None:
        self.table.setCurrentCell(i, 0)
        for r in range(self.table.rowCount()):
            for c in range(4):
                it = self.table.item(r, c)
                if it:
                    it.setBackground(QBrush(QColor(COLORS["accent"]).darker(220)) if r == i else QBrush())
        rep = self.program.repeat
        self.run_label.setText(f"Solis {i + 1}/{len(self.program.steps)}, cikls {self.runner.loop_no}"
                               f"{'' if rep == 0 else f'/{rep}'}")

    def _on_finished(self, ok: bool, msg: str) -> None:
        self.run_label.setText(msg)
        for r in range(self.table.rowCount()):
            for c in range(4):
                it = self.table.item(r, c)
                if it:
                    it.setBackground(QBrush())
        if not ok:
            for code in ("not_zeroed", "limit", "alarm", "busy", "timeout"):
                if code in msg:
                    msg += f" — {error_text(code)}"
            self.message.emit(msg)

    def _on_runner_state(self, s: str) -> None:
        self.btn_run.setEnabled(s == "idle")
        self.btn_pause.setEnabled(s != "idle")
        self.btn_pause.setText("▶ Turpināt" if s == "paused" else "⏸ Pauze")
        self.btn_stop.setEnabled(s != "idle")
        self.table.setEnabled(s != "running")
        self.editor.setEnabled(s != "running" and self._current()[1] is not None)

    # ------------------------------------------------------------ faili
    def _mark_dirty(self, d: bool = True) -> None:
        self.dirty = d
        name = os.path.basename(self.path) if self.path else "nesaglabāta programma"
        self.title_changed.emit(f"{name}{' *' if d else ''}")

    def confirm_discard(self) -> bool:
        if not self.dirty or not self.program.steps:
            return True
        r = dialogs.choose(self, "Programma nav saglabāta", "Saglabāt izmaiņas?",
                           [("Saglabāt", "save", "primary"), ("Nesaglabāt", "discard", "danger"),
                            ("Atcelt", "cancel", "")], "cancel")
        if r == "save":
            return self.save()
        return r == "discard"

    def set_program(self, prog: Program, path: str = "") -> None:
        self.program = prog
        self.path = path
        self._updating = True
        self.repeat.setValue(prog.repeat)
        self._updating = False
        self._refresh_table(0)
        self._mark_dirty(False)

    def new(self) -> None:
        if self.confirm_discard():
            self.set_program(Program())

    def _dir(self) -> str:
        return QSettings().value("program/dir", os.path.expanduser("~"))

    def open(self, path: str = "") -> None:
        if not self.confirm_discard():
            return
        if not path:
            path, _ = QFileDialog.getOpenFileName(self, "Atvērt programmu", self._dir(), FILE_FILTER)
        if not path:
            return
        try:
            with open(path, encoding="utf-8") as f:
                self.set_program(Program.from_json(f.read()), path)
            QSettings().setValue("program/dir", os.path.dirname(path))
            QSettings().setValue("program/last", path)
        except Exception as e:  # noqa: BLE001
            dialogs.inform(self, "Kļūda", f"Nevar atvērt {path}:\n{e}")

    def save(self) -> bool:
        if not self.path:
            return self.save_as()
        try:
            with open(self.path, "w", encoding="utf-8") as f:
                f.write(self.program.to_json())
        except OSError as e:
            dialogs.inform(self, "Kļūda", str(e))
            return False
        QSettings().setValue("program/last", self.path)
        self._mark_dirty(False)
        return True

    def save_as(self) -> bool:
        path, _ = QFileDialog.getSaveFileName(self, "Saglabāt programmu", self.path or self._dir(), FILE_FILTER)
        if not path:
            return False
        if not os.path.splitext(path)[1]:
            path += ".rrprog"
        self.path = path
        QSettings().setValue("program/dir", os.path.dirname(path))
        return self.save()

    def export_text(self) -> None:
        """Komandu teksts - der arī kontroliera ekrāna SD kartei (8.3 nosaukums, piem. PROG1.TXT)."""
        import re
        path, _ = QFileDialog.getSaveFileName(self, "Eksportēt komandas (arī SD kartei)",
                                              os.path.join(self._dir(), "PROG1.TXT"), "Teksts (*.txt *.TXT)")
        if not path:
            return
        base = os.path.basename(path)
        if not re.fullmatch(r"[A-Za-z0-9_\-]{1,8}\.[Tt][Xx][Tt]", base):
            dialogs.inform(self, "SD kartes nosaukums",
                                    f"'{base}' saglabāts, bet ekrāna SD kartē fails jānosauc īsāk:\n"
                                    "līdz 8 burtiem/cipariem + .TXT, bez atstarpēm un garumzīmēm (piem. PROG1.TXT).")
        lv = str.maketrans("āčēģīķļņšūžĀČĒĢĪĶĻŅŠŪŽ", "acegiklnsuzACEGIKLNSUZ")
        with open(path, "w", encoding="ascii", errors="replace", newline="\r\n") as f:
            f.write(self.program.to_text().translate(lv))
    def import_text(self) -> None:
        if not self.confirm_discard():
            return
        path, _ = QFileDialog.getOpenFileName(self, "Importēt komandas", self._dir(), "Teksts (*.txt);;Visi (*)")
        if path:
            with open(path, encoding="utf-8") as f:
                self.set_program(Program.from_text(f.read()))
            self._mark_dirty(True)
