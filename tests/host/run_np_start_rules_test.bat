@echo off
rem Builds and runs the netplay starting-rules host test with clang-cl (no CMake): np_start_rules.cpp only (its header
rem includes np_handoff.h for handoff::File, a plain struct; nothing of np_handoff.cpp is called).
rem _MSVC_STL_HARDENING makes an out-of-range span or vector index fast-fail, so an edit past a record's end is a red
rem run, not a lucky green one. Pass -v to print each check group before it runs. Synthetic bytes only.
setlocal
if not defined DOA4_VSDEVCMD set "DOA4_VSDEVCMD=C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\Common7\Tools\VsDevCmd.bat"
call "%DOA4_VSDEVCMD%" -arch=amd64 -host_arch=amd64 >nul 2>&1
if not defined DOA4_LLVM_BIN set "DOA4_LLVM_BIN=C:\Program Files\LLVM\bin"
set "PATH=%DOA4_LLVM_BIN%;%PATH%"
set "OUT=%TEMP%\doa4_np_start_rules_test"
if not exist "%OUT%" mkdir "%OUT%"
clang-cl /nologo /std:c++latest /EHsc /O2 /D_MSVC_STL_HARDENING=1 "%~dp0np_start_rules_host_test.cpp" "%~dp0..\..\src\net\np_start_rules.cpp" /Fo"%OUT%\\" /Fe"%OUT%\np_start_rules_host_test.exe"
if errorlevel 1 (
  echo BUILD FAILED
  exit /b 1
)
"%OUT%\np_start_rules_host_test.exe" %*
