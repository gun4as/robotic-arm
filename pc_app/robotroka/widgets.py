"""Kopīgi GUI elementi un tēma."""
from __future__ import annotations

from PySide6.QtCore import QSize, Qt, Signal
from PySide6.QtCore import QLocale
from PySide6.QtGui import QColor, QFont, QPainter, QPalette
from PySide6.QtWidgets import QApplication, QDoubleSpinBox, QPushButton, QWidget

COLORS = {
    "bg": "#15181d", "panel": "#1e232a", "panel2": "#262c35", "line": "#333b46",
    "text": "#e6e9ee", "dim": "#8b95a3", "accent": "#e0573a", "ok": "#3fb67a",
    "warn": "#e3a72f", "bad": "#e5484d", "blue": "#4c8dff",
}

ERRORS = {
    "not_zeroed": "Locītavai nav nulles (ZERO vai HOME)",
    "limit": "Mērķis ārpus limita",
    "queue_full": "Kustību rinda pilna",
    "alarm": "Avārijas stāvoklis — spied RESET",
    "busy": "Aizņemts — vispirms apturi kustību",
    "bad_arg": "Nepareizi argumenti",
    "unknown_cmd": "Nezināma komanda",
    "no_tmc": "Šai platei nav TMC UART",
    "tmc": "TMC draiveris neatbild",
    "no_endstop": "Locītavai nav ieslēgts gala slēdzis",
    "storage": "Atmiņas kļūda (nav saglabātu iestatījumu?)",
    "checksum": "Bojāta rinda",
    "line_too_long": "Rinda par garu",
    "timeout": "Kontrolieris neatbild",
    "not_connected": "Nav savienojuma",
    "cancelled": "Atcelts",
}


def error_text(code: str) -> str:
    return ERRORS.get(code, code)


def apply_theme(app: QApplication) -> None:
    app.setStyle("Fusion")
    p = QPalette()
    c = {k: QColor(v) for k, v in COLORS.items()}
    p.setColor(QPalette.Window, c["bg"])
    p.setColor(QPalette.WindowText, c["text"])
    p.setColor(QPalette.Base, c["panel"])
    p.setColor(QPalette.AlternateBase, c["panel2"])
    p.setColor(QPalette.Text, c["text"])
    p.setColor(QPalette.Button, c["panel2"])
    p.setColor(QPalette.ButtonText, c["text"])
    p.setColor(QPalette.Highlight, c["accent"])
    p.setColor(QPalette.HighlightedText, QColor("#ffffff"))
    p.setColor(QPalette.ToolTipBase, c["panel2"])
    p.setColor(QPalette.ToolTipText, c["text"])
    p.setColor(QPalette.PlaceholderText, c["dim"])
    p.setColor(QPalette.Disabled, QPalette.ButtonText, c["dim"])
    p.setColor(QPalette.Disabled, QPalette.Text, c["dim"])
    p.setColor(QPalette.Disabled, QPalette.WindowText, c["dim"])
    app.setPalette(p)
    app.setStyleSheet(f"""
        QWidget {{ font-size: 10pt; }}
        QGroupBox {{ border: 1px solid {COLORS['line']}; border-radius: 8px; margin-top: 14px; padding: 10px 8px 8px 8px;
                     background: {COLORS['panel']}; }}
        QGroupBox::title {{ subcontrol-origin: margin; left: 10px; padding: 0 4px; color: {COLORS['dim']};
                            font-weight: 600; }}
        QPushButton {{ padding: 6px 12px; border: 1px solid {COLORS['line']}; border-radius: 6px;
                       background: {COLORS['panel2']}; }}
        QPushButton:hover {{ border-color: #4a5462; }}
        QPushButton:pressed, QPushButton:checked {{ background: {COLORS['accent']}; border-color: {COLORS['accent']}; }}
        QPushButton:disabled {{ color: {COLORS['dim']}; }}
        QPushButton#primary {{ background: {COLORS['blue']}; border-color: {COLORS['blue']}; color: white; }}
        QPushButton#estop {{ background: {COLORS['bad']}; border-color: {COLORS['bad']}; color: white;
                             font-weight: 800; font-size: 12pt; padding: 8px 18px; }}
        QPushButton#jog {{ min-width: 38px; min-height: 30px; font-size: 12pt; padding: 2px 6px; }}
        QTabBar::tab {{ padding: 8px 16px; background: {COLORS['panel']}; border: 1px solid {COLORS['line']};
                        border-bottom: none; border-top-left-radius: 6px; border-top-right-radius: 6px; margin-right: 2px; }}
        QTabBar::tab:selected {{ background: {COLORS['accent']}; color: white; }}
        QTabWidget::pane {{ border: 1px solid {COLORS['line']}; top: -1px; }}
        QTableWidget {{ gridline-color: {COLORS['line']}; }}
        QHeaderView::section {{ background: {COLORS['panel2']}; color: {COLORS['dim']}; border: none;
                                border-right: 1px solid {COLORS['line']}; padding: 4px; }}
        QLineEdit, QSpinBox, QDoubleSpinBox, QComboBox, QPlainTextEdit {{ border: 1px solid {COLORS['line']};
                       border-radius: 5px; padding: 3px 5px; background: {COLORS['panel']}; }}
        QToolBar {{ border: none; spacing: 6px; padding: 4px; }}
        QStatusBar {{ background: {COLORS['panel']}; }}
    """)


