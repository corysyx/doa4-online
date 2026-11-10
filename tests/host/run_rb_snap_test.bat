@echo off
rem Builds and runs the rollback save / restore test host test (src\net\rb_snap.cpp and rb_wmap.cpp's pure parts) with clang-cl, no SDK.
setlocal
if not defined DOA4_VSDEVCMD set "DOA4_VSDEVCMD=C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\Common7\Tools\VsDevCmd.bat"
call "%DOA4_VSDEVCMD%" -arch=amd64 -host_arch=amd64 >nul 2>&1
if not defined DOA4_LLVM_BIN set "DOA4_LLVM_BIN=C:\Program Files\LLVM\bin"
set "PATH=%DOA4_LLVM_BIN%;%PATH%"
set "OUT=%TEMP%\doa4_rb_snap_test"
if not exist "%OUT%" mkdir "%OUT%"
if exist "%OUT%\rb_snap_host_test.exe" del "%OUT%\rb_snap_host_test.exe"
clang-cl /nologo /std:c++20 /EHsc /O2 /DDOA4_RB_HOST_TEST /D_MSVC_STL_HARDENING=1 "%~dp0rb_snap_host_test.cpp" "%~dp0..\..\src\net\rb_snap.cpp" "%~dp0..\..\src\net\rb_wmap.cpp" /Fo"%OUT%\\" /Fe"%OUT%\rb_snap_host_test.exe"
if errorlevel 1 (
  echo BUILD FAILED
  exit /b 1
)
"%OUT%\rb_snap_host_test.exe" %*
