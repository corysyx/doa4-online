@echo off
rem Builds the netplay worktree this script lives in (docs/BUILDING.md section 6), with the SDK from DOA4_SDK
rem (default: sdk-np1 beside this worktree, in forward slashes as docs/BUILDING.md section 6 asks).
rem Usage: np_build.bat configure | build [snapshot-tag]
rem With a tag, the exe and its two DLLs are copied to out\build\win-amd64-release\np_bin\<tag>\.
rem No errorlevel variable is read inside a parenthesised block: cmd expands it when the block is read.
rem PGO (docs/PGO.md), configure only. DOA4_PGO unset: tools\pgo_source_id.py check decides (pgo\doa4.profdata is
rem used when pgo\source.txt names this tree and the SDK checkout's HEAD; neither file: a plain build; anything else:
rem refused). DOA4_PGO=off: a plain build. DOA4_PGO=gen:<folder>: the instrumented build, which writes its profiles
rem into <folder> (never timed or shipped). Every configure passes all four compiler and linker flag variables, so
rem none keeps a value cached by an earlier configure of the same build folder. Both profile modes pass
rem -mllvm -static-func-full-module-prefix=false: a file-local function's profile name is then <file name>;<function>
rem instead of <absolute path>;<function>, so a profile made here matches a build at any other path (CI).
set "PGO_GEN_DIR="
if not defined DOA4_PGO goto pgo_dir_done
if /i not "%DOA4_PGO:~0,4%"=="gen:" goto pgo_dir_done
set "PGO_GEN_DIR=%DOA4_PGO:~4%"
if not defined PGO_GEN_DIR goto pgo_dir_done
for %%I in ("%PGO_GEN_DIR%") do set "PGO_GEN_DIR=%%~fI"
set "PGO_GEN_DIR=%PGO_GEN_DIR:\=/%"
:pgo_dir_done
if defined DOA4_SDK goto sdk_set
for %%I in ("%~dp0..\..\sdk-np1") do set "DOA4_SDK=%%~fI"
set "DOA4_SDK=%DOA4_SDK:\=/%"
:sdk_set
call "C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\Common7\Tools\VsDevCmd.bat" -arch=amd64 -host_arch=amd64 >nul
set "PATH=C:\Program Files\LLVM\bin;%PATH%"
cd /d "%~dp0.."
if not "%1"=="configure" goto build
set "PGO_C="
set "PGO_SHLINK="
if not defined DOA4_PGO goto pgo_check
if /i "%DOA4_PGO%"=="off" goto pgo_off
if defined PGO_GEN_DIR goto pgo_gen
echo pgo: refused: DOA4_PGO=%DOA4_PGO% is not off or gen:^<folder^>
exit /b 1
:pgo_off
echo pgo: off (DOA4_PGO=off: a plain build)
goto pgo_ready
:pgo_gen
set "PGO_C= -fprofile-generate=%PGO_GEN_DIR% -DDOA4_PGO_DUMP -mllvm -static-func-full-module-prefix=false"
set "PGO_SHLINK=-Wl,/force:unresolved -Wl,/export:__llvm_profile_dump"
echo pgo: gen %PGO_GEN_DIR% (the instrumented build: never timed, gated or shipped)
goto pgo_ready
:pgo_check
if not exist out mkdir out
py -3 -I "%CD%\tools\pgo_source_id.py" check --sdk-from "worktree:%DOA4_SDK%" > out\pgo_check.txt
set "PGO_RC=%errorlevel%"
type out\pgo_check.txt
if not "%PGO_RC%"=="0" exit /b 1
for /f "usebackq tokens=1,2,*" %%a in ("out\pgo_check.txt") do if "%%a %%b"=="pgo: use" set "PGO_C= -fprofile-use=%%c -mllvm -static-func-full-module-prefix=false"
rem The guard: findstr reads the check's file apart from the for /f, so a 'pgo: use' line that set no flag stops here.
set "PGO_USE_LINE="
findstr /b /l /c:"pgo: use " out\pgo_check.txt >nul && set "PGO_USE_LINE=1"
if defined PGO_USE_LINE if not defined PGO_C echo pgo: use line read but no flag set
if defined PGO_USE_LINE if not defined PGO_C exit /b 1
:pgo_ready
cmake --preset win-amd64-release "-DREXSDK_DIR=%DOA4_SDK%" "-DCMAKE_C_FLAGS=-march=x86-64-v3%PGO_C%" "-DCMAKE_CXX_FLAGS=-march=x86-64-v3%PGO_C%" "-DCMAKE_EXE_LINKER_FLAGS=" "-DCMAKE_SHARED_LINKER_FLAGS=%PGO_SHLINK%" "-DDOA4_RELEASE_CHANNEL=alpha"
if errorlevel 1 exit /b 1
exit /b 0
:build
cmake --build --preset win-amd64-release
if errorlevel 1 exit /b 1
if "%2"=="" exit /b 0
set "BIN=%CD%\out\build\win-amd64-release"
if exist "%BIN%\np_bin\%2" goto exists
mkdir "%BIN%\np_bin\%2"
copy /y "%BIN%\dead_or_alive_4.exe" "%BIN%\np_bin\%2\" >nul
if errorlevel 1 exit /b 1
copy /y "%BIN%\rexruntime.dll" "%BIN%\np_bin\%2\" >nul
if errorlevel 1 exit /b 1
copy /y "%BIN%\TracyClient.dll" "%BIN%\np_bin\%2\" >nul
if errorlevel 1 exit /b 1
echo snapshot %BIN%\np_bin\%2
exit /b 0
:exists
echo snapshot %2 exists
exit /b 1
