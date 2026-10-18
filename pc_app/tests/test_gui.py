"""GUI dūmu tests: īstais galvenais logs pret virtuālo roku, bez ekrāna (offscreen).
Saglabā ekrānuzņēmumus mapē, kas norādīta 1. argumentā (noklusējums: tests/screens)."""
import os
import sys
import tempfile
import time

os.environ.setdefault("QT_QPA_PLATFORM", "offscreen")
sys.path.insert(0, os.path.join(os.path.dirname(__file__), ".."))

from PySide6.QtCore import QEventLoop, QSettings, QTimer  # noqa: E402
from PySide6.QtWidgets import QApplication, QPushButton  # noqa: E402

out_dir = sys.argv[1] if len(sys.argv) > 1 else os.path.join(os.path.dirname(__file__), "screens")
os.makedirs(out_dir, exist_ok=True)
QSettings.setDefaultFormat(QSettings.IniFormat)
QSettings.setPath(QSettings.IniFormat, QSettings.UserScope, tempfile.mkdtemp())

# Logi testā nedrīkst bloķēt (īstos pārbauda atsevišķi beigās)
from robotroka import dialogs  # noqa: E402
real_ask = dialogs.ask
popups = []
dialogs.ask = lambda *a, **k: True
dialogs.inform = lambda *a, **k: popups.append(a[2] if len(a) > 2 else a)
dialogs.choose = lambda *a, **k: "discard"

from robotroka.app import MainWindow  # noqa: E402
from robotroka.program import Step  # noqa: E402
from robotroka.widgets import apply_theme  # noqa: E402

app = QApplication(sys.argv)
app.setOrganizationName("RobotRokaTest")
app.setApplicationName("RobotRokaPCTest")
apply_theme(app)
fails = 0


def check(cond, msg):
    global fails
    print(("  ok   " if cond else "  FAIL ") + msg)
    fails += 0 if cond else 1


def pump(sec):
    end = time.time() + sec
    while time.time() < end:
        app.processEvents(QEventLoop.AllEvents, 30)
        time.sleep(0.01)


def wait_until(pred, timeout):
    end = time.time() + timeout
    while time.time() < end:
        app.processEvents(QEventLoop.AllEvents, 30)
        if pred():
            return True
        time.sleep(0.01)
    return False


def click(widget, text):
    for b in widget.findChildren(QPushButton):
        if b.text() == text:
            b.click()
            return True
    raise AssertionError(f"nav pogas '{text}'")


def shot(w, name):
    pump(0.3)
    w.grab().save(os.path.join(out_dir, name))


