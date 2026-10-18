"""Cilne "Konsole": visas rindas no/uz kontrolieri un komandrinda ar vēsturi."""
from __future__ import annotations

from PySide6.QtCore import Qt
from PySide6.QtGui import QTextCharFormat, QColor, QTextCursor
from PySide6.QtWidgets import (QCheckBox, QHBoxLayout, QLineEdit, QPlainTextEdit, QPushButton, QVBoxLayout,
                               QWidget)

from .link import Link
from .widgets import COLORS, mono_font


class HistoryLineEdit(QLineEdit):
    def __init__(self):
        super().__init__()
        self.history, self.pos = [], 0

    def push(self, text: str) -> None:
        if text and (not self.history or self.history[-1] != text):
            self.history.append(text)
        self.pos = len(self.history)

    def keyPressEvent(self, e):
        if e.key() == Qt.Key_Up and self.history:
            self.pos = max(0, self.pos - 1)
            self.setText(self.history[self.pos])
        elif e.key() == Qt.Key_Down and self.history:
            self.pos = min(len(self.history), self.pos + 1)
            self.setText(self.history[self.pos] if self.pos < len(self.history) else "")
        else:
            super().keyPressEvent(e)


class ConsoleTab(QWidget):
    def __init__(self, link: Link):
        super().__init__()
        self.link = link
        lay = QVBoxLayout(self)
        self.view = QPlainTextEdit()
        self.view.setReadOnly(True)
        self.view.setMaximumBlockCount(3000)
        self.view.setFont(mono_font(10))
        lay.addWidget(self.view, 1)

        row = QHBoxLayout()
        self.input = HistoryLineEdit()
        self.input.setFont(mono_font(10))
        self.input.setPlaceholderText("Komanda (↑/↓ vēsture), piem.  MOVE 10 20 0 0 0 S30   HELP   CFG   ES")
        self.input.returnPressed.connect(self._send)
        row.addWidget(self.input, 1)
        b = QPushButton("Sūtīt")
        b.setObjectName("primary")
        b.clicked.connect(self._send)
        row.addWidget(b)
        lay.addLayout(row)

        opts = QHBoxLayout()
        for cmd in ("HELP", "INFO", "CFG", "ES", "?"):
            b = QPushButton(cmd)
            b.clicked.connect(lambda _=False, c=cmd: self.link.realtime("?") if c == "?" else self.link.send(c))
            opts.addWidget(b)
        opts.addStretch(1)
        self.show_status = QCheckBox("rādīt statusu")
        self.show_ok = QCheckBox("rādīt 'ok'")
        opts.addWidget(self.show_status)
        opts.addWidget(self.show_ok)
        b = QPushButton("Notīrīt")
        b.clicked.connect(self.view.clear)
        opts.addWidget(b)
        lay.addLayout(opts)

        link.line_received.connect(self._rx)
        link.line_sent.connect(lambda l: self.append("› " + l, COLORS["blue"]))

    def append(self, text: str, color: str = COLORS["text"]) -> None:
        fmt = QTextCharFormat()
        fmt.setForeground(QColor(color))
        cur = self.view.textCursor()
        cur.movePosition(QTextCursor.End)
        cur.insertText(text + "\n", fmt)
        sb = self.view.verticalScrollBar()
        if sb.value() >= sb.maximum() - 40:
            self.view.setTextCursor(cur)
            self.view.ensureCursorVisible()

    def _rx(self, line: str) -> None:
        if line.startswith("<"):
            if self.show_status.isChecked():
                self.append(line, COLORS["dim"])
            return
        if line == "ok":
            if self.show_ok.isChecked():
                self.append(line, COLORS["dim"])
            return
        if line.startswith("err"):
            self.append(line, COLORS["bad"])
        elif line.startswith("["):
            self.append(line, COLORS["warn"])
        else:
            self.append(line)

    def _send(self) -> None:
        t = self.input.text().strip()
        if not t:
            return
        self.input.push(t)
        self.input.clear()
        if t in ("!", "?"):
            self.link.realtime(t)
        else:
            self.link.send(t)
