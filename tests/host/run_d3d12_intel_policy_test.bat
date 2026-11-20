@echo off
rem Builds and runs the SDK's Intel render target policy host test (issue #21) with clang-cl (no CMake).
rem The test includes <rex/graphics/d3d12/intel_policy.h> from the SDK the build uses (REXSDK_DIR, default sdk-i21 beside
rem this worktree's folder).
setlocal
if "%REXSDK_DIR%"=="" set "REXSDK_DIR=%~dp0..\..\..\sdk-i21"
if not defined DOA4_VSDEVCMD set "DOA4_VSDEVCMD=C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\Common7\Tools\VsDevCmd.bat"
call "%DOA4_VSDEVCMD%" -arch=amd64 -host_arch=amd64 >nul 2>&1
if not defined DOA4_LLVM_BIN set "DOA4_LLVM_BIN=C:\Program Files\LLVM\bin"
set "PATH=%DOA4_LLVM_BIN%;%PATH%"
set "OUT=%TEMP%\doa4_d3d12_intel_policy_test"
if not exist "%OUT%" mkdir "%OUT%"
clang-cl /nologo /std:c++latest /EHsc /O2 /I"%REXSDK_DIR%\include" "%~dp0d3d12_intel_policy_host_test.cpp" /Fo"%OUT%\\" /Fe"%OUT%\d3d12_intel_policy_host_test.exe"
if errorlevel 1 (
  echo BUILD FAILED
  exit /b 1
)
"%OUT%\d3d12_intel_policy_host_test.exe"