w = MainWindow()
w.resize(1360, 820)
w.show()
try:
    print("[gui] virtuālā roka + savienojums")
    w.start_virtual()
    check(wait_until(lambda: w.link.connected and w.link.status.seq > 3 and w.cfg.loaded, 10), "savienots, CFG nolasīts")
    check("gatavs" in w.state_label.text(), f"stāvoklis: {w.state_label.text()}")
    check(wait_until(lambda: "RAMPS 1.4" in w.board_label.text() and w.board_label.isVisible(), 3),
          f"plate redzama: {w.board_label.text()}")

    print("[gui] vadība")
    w.link.send("ZERO")
    w.control.speed_slider.setValue(100)
    for j, v in enumerate([30, -20, 45, 10, 90]):
        w.control.targets[j].setValue(v)
    click(w.control, "Iet uz visiem mērķiem")
    check(wait_until(lambda: w.link.status.state == "Move", 3), "kustība sākās")
    check(wait_until(lambda: w.link.status.idle, 15), "kustība beidzās")
    check(abs(w.link.status.pos[2] - 45) < 0.05, f"J3 = {w.link.status.pos[2]}")
    w.control.step_size.setCurrentIndex(4)          # 10°
    w.control._step(0, 1)
    check(wait_until(lambda: abs(w.link.status.pos[0] - 40) < 0.05 and w.link.status.idle, 8), "solis +10° J1")
    w.control._jog(1, 1, True)
    pump(0.6)
    w.control._jog(1, 1, False)
    check(wait_until(lambda: w.link.status.idle, 5) and w.link.status.pos[1] > -20, f"jogs J2 -> {w.link.status.pos[1]:.2f}")
    msgs = []
    w.control.message.connect(msgs.append)
    w.control.targets[2].setValue(170)              # J3 limits ir ±135
    before = w.link.status.pos[2]
    click(w.control, "Iet uz visiem mērķiem")
    pump(0.5)
    check(any("ārpus limita" in m for m in msgs) and w.link.status.idle and w.link.status.pos[2] == before,
          f"mērķis ārpus limita netiek sūtīts: {msgs}")
    check("ĀRPUS" in w.control.targets[2].toolTip(), "lauks iezīmēts kā ārpus limita")
    w.control.targets[2].setValue(45)
    w.control.grip_slider.setValue(60)
    check(wait_until(lambda: w.link.status.grip == 60, 3), "satvērējs 60%")
    shot(w, "1_vadiba.png")

    print("[gui] konfigurācija: gala slēdzis J1, SAVE")
    w.tabs.setCurrentWidget(w.config)
    w.config.cells[(0, "es")].setChecked(True)
    w.config.cells[(0, "home_pos")].setValue(-102)
    w.config.cells[(0, "speed")].setValue(40)
    w.config.write(save=True)
    check(wait_until(lambda: w.cfg.joints[0].get("es") == 1 and w.cfg.joints[0].get("speed") == 40, 5), "ierakstīts")
    check(not popups, f"bez kļūdām: {popups}")
    check(w.config.board_label.text() == "RAMPS 1.4", f"konfigurācijā plate: {w.config.board_label.text()}")
    w.config.hwcells["lcd"].setChecked(False)
    w.config.write(save=False)
    check(wait_until(lambda: w.cfg.globals.get("lcd") == 0, 5), "ekrāns atslēgts no PC")
    w.config.hwcells["lcd"].setChecked(True)
    w.config.write(save=False)
    check(wait_until(lambda: w.cfg.globals.get("lcd") == 1, 5), "ekrāns ieslēgts no PC")
    pump(0.5)
    shot(w, "3_konfiguracija.png")

    print("[gui] programma: mācīšana + HOME + izpilde")
    w.tabs.setCurrentWidget(w.program)
    p = w.program
    p.set_program(p.program.__class__())
    p._insert(Step(type="home", value=1, comment="atrod J1 slēdzi"))
    p.add_position()
    p._insert(Step(type="grip", value=100, comment="satver"))
    p._insert(Step(type="wait", value=300))
    p._insert(Step(type="move", joints=[0, 0, 0, 0, 0], speed=80, comment="mājās"))
    p._insert(Step(type="grip", value=0, comment="atlaiž"))
    check(len(p.program.steps) == 6, f"6 soļi ({len(p.program.steps)})")
    p.table.setCurrentCell(1, 0)
    shot(w, "2_programma.png")
    p.repeat.setValue(1)
    done = {}
    p.runner.finished.connect(lambda ok, msg: done.update(ok=ok, msg=msg))
    p._run()
    check(wait_until(lambda: "ok" in done, 60), f"programma beidzās: {done}")
    check(done.get("ok") is True, "veiksmīgi")
    pos = w.link.status.pos
    check(all(abs(v) < 0.05 for v in pos) and w.link.status.grip == 0, f"beigās mājās: {pos}")

    print("[gui] faila saglabāšana / atvēršana")
    path = os.path.join(tempfile.mkdtemp(), "tests.rrprog")
    p.path = path
    check(p.save() and os.path.exists(path), "saglabāts")
    p.set_program(p.program.__class__())
    p.open(path)
    check(len(p.program.steps) == 6 and p.program.steps[1].type == "move", "atvērts")

    print("[gui] cilne Pults (tilta rindas)")
    w.tabs.setCurrentWidget(w.remote)
    m = ("#rcmap t=1:-1,1:1,2:-1,2:1,3:-1,3:1,4:-1,4:1,5:-1,5:1,6:-1,6:1 a=0:1,1:1,2:1,3:1,-1:1 "
         "b=0:1,1:1,2:1,-1:1,3:-1 s=25,50,100 st=5 g=5")
    w.link.bridge_line.emit(m)
    pump(0.2)
    check(w.remote.map_line() == "#rcmap_set" + m[len("#rcmap"):], f"rcmap turp-atpakaļ: {w.remote.map_line()}")
    w.link.bridge_line.emit("#rcraw link=1 ch=-500,800,0,250,300 btn=4097 sw=5 s3=0,2 bat=8150 armed=1")
    pump(0.2)
    check(w.remote.btn_chips[0].styleSheet().startswith("background") and w.remote.btn_chips[12].styleSheet().startswith("background"),
          "pogas P0 un H iedegas")
    check("8.2 V" in w.remote.state_label.text() and "ARM" in w.remote.state_label.text(), w.remote.state_label.text())
    check(w.remote.mode_chip.text() == "C režīms atskaņošana", w.remote.mode_chip.text())
    shot(w, "6_pults.png")

    print("[gui] apstiprinājuma logs programmas logā")
    def press_ok():
        ov = [o for o in dialogs._open]
        if ov:
            shot(w, "5_logs.png")
            for b in ov[0].findChildren(QPushButton):
                if b.text() == "Atslēgt":
                    b.click()
    QTimer.singleShot(400, press_ok)
    check(real_ask(w.control, "Atslēgt motorus", "Roka var nokrist no sava svara.", "Atslēgt") is True, "Apstiprināt -> True")
    check(not dialogs._open and w.centralWidget().graphicsEffect() is None, "logs aizvērts, fons atjaunots")
    QTimer.singleShot(300, dialogs.cancel_all)        # tā dara AVĀRIJAS STOP (Esc)
    check(real_ask(w.control, "Nulle visām", "Tests", "Uzstādīt") is False, "cancel_all -> False")

    print("[gui] ESTOP ar Esc")
    w.link.send("MOVE -100 S30")
    wait_until(lambda: w.link.status.state == "Move", 3)
    w.estop()
    check(wait_until(lambda: w.link.status.state == "Alarm", 3), "Alarm")
    w.tabs.setCurrentWidget(w.console)
    w.console.input.setText("RESET")
    w.console._send()
    check(wait_until(lambda: w.link.status.state == "Idle", 3), "RESET no konsoles")
    shot(w, "4_konsole.png")
finally:
    w.program.dirty = False
    w.close()
    pump(0.3)

print(f"\n{'VISS OK' if not fails else f'{fails} KĻŪDAS'}  (ekrānuzņēmumi: {out_dir})")
sys.exit(1 if fails else 0)
