@echo off
rem Builds and runs the bust morph host test with clang-cl (no CMake).
setlocal
if not defined DOA4_VSDEVCMD set "DOA4_VSDEVCMD=C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\Common7\Tools\VsDevCmd.bat"
call "%DOA4_VSDEVCMD%" -arch=amd64 -host_arch=amd64 >nul 2>&1
if not defined DOA4_LLVM_BIN set "DOA4_LLVM_BIN=C:\Program Files\LLVM\bin"
set "PATH=%DOA4_LLVM_BIN%;%PATH%"
set "OUT=%TEMP%\doa4_suits_bust_test"
if not exist "%OUT%" mkdir "%OUT%"
clang-cl /nologo /std:c++latest /EHsc /O2 /I"%~dp0..\..\src" "%~dp0suits_bust_test.cpp" /Fo"%OUT%\\" /Fe"%OUT%\suits_bust_test.exe"
if errorlevel 1 (
  echo BUILD FAILED
  exit /b 1
)
"%OUT%\suits_bust_test.exe"
