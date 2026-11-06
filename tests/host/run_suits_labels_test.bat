@echo off
rem Builds and runs the costume-select label host test with clang-cl (no CMake).
rem _MSVC_STL_HARDENING turns an out-of-range vector or array index into a fast-fail, so a bounds
rem guard's mutant fails the run instead of reading whatever lies past the end.
setlocal
if not defined DOA4_VSDEVCMD set "DOA4_VSDEVCMD=C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\Common7\Tools\VsDevCmd.bat"
call "%DOA4_VSDEVCMD%" -arch=amd64 -host_arch=amd64 >nul 2>&1
if not defined DOA4_LLVM_BIN set "DOA4_LLVM_BIN=C:\Program Files\LLVM\bin"
set "PATH=%DOA4_LLVM_BIN%;%PATH%"
set "OUT=%TEMP%\doa4_suits_labels_test"
if not exist "%OUT%" mkdir "%OUT%"
clang-cl /nologo /std:c++latest /EHsc /O2 /D_MSVC_STL_HARDENING=1 "%~dp0suits_labels_host_test.cpp" "%~dp0..\..\src\doa4_suits_labels.cpp" "%~dp0..\..\src\doa4_afs_overlay.cpp" /Fo"%OUT%\\" /Fe"%OUT%\suits_labels_host_test.exe"
if errorlevel 1 (
  echo BUILD FAILED
  exit /b 1
)
"%OUT%\suits_labels_host_test.exe"
