@echo off
rem Builds and runs the lockstep port's vertex-stream pass host test with clang-cl (no CMake): np_vsm_pure.cpp, carried over from the
rem measurement branch's np_rb0_vsm_pure.cpp (no SDK, no guest includes). _MSVC_STL_HARDENING makes an out-of-range index fast-fail.
rem Pass -v to print each check group before it runs.
setlocal
if not defined DOA4_VSDEVCMD set "DOA4_VSDEVCMD=C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\Common7\Tools\VsDevCmd.bat"
call "%DOA4_VSDEVCMD%" -arch=amd64 -host_arch=amd64 >nul 2>&1
if not defined DOA4_LLVM_BIN set "DOA4_LLVM_BIN=C:\Program Files\LLVM\bin"
set "PATH=%DOA4_LLVM_BIN%;%PATH%"
set "OUT=%TEMP%\doa4_np_vsm_test"
if not exist "%OUT%" mkdir "%OUT%"
clang-cl /nologo /std:c++latest /EHsc /O2 /D_MSVC_STL_HARDENING=1 "%~dp0np_vsm_host_test.cpp" "%~dp0..\..\src\net\np_vsm_pure.cpp" /Fo"%OUT%\\" /Fe"%OUT%\np_vsm_host_test.exe"
if not %errorlevel%==0 (
  echo BUILD FAILED
  exit /b 1
)
"%OUT%\np_vsm_host_test.exe" %*
