@echo off
rem DOA4 rollback milestone 2: the save / restore test (src\net\rb_snap.h). Put this file and snaptest_single.txt in
rem the game folder (next to dead_or_alive_4.exe, alpha-v0.3.141 or later) and double-click it. The game plays a scripted
rem Versus fight by itself two times (about 2 minutes each), rewinds it many times each time and checks that every replay
rem comes out the same (the D3D device's and the XMA decoder's state go back with the snapshot): from frame 3600,
rem with graphics memory, the main thread's registers and the physical heaps' allocations put back too (round 44: which code stores the worker-stack addresses, and the workers one at a time for comparison):
rem 54 rewinds of 8 frames, 29 rewinds of 15 frames, then 4 rewinds at the start of round 2; the sound mix page is left out again.
rem Your own save is not used or changed: each run gets a fresh, empty user data folder. A run that has not ended
rem after 4 minutes, or that froze, is closed.
setlocal
set "HERE=%~dp0"
cd /d "%HERE%"
if not exist "dead_or_alive_4.exe" (
  echo Put run_snaptest.bat and snaptest_single.txt in the game folder, next to dead_or_alive_4.exe.
  pause
  exit /b 1
)
if not exist "snaptest_single.txt" (
  echo snaptest_single.txt is missing: put it next to run_snaptest.bat.
  pause
  exit /b 1
)
set "DOA4_NP=record"
set "DOA4_NP_SCRIPT=%HERE%snaptest_single.txt"
if not defined DOA4_NP_SNAPTEST set "DOA4_NP_SNAPTEST=1"
set "DOA4_NP_SNAPTEST_MODE=marked"
set "SDL_GAMECONTROLLER_IGNORE_DEVICES_EXCEPT=0x0000/0x0000"
set "CACHE=%USERPROFILE%\Documents\dead_or_alive_4\cache"
set "CACHEARG="
if exist "%CACHE%" set "CACHEARG=--cache_path=%CACHE%"
echo The game now plays a fight by itself two times, and rewinds it many times (about 6 minutes in all).
echo Do not touch it: it closes by itself each time.
for %%M in (wlanes cserial) do call :run %%M
if exist "%HERE%snaptest_results.zip" del "%HERE%snaptest_results.zip"
powershell -NoProfile -Command "Compress-Archive -Path '%HERE%snaptest_run_wlanes','%HERE%snaptest_run_cserial' -DestinationPath '%HERE%snaptest_results.zip'" >nul 2>&1
echo.
if exist "%HERE%snaptest_results.zip" (
  echo DONE. Send snaptest_results.zip from the game folder.
) else (
  echo DONE, but the results could not be zipped: send the two snaptest_run_ folders.
)
pause
exit /b 0

