@echo off
rem Builds and runs the exclusive fullscreen and output size decisions host test with clang-cl (no CMake).
rem The test includes <rex/ui/exclusive_fullscreen.h> and <rex/ui/display_mode.h> from the SDK the build uses (REXSDK_DIR, default sdk-ou1 beside
rem this worktree's folder).
setlocal
if "%REXSDK_DIR%"=="" set "REXSDK_DIR=%~dp0..\..\..\sdk-ou1"
if not defined DOA4_VSDEVCMD set "DOA4_VSDEVCMD=C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\Common7\Tools\VsDevCmd.bat"
call "%DOA4_VSDEVCMD%" -arch=amd64 -host_arch=amd64 >nul 2>&1
if not defined DOA4_LLVM_BIN set "DOA4_LLVM_BIN=C:\Program Files\LLVM\bin"
set "PATH=%DOA4_LLVM_BIN%;%PATH%"
set "OUT=%TEMP%\doa4_exclusive_mode_test"
if not exist "%OUT%" mkdir "%OUT%"
clang-cl /nologo /std:c++latest /EHsc /O2 /I"%REXSDK_DIR%\include" "%~dp0exclusive_mode_host_test.cpp" /Fo"%OUT%\\" /Fe"%OUT%\exclusive_mode_host_test.exe"
if errorlevel 1 (
  echo BUILD FAILED
  exit /b 1
)
"%OUT%\exclusive_mode_host_test.exe"