def mono_font(size: int = 10, bold: bool = False) -> QFont:
    f = QFont("Consolas")
    f.setStyleHint(QFont.Monospace)
    f.setPointSize(size)
    f.setBold(bold)
    return f


class Led(QWidget):
    """Apaļš indikators: None = pelēks (atslēgts), True = sarkans/zaļš, False = tumšs."""

    def __init__(self, on_color: str = COLORS["bad"], size: int = 14, parent=None):
        super().__init__(parent)
        self._on = None
        self._color = QColor(on_color)
        self.setFixedSize(QSize(size, size))

    def set_state(self, on) -> None:
        if on != self._on:
            self._on = on
            self.update()

    def paintEvent(self, _):
        p = QPainter(self)
        p.setRenderHint(QPainter.Antialiasing)
        if self._on is None:
            col = QColor(COLORS["line"])
        elif self._on:
            col = self._color
        else:
            col = QColor("#2b3a2f")
        p.setBrush(col)
        p.setPen(QColor(COLORS["line"]))
        p.drawEllipse(1, 1, self.width() - 2, self.height() - 2)


class AngleSpinBox(QDoubleSpinBox):
    """Skaitļa lauks grādiem: pieņem gan komatu, gan punktu, plašs diapazons (±720°).
    Locītavas limitus pārbauda izmantotājs (lai lauks klusi neatsakās pieņemt ciparus)."""

    def __init__(self, decimals: int = 2, parent=None):
        super().__init__(parent)
        self.setLocale(QLocale.c())
        self.setDecimals(decimals)
        self.setRange(-720, 720)
        self.setSuffix("°")
        self.setKeyboardTracking(True)

    def _norm(self, text: str) -> str:
        return text.replace(",", ".")

    def validate(self, text, pos):
        return super().validate(self._norm(text), pos)

    def valueFromText(self, text):
        return super().valueFromText(self._norm(text))

    def fixup(self, text):
        return super().fixup(self._norm(text))

    def set_limits_hint(self, lo: float, hi: float) -> bool:
        """Iekrāso lauku, ja vērtība ārpus [lo, hi]. Atgriež True, ja vērtība ir derīga."""
        ok = lo - 1e-6 <= self.value() <= hi + 1e-6
        self.setStyleSheet("" if ok else f"border:1px solid {COLORS['bad']}; color:{COLORS['bad']};")
        self.setToolTip(f"Atļauts {lo:g}…{hi:g}° (maina: Konfigurācija → Min/Max leņķis)"
                        + ("" if ok else "\nŠī vērtība ir ĀRPUS limita"))
        return ok


