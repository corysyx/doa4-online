@echo off
rem Builds and runs the issue #10 stage-wait host test with clang-cl (no CMake): np_stage_wait_pure.h, the bookkeeping of the main
rem thread's wait for the stage workers (no SDK, no guest includes). Pass -v to print each check group and its check count.
setlocal
if not defined DOA4_VSDEVCMD set "DOA4_VSDEVCMD=C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\Common7\Tools\VsDevCmd.bat"
call "%DOA4_VSDEVCMD%" -arch=amd64 -host_arch=amd64 >nul 2>&1
if not defined DOA4_LLVM_BIN set "DOA4_LLVM_BIN=C:\Program Files\LLVM\bin"
set "PATH=%DOA4_LLVM_BIN%;%PATH%"
set "OUT=%TEMP%\doa4_np_stage_wait_test"
if not exist "%OUT%" mkdir "%OUT%"
clang-cl /nologo /std:c++latest /EHsc /O2 /D_MSVC_STL_HARDENING=1 "%~dp0np_stage_wait_host_test.cpp" /Fo"%OUT%\\" /Fe"%OUT%\np_stage_wait_host_test.exe"
if not %errorlevel%==0 (
  echo BUILD FAILED
  exit /b 1
)
"%OUT%\np_stage_wait_host_test.exe" %*
