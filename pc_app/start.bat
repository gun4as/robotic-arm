@echo off
rem RobotRoka PC programma. Pirmaja reize izveido Python vidi un instale PySide6 + pyserial.
cd /d "%~dp0"
if not exist ".venv\Scripts\python.exe" (
  echo Izveido Python vidi...
  python -m venv .venv || goto :nopython
  ".venv\Scripts\python.exe" -m pip install --disable-pip-version-check -r requirements.txt || goto :fail
)
start "" ".venv\Scripts\pythonw.exe" robotroka_pc.py
exit /b 0

:nopython
echo Nav atrasts Python 3.9+. Instale no https://www.python.org (atzime "Add to PATH").
pause
exit /b 1

:fail
echo Neizdevas instalet bibliotekas.
pause
exit /b 1