class HoldButton(QPushButton):
    """Poga, kas ziņo par nospiešanu un atlaišanu (jogam)."""
    held = Signal(bool)

    def __init__(self, text: str, parent=None):
        super().__init__(text, parent)
        self.setObjectName("jog")
        self.setAutoRepeat(False)
        self.setFocusPolicy(Qt.NoFocus)
        self.pressed.connect(lambda: self.held.emit(True))
        self.released.connect(lambda: self.held.emit(False))


class PositionBar(QWidget):
    """Josla: kur locītava ir starp min un max. Marķieris = pozīcija, aizpildījums no 0°,
    dzeltena līnija = mērķis, pelēka līnija = 0°. Pelēks = nav nulles. Klikšķis -> jauns mērķis."""
    target_clicked = Signal(float)

    def __init__(self, parent=None):
        super().__init__(parent)
        self.lo, self.hi = -180.0, 180.0
        self.pos, self.target, self.zeroed = 0.0, 0.0, False
        self.unit = "°"
        self.setMinimumSize(160, 26)
        self.setCursor(Qt.PointingHandCursor)
        self.setToolTip("Kur locītava atrodas. Dzeltenā līnija = mērķis (klikšķis joslā to pārliek), pelēkā = 0°")

    def set_range(self, lo: float, hi: float) -> None:
        self.lo, self.hi = lo, hi
        self.update()

    def set_state(self, pos: float, zeroed: bool) -> None:
        if pos != self.pos or zeroed != self.zeroed:
            self.pos, self.zeroed = pos, zeroed
            self.update()

    def set_target(self, v: float) -> None:
        self.target = v
        self.update()

    def _x(self, v: float) -> float:
        span = self.hi - self.lo
        f = (min(self.hi, max(self.lo, v)) - self.lo) / span if span > 0 else 0.5
        return 1 + f * (self.width() - 2)

    def paintEvent(self, _):
        p = QPainter(self)
        p.setRenderHint(QPainter.Antialiasing)
        h = self.height()
        p.setPen(QColor(COLORS["line"]))
        p.setBrush(QColor(COLORS["panel2"]))
        p.drawRoundedRect(0, 0, self.width() - 1, h - 1, 5, 5)
        col = QColor(COLORS["accent"] if self.zeroed else COLORS["dim"])
        x0, xp = self._x(0.0), self._x(self.pos)
        fill = QColor(col)
        fill.setAlpha(80)
        p.setPen(Qt.NoPen)
        p.setBrush(fill)
        p.drawRect(int(min(x0, xp)), 1, int(abs(xp - x0)), h - 2)
        p.setBrush(QColor(COLORS["dim"]))
        p.drawRect(int(x0), 1, 1, h - 2)
        p.setBrush(QColor(COLORS["warn"]))
        p.drawRect(int(self._x(self.target)) - 1, 0, 2, h)
        p.setBrush(col)
        p.drawRoundedRect(int(xp) - 3, 3, 6, h - 6, 3, 3)
        p.setPen(QColor(COLORS["dim"]))
        f = p.font()
        f.setPointSize(7)
        p.setFont(f)
        p.drawText(5, 0, 60, h, Qt.AlignVCenter | Qt.AlignLeft, f"{self.lo:g}{self.unit}")
        p.drawText(self.width() - 65, 0, 60, h, Qt.AlignVCenter | Qt.AlignRight, f"{self.hi:g}{self.unit}")

    def mousePressEvent(self, ev) -> None:
        span = self.hi - self.lo
        f = (ev.position().x() - 1) / max(1, self.width() - 2)
        v = round((self.lo + min(1.0, max(0.0, f)) * span) * 2) / 2
        self.target_clicked.emit(v)
