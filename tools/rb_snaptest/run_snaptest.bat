@echo off
rem DOA4 rollback milestone 2: the save / restore test (src\net\rb_snap.h). Put this file and snaptest_single.txt in
rem the game folder (next to dead_or_alive_4.exe, alpha-v0.3.123 or later) and double-click it. The game plays a scripted
rem Versus fight by itself two times (about 2 minutes each), rewinds it 12 times each time and checks that every replay
rem comes out the same (the D3D device's and the XMA decoder's state go back with the snapshot): from frame 3600,
rem with graphics memory put back too (round 21): from frame 3600 watching the last words that differ, then from 4096.
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
echo The game now plays a fight by itself, two times, and rewinds it a few times (about 8 minutes in all).
echo Do not touch it: it closes by itself each time.
for %%M in (a3600w b4096phys) do call :run %%M
if exist "%HERE%snaptest_results.zip" del "%HERE%snaptest_results.zip"
powershell -NoProfile -Command "Compress-Archive -Path '%HERE%snaptest_run_a3600w','%HERE%snaptest_run_b4096phys' -DestinationPath '%HERE%snaptest_results.zip'" >nul 2>&1
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
set "DOA4_NP_SNAPTEST_FENCE=rewind"
set "DOA4_NP_SNAPTEST_DEVICE=state"
set "DOA4_NP_SNAPTEST=1"
if "%1"=="b4096phys" set "DOA4_NP_SNAPTEST=4096,8,240,12"
set "DOA4_NP_SNAPTEST_MODE=marked"
if "%1"=="b3600full" set "DOA4_NP_SNAPTEST_MODE=full"
if "%1"=="c3600both" set "DOA4_NP_SNAPTEST_MODE=full"
set "DOA4_NP_SNAPTEST_PHYS=restore"
set "DOA4_NP_SNAPTEST_WATCH="
if "%1"=="a3600w" set "DOA4_NP_SNAPTEST_WATCH=82F6C9D0-82F6C9D8,8357A5E0-8357A5F0,8357A7E0-8357A7E8,8357A818-8357A820,8357C190-8357C194"
if "%1"=="c3600both" set "DOA4_NP_SNAPTEST_PHYS=restore"
set "DOA4_NP_SNAPTEST_AUDIO="
set "DOA4_NP_SNAPTEST_DEVLIVE="
set "REX_LOG_LAZY_OBJECTS=1"
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
