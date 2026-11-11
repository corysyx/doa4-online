@echo off
rem Builds and runs the language host test with clang-cl (no CMake).
rem toml++ comes from the ReXGlue SDK checkout beside this repo (SettingsFileNotes), as for the toml_upsert test.
setlocal
if not defined DOA4_VSDEVCMD set "DOA4_VSDEVCMD=C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\Common7\Tools\VsDevCmd.bat"
call "%DOA4_VSDEVCMD%" -arch=amd64 -host_arch=amd64 >nul 2>&1
if not defined DOA4_LLVM_BIN set "DOA4_LLVM_BIN=C:\Program Files\LLVM\bin"
set "PATH=%DOA4_LLVM_BIN%;%PATH%"
set "OUT=%TEMP%\doa4_language_test"
if not exist "%OUT%" mkdir "%OUT%"
clang-cl /nologo /std:c++latest /EHsc /O2 -I "%~dp0..\..\..\rexglue-sdk\thirdparty\tomlplusplus\include" "%~dp0language_host_test.cpp" "%~dp0..\..\src\doa4_language.cpp" /Fo"%OUT%\\" /Fe"%OUT%\language_host_test.exe"
if errorlevel 1 (
  echo BUILD FAILED
  exit /b 1
)
"%OUT%\language_host_test.exe"
