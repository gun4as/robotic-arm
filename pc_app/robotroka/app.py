"""RobotRoka PC programma - galvenais logs."""
from __future__ import annotations

import os
import re
import sys

from PySide6.QtCore import QProcess, QSettings, QStandardPaths, QTimer, Qt
from PySide6.QtGui import QAction, QKeySequence, QShortcut
from PySide6.QtWidgets import (QApplication, QComboBox, QHBoxLayout, QLabel, QMainWindow, QPushButton,
                               QTabWidget, QToolBar, QVBoxLayout, QWidget)

from . import __version__, dialogs
from .config import ControllerConfig
from .link import NUM_JOINTS, VIRTUAL_URL, Link, Status, list_ports, virtual_com_pairs
from .ui_config import ConfigTab
from .ui_console import ConsoleTab
from .ui_control import ControlTab
from .ui_program import ProgramTab
from .ui_remote import RemoteTab
from .widgets import COLORS, apply_theme, error_text, mono_font

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
VIRTUAL_EXE = os.path.join(ROOT, "firmware", "controller", "sim", ".pio", "build", "virtual", "program.exe")
STATE_NAMES = {"Idle": "gatavs", "Jog": "manuāli", "Move": "kustība", "Home": "HOME", "Alarm": "AVĀRIJA"}


