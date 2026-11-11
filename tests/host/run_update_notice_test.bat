@echo off
rem Builds and runs the start-up update notice's host test (src/doa4_update_notice.h/.cpp, Task 15) with clang-cl (no CMake):
rem doa4_update_notice.cpp over np_rules.cpp and np_wire.cpp (versions: rules::ParseVersion, CompareVersions).
rem No network and no game: every file it writes is under %%TEMP%%\doa4_update_notice_<pid>\.
rem UPDATE_NOTICE_OUT (optional) names the output folder, so that mutation probes never share one.
setlocal
if not defined DOA4_VSDEVCMD set "DOA4_VSDEVCMD=C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\Common7\Tools\VsDevCmd.bat"
call "%DOA4_VSDEVCMD%" -arch=amd64 -host_arch=amd64 >nul 2>&1
if not defined DOA4_LLVM_BIN set "DOA4_LLVM_BIN=C:\Program Files\LLVM\bin"
set "PATH=%DOA4_LLVM_BIN%;%PATH%"
if not defined UPDATE_NOTICE_OUT set "UPDATE_NOTICE_OUT=%TEMP%\doa4_update_notice_test"
set "OUT=%UPDATE_NOTICE_OUT%"
if not exist "%OUT%" mkdir "%OUT%"
if exist "%OUT%\update_notice_host_test.exe" del "%OUT%\update_notice_host_test.exe"
clang-cl /nologo /std:c++latest /EHsc /O2 /D_MSVC_STL_HARDENING=1 /D_WIN32_WINNT=0x0A00 /DWIN32_LEAN_AND_MEAN /DNOMINMAX "%~dp0update_notice_host_test.cpp" "%~dp0..\..\src\doa4_update_notice.cpp" "%~dp0..\..\src\net\np_rules.cpp" "%~dp0..\..\src\net\np_wire.cpp" /Fo"%OUT%\\" /Fe"%OUT%\update_notice_host_test.exe"
if errorlevel 1 (
  echo BUILD FAILED
  exit /b 1
)
"%OUT%\update_notice_host_test.exe" %*
