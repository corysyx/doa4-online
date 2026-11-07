@echo off
rem Builds and runs the suit converter numeric kit host test with clang-cl (no CMake), under the converter's
rem floating-point flags for every translation unit (x86-64-v3 with FMA switched off, no contraction, no fast-math,
rem no errno). Converter objects go to %TEMP%\doa4_suitconv_test\obj\ (named after their source), where
rem suitconv_fp_scan.py looks for them; the test's own object goes to ...\testobj\.
rem DOA4_SUITCONV_OUT, if set, replaces that output folder (the mutation probes build their copies elsewhere).
rem Prints "PASSED: N checks, 0 failures" (or FAILED) as its last line.
setlocal
if not defined DOA4_VSDEVCMD set "DOA4_VSDEVCMD=C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\Common7\Tools\VsDevCmd.bat"
call "%DOA4_VSDEVCMD%" -arch=amd64 -host_arch=amd64 >nul 2>&1
if not defined DOA4_LLVM_BIN set "DOA4_LLVM_BIN=C:\Program Files\LLVM\bin"
set "PATH=%DOA4_LLVM_BIN%;%PATH%"
set "OUT=%TEMP%\doa4_suitconv_test"
if defined DOA4_SUITCONV_OUT set "OUT=%DOA4_SUITCONV_OUT%"
if not exist "%OUT%\obj" mkdir "%OUT%\obj"
if not exist "%OUT%\testobj" mkdir "%OUT%\testobj"
set "SRC=%~dp0..\..\src"
set FPFLAGS=/clang:-march=x86-64-v3 /clang:-mno-fma /clang:-ffp-contract=off /clang:-fno-fast-math /clang:-fno-math-errno
set CXXFLAGS=/nologo /std:c++latest /EHsc /O2 %FPFLAGS%
if exist "%OUT%\obj\doa4_suitconv_num.obj" del /q "%OUT%\obj\doa4_suitconv_num.obj"
if exist "%OUT%\testobj\suitconv_num_host_test.obj" del /q "%OUT%\testobj\suitconv_num_host_test.obj"
if exist "%OUT%\suitconv_num_host_test.exe" del /q "%OUT%\suitconv_num_host_test.exe"
clang-cl %CXXFLAGS% /c "%SRC%\doa4_suitconv_num.cpp" /Fo"%OUT%\obj\doa4_suitconv_num.obj" > "%OUT%\build.log" 2>&1
if errorlevel 1 goto failed
clang-cl %CXXFLAGS% /c "%~dp0suitconv_num_host_test.cpp" /Fo"%OUT%\testobj\suitconv_num_host_test.obj" >> "%OUT%\build.log" 2>&1
if errorlevel 1 goto failed
clang-cl /nologo "%OUT%\testobj\suitconv_num_host_test.obj" "%OUT%\obj\doa4_suitconv_num.obj" /Fe"%OUT%\suitconv_num_host_test.exe" >> "%OUT%\build.log" 2>&1
if errorlevel 1 goto failed
"%OUT%\suitconv_num_host_test.exe"
exit /b %errorlevel%
:failed
type "%OUT%\build.log"
echo BUILD FAILED
exit /b 1
