"""Integrācijas tests: PC programmas savienojums + programmas izpildītājs pret
virtuālo roku (īstais firmware, kompilēts datoram).

    cd firmware/controller/sim && pio run -e virtual
    pc_app\\.venv\\Scripts\\python pc_app\\tests\\test_virtual.py
"""
import os
import subprocess
import sys
import tempfile
import time

sys.path.insert(0, os.path.join(os.path.dirname(__file__), ".."))

from PySide6.QtCore import QCoreApplication, QEventLoop, QTimer  # noqa: E402

from robotroka.link import Link  # noqa: E402
from robotroka.program import Program, Runner, Step  # noqa: E402

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
EXE = os.path.join(ROOT, "firmware", "controller", "sim", ".pio", "build", "virtual", "program.exe")
PORT = 5598

app = QCoreApplication(sys.argv)
fails = 0


def check(cond, msg):
    global fails
    print(("  ok   " if cond else "  FAIL ") + msg)
    if not cond:
        fails += 1


def wait_until(pred, timeout):
    end = time.time() + timeout
    while time.time() < end:
        app.processEvents(QEventLoop.AllEvents, 50)
        if pred():
            return True
        time.sleep(0.01)
    return False


def cmd(link, text, timeout=5):
    box = {}
    link.send(text, lambda r: box.setdefault("r", r))
    wait_until(lambda: "r" in box, timeout)
    return box.get("r")


def run_program(link, prog, timeout=60):
    runner = Runner(link)
    res = {}
    started = []
    runner.step_started.connect(started.append)
    runner.finished.connect(lambda ok, msg: res.update(ok=ok, msg=msg))
    runner.start(prog)
    wait_until(lambda: "ok" in res, timeout)
    return res, started


workdir = tempfile.mkdtemp()
proc = subprocess.Popen([EXE, str(PORT)], cwd=workdir, stdout=subprocess.DEVNULL)
time.sleep(0.4)
link = Link()
events = []
link.event.connect(events.append)
try:
    print("[test] savienojums")
    link.open(f"socket://127.0.0.1:{PORT}")
    check(wait_until(lambda: link.status.seq > 2, 6), "statuss plūst (AUTO)")
    r = cmd(link, "INFO")
    check(r and r.ok and any("RobotRoka" in l for l in r.lines), f"INFO: {r}")
    r = cmd(link, "CFG")
    check(r and r.ok and sum(l.startswith("[CFG] J") for l in r.lines) == 5, "CFG 5 rindas")

    print("[test] programma bez nulles -> kļūda")
    prog = Program(steps=[Step(type="move", joints=[10, None, None, None, None], speed=100)])
    res, _ = run_program(link, prog, 10)
    check(res.get("ok") is False and "J1 nav nulles" in res.get("msg", ""), f"rezultāts: {res}")

    print("[test] programma ar kustībām, satvērēju, pauzi")
    check(cmd(link, "ZERO").ok, "ZERO")
    prog = Program(steps=[
        Step(type="move", joints=[20, 10, None, None, None], speed=100),
        Step(type="grip", value=80),
        Step(type="wait", value=300),
        Step(type="move", joints=[-10, None, 15, None, 30], speed=80),
        Step(type="cmd", text="MOVEJ 2 -5 S100"),
    ], repeat=2)
    t0 = time.time()
    res, started = run_program(link, prog, 60)
    check(res.get("ok") is True, f"pabeigta: {res} ({time.time() - t0:.1f}s)")
    check(started == [0, 1, 2, 3, 4] * 2, f"soļu secība: {started}")
    wait_until(lambda: False, 0.3)
    p = link.status.pos
    check(abs(p[0] + 10) < 0.05 and abs(p[1] + 5) < 0.05 and abs(p[2] - 15) < 0.05 and abs(p[4] - 30) < 0.05,
          f"gala pozīcija {p}")
    check(link.status.grip == 80, f"satvērējs {link.status.grip}")

    print("[test] HOME programmā ar gala slēdzi")
    for c in ["CFG ES 2 1", "CFG HOME_POS 2 -92"]:
        check(cmd(link, c).ok, c)
    prog = Program(steps=[Step(type="home", value=2), Step(type="move", joints=[None, 0, None, None, None], speed=100)])
    res, _ = run_program(link, prog, 40)
    check(res.get("ok") is True, f"HOME programma: {res}")
    check(any("[HOME] J2 ok" in e for e in events), "HOME notikums")
    check(abs(link.status.pos[1]) < 0.05 and link.status.endstops[1] == "0", f"J2 {link.status.pos[1]}, S={link.status.endstops}")

    print("[test] apturēšana programmas vidū")
    prog = Program(steps=[Step(type="move", joints=[150, None, None, None, None], speed=30)])
    runner = Runner(link)
    res = {}
    runner.finished.connect(lambda ok, msg: res.update(ok=ok, msg=msg))
    runner.start(prog)
    wait_until(lambda: link.status.state == "Move", 5)
    runner.stop()
    check(res.get("ok") is False, "apturēta")
    check(wait_until(lambda: link.status.idle, 8), "roka nobremzēja")
    check(link.status.pos[0] < 140, f"J1 neaizbrauca līdz galam ({link.status.pos[0]:.1f})")

    print("[test] ESTOP + RESET")
    link.send("MOVE -150 S100")
    wait_until(lambda: link.status.state == "Move", 5)
    link.realtime("!")
    check(wait_until(lambda: link.status.state == "Alarm", 3), "Alarm")
    check(cmd(link, "RESET").ok and wait_until(lambda: link.status.state == "Idle", 2), "RESET")

    print("[test] teksta eksports/imports")
    txt = prog.to_text()
    back = Program.from_text(txt)
    check(back.steps[0].text.startswith("MOVE 150.00 _ _ _ _ S30"), f"teksts: {txt!r}")
finally:
    link.close()
    proc.terminate()

print(f"\n{'VISS OK' if not fails else f'{fails} KĻŪDAS'}")
sys.exit(1 if fails else 0)
