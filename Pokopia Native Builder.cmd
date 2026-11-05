@echo off
rem Pokopia Native Builder (Windows): opens the window (nxs2_gui.py) with Python 3.12 or newer. The python.org installer
rem puts "py" on the PATH; "pyw" runs without a console window. What happens is written to builder-log.txt beside this
rem file: attach it to a report when the window does not open.
setlocal
cd /d "%~dp0"
set "LOG=%~dp0builder-log.txt"
> "%LOG%" echo Pokopia Native Builder, %DATE% %TIME%, in %~dp0
set "PY="
set "PYW="
where py >nul 2>nul && set "PY=py -3" && set "PYW=pyw -3"
if not defined PY where python >nul 2>nul && set "PY=python" && set "PYW=pythonw"
if not defined PY goto nopython
rem lz4 comes ready-made for a new Python only some months after its release (3.15: pip then wants Microsoft's C++ Build
rem Tools), so when the newest Python lacks it, an installed 3.14, 3.13 or 3.12 that has it is used instead
set "ALT="
if not "%PY%"=="py -3" goto pychosen
py -3 -c "import lz4" >nul 2>nul && goto pychosen
for %%V in (3.14 3.13 3.12) do if not defined ALT py -%%V -c "import lz4" >nul 2>nul && set "ALT=%%V"
if defined ALT set "PY=py -%ALT%" && set "PYW=pyw -%ALT%"
:pychosen
>> "%LOG%" echo Python command: %PY%
rem Python itself, its version and Tk, checked in this window first: a launcher without a console shows nothing when they fail ("call": some
rem installs put a .bat shim on the PATH)
call %PY% -c "import sys; print('Python', sys.version.split()[0], 'at', sys.executable); import tkinter; print('Tk', tkinter.TkVersion); sys.exit(0 if sys.version_info >= (3, 12) else 3)" >> "%LOG%" 2>&1
set "RC=%ERRORLEVEL%"
>> "%LOG%" echo check: exit code %RC%
if "%RC%"=="3" goto oldpython
if not "%RC%"=="0" goto broken
start "" %PYW% "%~dp0nxs2_gui.py"
>> "%LOG%" echo window started (its own errors follow, if any)
exit /b 0

:nopython
>> "%LOG%" echo no Python on the PATH
echo Python 3.12 or newer was not found. Install it from https://www.python.org/downloads/ (tick "Add python.exe to PATH"), then run this again.
pause
exit /b 1

:oldpython
type "%LOG%"
echo.
echo This Python is older than 3.12. Install 3.12 or newer from https://www.python.org/downloads/ (tick "Add python.exe to PATH"), then run this again.
pause
exit /b 1

:broken
type "%LOG%"
echo.
echo Python did not start properly (above, and in builder-log.txt beside this file). If it mentions tkinter, reinstall Python from
echo https://www.python.org/downloads/ with "tcl/tk and IDLE" ticked. The "python" that opens the Microsoft Store is not Python:
echo install it from python.org instead. Still stuck? Attach builder-log.txt to a report on GitHub.
pause
exit /b 1