class MainWindow(QMainWindow):
    def __init__(self):
        super().__init__()
        self.setMinimumSize(1100, 680)
        self.link = Link()
        self.cfg = ControllerConfig(self.link)
        self.virtual: QProcess | None = None
        self._virtual_arg = ""
        self._program_title = "nesaglabāta programma"

        # ---------------------------------------------------------- rīkjosla
        tb = QToolBar("Savienojums")
        tb.setMovable(False)
        self.addToolBar(tb)
        self.port_combo = QComboBox()
        self.port_combo.setMinimumWidth(280)
        self.port_combo.setEditable(True)
        self.port_combo.setToolTip("COM ports vai socket://host:port")
        tb.addWidget(self.port_combo)
        refresh = QAction("⟳", self)
        refresh.setToolTip("Atjaunot portu sarakstu")
        refresh.triggered.connect(self.refresh_ports)
        tb.addAction(refresh)
        self.baud_combo = QComboBox()
        for b in ("115200", "250000", "57600", "9600"):
            self.baud_combo.addItem(b)
        tb.addWidget(self.baud_combo)
        self.btn_connect = QPushButton("Pieslēgties")
        self.btn_connect.setObjectName("primary")
        self.btn_connect.clicked.connect(self.toggle_connection)
        tb.addWidget(self.btn_connect)
        self.btn_virtual = QPushButton("Virtuālā roka")
        self.btn_virtual.setToolTip("Palaiž īsto firmware uz datora (bez dzelžiem) un pieslēdzas tam")
        self.btn_virtual.clicked.connect(self.start_virtual)
        tb.addWidget(self.btn_virtual)
        # Stāvoklis un apstāšanās pogas - atsevišķā joslā, kas nekad nepazūd (rīkjoslas ">>" izvēlnē)
        header = QWidget()
        hl = QHBoxLayout(header)
        hl.setContentsMargins(8, 4, 8, 4)
        self.board_label = QLabel("")
        self.board_label.setToolTip("Pieslēgtā plate (to ziņo pats firmware)")
        self.board_label.setStyleSheet(f"color:white; background:{COLORS['blue']}; border-radius:6px; padding:3px 10px; font-weight:700")
        self.board_label.hide()
        hl.addWidget(self.board_label)
        self.state_label = QLabel("nav savienojuma")
        self.state_label.setFont(mono_font(11, True))
        hl.addWidget(self.state_label, 1)
        for txt, cmd in (("Apturēt", "STOP"), ("RESET", "RESET")):
            b = QPushButton(txt)
            b.clicked.connect(lambda _=False, c=cmd: self.send_simple(c))
            hl.addWidget(b)
        self.btn_estop = QPushButton("AVĀRIJAS STOP  (Esc)")
        self.btn_estop.setObjectName("estop")
        self.btn_estop.clicked.connect(self.estop)
        hl.addWidget(self.btn_estop)
        QShortcut(QKeySequence(Qt.Key_Escape), self, activated=self.estop, context=Qt.ApplicationShortcut)
        # ---------------------------------------------------------- cilnes
        self.tabs = QTabWidget()
        self.control = ControlTab(self.link, self.cfg)
        self.program = ProgramTab(self.link)
        self.config = ConfigTab(self.link, self.cfg)
        self.console = ConsoleTab(self.link)
        self.remote = RemoteTab(self.link)
        self.tabs.addTab(self.control, "Vadība")
        self.tabs.addTab(self.program, "Programma")
        self.tabs.addTab(self.config, "Konfigurācija")
        self.tabs.addTab(self.remote, "Pults")
        self.tabs.addTab(self.console, "Konsole")
        central = QWidget()
        cl = QVBoxLayout(central)
        cl.setContentsMargins(4, 0, 4, 4)
        cl.setSpacing(4)
        cl.addWidget(header)
        cl.addWidget(self.tabs, 1)
        self.setCentralWidget(central)

        # ---------------------------------------------------------- statusa josla
        sb = self.statusBar()
        self.sb_pos = QLabel()
        self.sb_pos.setFont(mono_font(10))
        sb.addPermanentWidget(self.sb_pos)

        for w in (self.control, self.program, self.config, self.remote):
            w.message.connect(self.show_message)
        self.config.log.connect(lambda l: self.console.append(l, COLORS["dim"]))
        self.program.title_changed.connect(self._set_program_title)
        self.link.connection_changed.connect(self._on_connection)
        self.link.status_changed.connect(self._on_status)
        self.link.event.connect(self._on_event)
        self.cfg.info_changed.connect(self._on_info)

        self._watchdog = QTimer(self)
        self._watchdog.timeout.connect(self._check_alive)
        self._watchdog.start(500)

        self.refresh_ports()
        s = QSettings()
        if s.value("window/geometry"):
            self.restoreGeometry(s.value("window/geometry"))
        self.baud_combo.setCurrentText(str(s.value("link/baud", "115200")))
        last = s.value("program/last", "")
        if last and os.path.exists(last):
            self.program.open(last)
        self._update_title()
        self._on_connection(False, "Izvēlies portu un spied 'Pieslēgties' (vai 'Virtuālā roka')")

    # ------------------------------------------------------------ savienojums
    def refresh_ports(self) -> None:
        cur = self.port_combo.currentData() or QSettings().value("link/port", "")
        self.port_combo.clear()
        for url, desc in list_ports():
            self.port_combo.addItem(desc, url)
        i = self.port_combo.findData(cur)
        if i >= 0:
            self.port_combo.setCurrentIndex(i)

    def _selected_url(self) -> str:
        i = self.port_combo.currentIndex()
        if i >= 0 and self.port_combo.itemText(i) == self.port_combo.currentText():
            return self.port_combo.itemData(i)
        return self.port_combo.currentText().strip()

    def toggle_connection(self) -> None:
        if self.link.connected:
            self.control.stop_jog()
            self.program.stop_program()
            self.link.close()
            return
        url = self._selected_url()
        if not url:
            return
        try:
            self.link.open(url, int(self.baud_combo.currentText()))
        except Exception as e:  # noqa: BLE001
            dialogs.inform(self, "Nevar pieslēgties", f"{url}\n\n{e}")
            return
        QSettings().setValue("link/port", url)
        QSettings().setValue("link/baud", self.baud_combo.currentText())
        QTimer.singleShot(400, lambda: self.cfg.refresh() if self.link.connected else None)

    def start_virtual(self) -> None:
        """Palaiž virtuālo roku. Ja ir com0com pāris (piem. COM10 <-> COM11), roka klausās
        vienā portā un programma pieslēdzas otram - tieši kā īstai platei. Citādi TCP."""
        if not os.path.exists(VIRTUAL_EXE):
            dialogs.inform(self, "Virtuālā roka nav uzbūvēta",
                                    "Uzbūvē to ar PlatformIO:\n\n  cd firmware/controller/sim\n  pio run -e virtual")
            return
        pairs = virtual_com_pairs()
        if pairs:
            app_port, arm_arg = pairs[0]
        else:
            app_port, arm_arg = VIRTUAL_URL, "5555"
        if self.link.connected:
            self.link.close()
        running = self.virtual is not None and self.virtual.state() != QProcess.NotRunning
        if running and self._virtual_arg != arm_arg:
            self.virtual.kill()
            self.virtual.waitForFinished(1000)
            running = False
        if not running:
            work = QStandardPaths.writableLocation(QStandardPaths.AppDataLocation)
            os.makedirs(work, exist_ok=True)
            self.virtual = QProcess(self)
            self.virtual.setWorkingDirectory(work)
            self.virtual.start(VIRTUAL_EXE, [arm_arg])
            # ja ports nav atverams, programma uzreiz beidzas ar paskaidrojumu
            if not self.virtual.waitForStarted(3000) or self.virtual.waitForFinished(600):
                err = bytes(self.virtual.readAll()).decode("utf-8", "replace")
                if "aizņemts" in err or "izmanto cita" in err:
                    # Visticamāk virtuālā roka jau darbojas (palaista ar roku) - vienkārši pieslēdzamies.
                    self.show_message(f"{arm_arg} jau aizņemts - pieslēdzos esošajai virtuālajai rokai")
                else:
                    dialogs.inform(self, "Kļūda", f"Neizdevās palaist virtuālo roku ({arm_arg}).\n\n{err}")
                    return
            else:
                self.virtual.readyReadStandardOutput.connect(lambda p=self.virtual: p.readAll())   # neļaujam buferim pildīties
            self._virtual_arg = arm_arg
        self.refresh_ports()
        i = self.port_combo.findData(app_port)
        if i >= 0:
            self.port_combo.setCurrentIndex(i)
        if not pairs:
            self.show_message("Virtuālā roka caur TCP. Lai tā būtu COM ports - instalē com0com (skat. docs/pc_programma.md)")
        QTimer.singleShot(400, self.toggle_connection)
    def _on_connection(self, on: bool, msg: str) -> None:
        self.btn_connect.setText("Atvienot" if on else "Pieslēgties")
        self.port_combo.setEnabled(not on)
        self.baud_combo.setEnabled(not on)
        if not on:
            self.board_label.hide()
            self.cfg.info = {}
            self.state_label.setText("nav savienojuma")
            self.state_label.setStyleSheet(f"color:{COLORS['dim']}")
            self.control.stop_jog()
            self.program.stop_program()
        self.show_message(msg)
        self.console.append(f"--- {msg}", COLORS["dim"])

    def _on_info(self) -> None:
        i = self.cfg.info
        self.board_label.setText(f"{i.get('board', '?')}  ·  fw {i.get('version', '?')}")
        self.board_label.show()

    def _check_alive(self) -> None:
        import time
        if self.link.connected and self.link.status.at and time.monotonic() - self.link.status.at > 1.5:
            self.state_label.setText("kontrolieris klusē")
            self.state_label.setStyleSheet(f"color:{COLORS['warn']}")

    # ------------------------------------------------------------ statuss
    def _on_status(self, st: Status) -> None:
        name = STATE_NAMES.get(st.state, st.state)
        if st.queue:
            name += f" · rindā {st.queue}"
        if st.run:
            line, _, cyc = st.run.lstrip("P").partition("/")
            name += f" · SD programma{' (pauze)' if st.run.startswith('P') else ''} rinda {line}, cikls {cyc}"
        color = {"Idle": COLORS["ok"], "Alarm": COLORS["bad"]}.get(st.state, COLORS["blue"])
        self.state_label.setText(f"{name}  ·  motori {'ieslēgti' if st.enabled else 'atslēgti'}")
        self.state_label.setStyleSheet(f"color:{color}")
        self.sb_pos.setText("  ".join(f"J{j + 1} {st.pos[j]:8.2f}°" for j in range(NUM_JOINTS))
                            + f"   G {st.grip:3d}%   S {st.endstops}")
        self.control.update_status(st)
        self.config.update_status(st)

    def _on_event(self, line: str) -> None:
        if line.startswith("[READY]"):
            self.show_message("Kontrolieris (pār)startējās - pozīcija jāuzstāda no jauna (ZERO / HOME)")
            self.program.stop_program()
            QTimer.singleShot(300, self.cfg.refresh)
        elif line.startswith("[ESTOP]"):
            self.program.stop_program()
            self.show_message("AVĀRIJAS APSTĀŠANĀS - spied RESET")
        elif line.startswith("[ENDSTOP]"):
            # roka apstājās pie slēdža (bez avārijas) - prom no tā var braukt uzreiz
            self.program.stop_program()
            m = re.match(r"\[ENDSTOP\] J(\d+)", line)
            self.show_message(f"J{m.group(1) if m else '?'} apstājās pie gala slēdža - uz otru pusi var braukt")
        elif "[HOME] kluda" in line:
            self.program.stop_program()
            dialogs.inform(self, "HOME neizdevās", line)
        elif line.startswith("[ZERO]"):
            # nulle pēc HOME: kontrolieris pārrēķināja un saglabāja 'Leņķis pie slēdža'
            m = re.match(r"\[ZERO\] J(\d+) sledzis tagad (\S+)", line)
            if m:
                self.show_message(f"J{m.group(1)}: šī vieta tagad ir 0 - HOME turpmāk brauks šeit "
                                  f"(leņķis pie slēdža {m.group(2)}°, saglabāts)")
            QTimer.singleShot(300, self.cfg.refresh)
        elif line.startswith("[JOG] "):
            self.show_message("Manuālā kustība: " + error_text(line[6:].strip()))
        elif line.startswith("[HOME]") or line.startswith("[TMC]") or line.startswith("[HOST]") or line.startswith("[RUN]"):
            self.show_message(line)

    # ------------------------------------------------------------ darbības
    def estop(self) -> None:
        dialogs.cancel_all()
        self.control.stop_jog()
        self.program.stop_program()
        if self.link.connected:
            self.link.realtime("!")
        self.show_message("AVĀRIJAS STOP nosūtīts")

    def send_simple(self, cmd: str) -> None:
        if cmd == "STOP":
            self.program.stop_program()
        self.link.send(cmd, lambda r: None if r.ok else self.show_message(f"{cmd}: {error_text(r.code)}"))

    def show_message(self, msg: str) -> None:
        for code in ("not_zeroed", "limit", "alarm", "busy", "no_endstop", "queue_full", "storage", "bad_arg"):
            if msg.endswith(code):
                msg = msg[: -len(code)] + error_text(code)
        self.statusBar().showMessage(msg, 8000)

    def _set_program_title(self, t: str) -> None:
        self._program_title = t
        self._update_title()

    def _update_title(self) -> None:
        self.setWindowTitle(f"RobotRoka {__version__} — {self._program_title}")

    def closeEvent(self, e) -> None:
        if not self.program.confirm_discard():
            e.ignore()
            return
        self.control.stop_jog()
        self.link.close()
        if self.virtual and self.virtual.state() != QProcess.NotRunning:
            self.virtual.kill()
            self.virtual.waitForFinished(1000)
        QSettings().setValue("window/geometry", self.saveGeometry())
        e.accept()


def main() -> int:
    app = QApplication(sys.argv)
    app.setOrganizationName("RobotRoka")
    app.setApplicationName("RobotRokaPC")
    apply_theme(app)
    w = MainWindow()
    w.show()
    return app.exec()
