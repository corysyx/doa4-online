@echo off
rem DOA4 rollback milestone 2: the save / restore test (src\net\rb_snap.h). Put this file and snaptest_single.txt in
rem the game folder (next to dead_or_alive_4.exe, alpha-v0.3.103 or later) and double-click it. The game plays a scripted
rem Versus fight by itself (about 2 minutes), rewinds it 12 times and checks that every replay comes out the same.
rem Your own save is not used or changed: the run gets a fresh, empty user data folder.
setlocal
cd /d "%~dp0"
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
set "RUN=%~dp0snaptest_run"
if exist "%RUN%" rmdir /s /q "%RUN%"
mkdir "%RUN%\userdata"
mkdir "%RUN%\out"
set "DOA4_NP=record"
set "DOA4_NP_DIR=%RUN%\out"
set "DOA4_NP_SCRIPT=%~dp0snaptest_single.txt"
if not defined DOA4_NP_SNAPTEST set "DOA4_NP_SNAPTEST=1"
set "SDL_GAMECONTROLLER_IGNORE_DEVICES_EXCEPT=0x0000/0x0000"
set "CACHE=%USERPROFILE%\Documents\dead_or_alive_4\cache"
set "CACHEARG="
if exist "%CACHE%" set "CACHEARG=--cache_path=%CACHE%"
echo The game now plays a fight by itself and rewinds it a few times (about 2 minutes).
echo Do not touch it: it closes by itself when the test is done.
start "" /wait "%~dp0dead_or_alive_4.exe" "--user_data_root=%RUN%\userdata" "--log_file=%RUN%\game.log" --headless=true %CACHEARG%
if exist "%~dp0snaptest_results.zip" del "%~dp0snaptest_results.zip"
powershell -NoProfile -Command "Compress-Archive -Path '%RUN%\game.log','%RUN%\out\*.txt' -DestinationPath '%~dp0snaptest_results.zip'" >nul 2>&1
echo.
if exist "%~dp0snaptest_results.zip" (
  echo DONE. Send snaptest_results.zip from the game folder.
) else (
  echo DONE, but the results could not be zipped: send the files in the snaptest_run folder ^(game.log and out\np_log.txt^).
)
findstr /c:"snaptest: done" /c:"snaptest: test" "%RUN%\out\np_log.txt" 2>nul
pause