:run
set "RUN=%HERE%snaptest_run_%1"
if exist "%RUN%" rmdir /s /q "%RUN%"
mkdir "%RUN%\userdata"
mkdir "%RUN%\out"
set "DOA4_NP_DIR=%RUN%\out"
set "DOA4_LAG_TRACE="
set "DOA4_FIBER_PROF="
set "DOA4_NP_SNAPTEST_CENSUS="
set "DOA4_NP_SNAPTEST_SKIPJOB="
if "%1"=="bskip2" set "DOA4_NP_SNAPTEST_SKIPJOB=2"
if "%1"=="cskip0" set "DOA4_NP_SNAPTEST_SKIPJOB=0"
set "DOA4_LAG_DIR=%RUN%\out"
set "DOA4_NP_SNAPTEST_FENCE=rewind"
set "DOA4_NP_SNAPTEST_DEVICE=state"
set "DOA4_NP_SNAPTEST=1"
if "%1"=="wlanes" set "DOA4_NP_SNAPTEST=3600,8,61,30"
if "%1"=="cserial" set "DOA4_NP_SNAPTEST=3600,8,61,30"
if "%1"=="bskip2" set "DOA4_NP_SNAPTEST=3600,8,61,30"
if "%1"=="cskip0" set "DOA4_NP_SNAPTEST=3600,8,61,30"
if "%1"=="b113x15" set "DOA4_NP_SNAPTEST=3600,15,113,29"
if "%1"=="c5946" set "DOA4_NP_SNAPTEST=5946,8,61,4"
set "DOA4_NP_SNAPTEST_MODE=marked"
if "%1"=="b3600full" set "DOA4_NP_SNAPTEST_MODE=full"
if "%1"=="c3600both" set "DOA4_NP_SNAPTEST_MODE=full"
set "DOA4_NP_SNAPTEST_PHYS=restore"
set "DOA4_NP_SNAPTEST_WATCH=off"
if "%1"=="wlanes" set "DOA4_NP_SNAPTEST_WATCH=109D8CB0-109D8CC0,109EB1C0-109EB1D0,109E65A0-109E65B0,109EE440-109EE450,109F0E20-109F0E30,102CB720-102CB730,102C6840-102C6850"
if "%1"=="b113x15" set "DOA4_NP_SNAPTEST_WATCH=4001C68C-4001C69C,400246DC-400246EC,400FBAE0-400FBAE4,8357D520-8357D524,401CFDE8-401CFDEC,400E9728-400E972C,401E8FD8-401E8FDC,400F0C3C-400F0C40,400EAD1C-400EAD20"
set "DOA4_NP_SNAPTEST_WATCH_FRAMES="
if "%1"=="b113x15" set "DOA4_NP_SNAPTEST_WATCH_FRAMES=all"
if "%1"=="wlanes" set "DOA4_NP_SNAPTEST_WATCH_FRAMES=all"
set "DOA4_NP_SNAPTEST_WATCH_TEST=1"
set "DOA4_NP_SNAPTEST_STASHES="
set "DOA4_NP_SNAPTEST_THREADS="
set "DOA4_NP_SNAPTEST_DIFFAT=1"
set "DOA4_NP_SNAPTEST_NOISE=82F6C9D0-82F6C9D8,8357A500-8357A960,8357C190-8357C194,40195000-40196000"
set "DOA4_NP_SNAPTEST_HEAPS="
set "DOA4_NP_SNAPTEST_FIBERS="
set "DOA4_NP_SNAPTEST_REGS="
if "%1"=="c3600both" set "DOA4_NP_SNAPTEST_PHYS=restore"
set "DOA4_NP_SNAPTEST_AUDIO="
set "DOA4_NP_SNAPTEST_DEVLIVE="
set "REX_LOG_LAZY_OBJECTS=1"
set "DOA4_NP_SERIAL="
if "%1"=="cserial" set "DOA4_NP_SERIAL=3"
echo.
echo Run %1 ...
start "" "%HERE%dead_or_alive_4.exe" "--user_data_root=%RUN%\userdata" "--log_file=%RUN%\game.log" --headless=true %CACHEARG%
set /a WAITED=0
:wait
timeout /t 5 /nobreak >nul
set /a WAITED+=5
tasklist /fi "imagename eq dead_or_alive_4.exe" 2>nul | find /i "dead_or_alive_4.exe" >nul
if errorlevel 1 goto ended
findstr /c:"FREEZE DUMP" "%RUN%\game.log" >nul 2>&1
if not errorlevel 1 (
  echo The game froze: closing it.
  timeout /t 5 /nobreak >nul
  echo [run_snaptest] closed after a freeze>>"%RUN%\out\np_log.txt"
  taskkill /f /im dead_or_alive_4.exe >nul 2>&1
  timeout /t 3 /nobreak >nul
  goto ended
)
if %WAITED% geq 240 (
  echo The game has not finished after 4 minutes: closing it.
  echo [run_snaptest] closed after 4 minutes>>"%RUN%\out\np_log.txt"
  taskkill /f /im dead_or_alive_4.exe >nul 2>&1
  timeout /t 3 /nobreak >nul
  goto ended
)
goto wait
:ended
rmdir /s /q "%RUN%\userdata" 2>nul
findstr /c:"snaptest: done" /c:"PASS -" /c:"FAIL -" "%RUN%\out\np_log.txt" 2>nul
exit /b 0
