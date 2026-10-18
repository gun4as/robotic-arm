"""Apstiprinājuma un paziņojumu logi pašā programmas logā (tāpat kā web lapā).

Fons aptumšojas un izplūst, vidū lodziņš ar virsrakstu, tekstu un pogām. Bez sistēmas
uznirstošajiem logiem. Enter = galvenā poga, klikšķis ārpusē = atcelt.
Esc programmā ir AVĀRIJAS STOP - tas aizver arī atvērto logu (cancel_all).
"""
from __future__ import annotations

from typing import Optional

from PySide6.QtCore import QEvent, QEventLoop, QObject, Qt
from PySide6.QtGui import QColor, QPainter
from PySide6.QtWidgets import QFrame, QGraphicsBlurEffect, QHBoxLayout, QLabel, QPushButton, QVBoxLayout, QWidget

from .widgets import COLORS

_open: list = []


class _Overlay(QWidget):
    def __init__(self, win: QWidget, title: str, text: str, buttons: list, cancel_value):
        super().__init__(win)
        self.win = win
        self.result = cancel_value
        self.cancel_value = cancel_value
        self._loop = QEventLoop()
        self.setAttribute(Qt.WA_StyledBackground, False)
        self.setFocusPolicy(Qt.StrongFocus)

        self.box = QFrame(self)
        self.box.setObjectName("dlgBox")
        self.box.setStyleSheet(f"""
            QFrame#dlgBox {{ background: {COLORS['panel']}; border: 1px solid {COLORS['line']}; border-radius: 12px; }}
            QLabel#dlgTitle {{ font-weight: 700; font-size: 11pt; padding: 14px 18px;
                               border-bottom: 1px solid {COLORS['line']}; }}
            QLabel#dlgText {{ padding: 18px; }}
            QPushButton#dlgDanger {{ background: rgba(229,72,77,40); border-color: rgba(229,72,77,140); color: {COLORS['bad']};
                                     font-weight: 600; }}
            QPushButton#dlgDanger:hover {{ background: rgba(229,72,77,75); }}
            QPushButton#dlgPrimary {{ background: {COLORS['blue']}; border-color: {COLORS['blue']}; color: white; }}
        """)
        lay = QVBoxLayout(self.box)
        lay.setContentsMargins(0, 0, 0, 0)
        lay.setSpacing(0)
        t = QLabel(title)
        t.setObjectName("dlgTitle")
        lay.addWidget(t)
        m = QLabel(text)
        m.setObjectName("dlgText")
        m.setWordWrap(True)
        m.setTextInteractionFlags(Qt.TextSelectableByMouse)
        lay.addWidget(m)
        row = QHBoxLayout()
        row.setContentsMargins(18, 0, 18, 16)
        row.addStretch(1)
        self.default_btn: Optional[QPushButton] = None
        for label, value, kind in buttons:
            b = QPushButton(label)
            if kind == "danger":
                b.setObjectName("dlgDanger")
            elif kind == "primary":
                b.setObjectName("dlgPrimary")
            b.clicked.connect(lambda _=False, v=value: self.done(v))
            row.addWidget(b)
            if self.default_btn is None:
                self.default_btn = b
        lay.addLayout(row)
        self.box.setFixedWidth(460)
        win.installEventFilter(self)
        self._fit()

    def _fit(self) -> None:
        self.setGeometry(self.win.rect())
        self.box.adjustSize()
        self.box.move((self.width() - self.box.width()) // 2, (self.height() - self.box.height()) // 2)

    def eventFilter(self, obj: QObject, ev: QEvent) -> bool:
        if obj is self.win and ev.type() == QEvent.Resize:
            self._fit()
        return False

    def paintEvent(self, _ev) -> None:
        QPainter(self).fillRect(self.rect(), QColor(0, 0, 0, 140))

    def mousePressEvent(self, ev) -> None:
        if not self.box.geometry().contains(ev.position().toPoint()):
            self.done(self.cancel_value)

    def keyPressEvent(self, ev) -> None:
        if ev.key() in (Qt.Key_Return, Qt.Key_Enter) and self.default_btn:
            self.default_btn.click()
        else:
            super().keyPressEvent(ev)

    def done(self, value) -> None:
        self.result = value
        self._loop.quit()

    def run(self):
        central = getattr(self.win, "centralWidget", lambda: None)()
        if central is not None:
            blur = QGraphicsBlurEffect()
            blur.setBlurRadius(4)
            central.setGraphicsEffect(blur)
        _open.append(self)
        self.show()
        self.raise_()
        self.setFocus()
        self._loop.exec()
        _open.remove(self)
        if central is not None and not _open:
            central.setGraphicsEffect(None)
        self.win.removeEventFilter(self)
        self.hide()
        self.deleteLater()
        return self.result


def _show(parent: QWidget, title: str, text: str, buttons: list, cancel_value):
    return _Overlay(parent.window(), title, text, buttons, cancel_value).run()


def ask(parent: QWidget, title: str, text: str, ok_text: str = "Apstiprināt", danger: bool = True) -> bool:
    """Jā/nē jautājums. True = apstiprināts."""
    return bool(_show(parent, title, text, [(ok_text, True, "danger" if danger else "primary"),
                                            ("Atcelt", False, "")], False))


def inform(parent: QWidget, title: str, text: str) -> None:
    """Paziņojums ar vienu pogu."""
    _show(parent, title, text, [("Labi", None, "primary")], None)


def choose(parent: QWidget, title: str, text: str, buttons: list, cancel_value):
    """Vairākas izvēles: buttons = [(teksts, vērtība, 'danger'|'primary'|''), ...]."""
    return _show(parent, title, text, buttons, cancel_value)


def cancel_all() -> None:
    """Aizver visus atvērtos logus kā 'atcelts' (izsauc AVĀRIJAS STOP)."""
    for o in list(_open):
        o.done(o.cancel_value)
