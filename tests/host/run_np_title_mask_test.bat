@echo off
rem Builds and runs the netplay title-mask host test with clang-cl (no CMake): src/net/np_title_mask.h is header-only
rem and includes np_core.h for Pad only (no np_core.cpp is called). _MSVC_STL_HARDENING as in the other np runners.
rem Pass -v to print each check group before it runs.
setlocal
if not defined DOA4_VSDEVCMD set "DOA4_VSDEVCMD=C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\Common7\Tools\VsDevCmd.bat"
call "%DOA4_VSDEVCMD%" -arch=amd64 -host_arch=amd64 >nul 2>&1
if not defined DOA4_LLVM_BIN set "DOA4_LLVM_BIN=C:\Program Files\LLVM\bin"
set "PATH=%DOA4_LLVM_BIN%;%PATH%"
set "OUT=%TEMP%\doa4_np_title_mask_test"
if not exist "%OUT%" mkdir "%OUT%"
clang-cl /nologo /std:c++latest /EHsc /O2 /D_MSVC_STL_HARDENING=1 "%~dp0np_title_mask_host_test.cpp" /Fo"%OUT%\\" /Fe"%OUT%\np_title_mask_host_test.exe"
if errorlevel 1 (
  echo BUILD FAILED
  exit /b 1
)
"%OUT%\np_title_mask_host_test.exe" %*
