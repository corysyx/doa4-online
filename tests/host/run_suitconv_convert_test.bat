@echo off
rem Builds and runs the suit converter's geometry-core host test (convert.py's port at recipe level L0) with clang-cl
rem (no CMake), under the converter's floating-point flags for every translation unit (x86-64-v3 with FMA switched off, no
rem contraction, no fast-math, no errno). Converter objects go to %TEMP%\doa4_suitconv_test\obj\ (named after their
rem source), where suitconv_fp_scan.py scans them before the test runs; the test's own object and the SDK-side sources it
rem links (afs overlay, xdvdfs, gamefiles) go to ...\testobj\.
rem DOA4_SUITCONV_OUT, if set, replaces that output folder (the mutation probes build their copies elsewhere).
rem Environment variables read by the test (all optional, all read-only): DOA4_SUITCONV_DISC (the DOAX2 ISO or an
rem extracted folder), DOA4_SUITCONV_GAME_DIR (the DOA4 game folder), DOA4_SUITCONV_LEVELS_DIR (the oracle's level
rem outputs, <L>\<design>_<colour>.tpr/.cat), DOA4_SUITCONV_TRACE_DIR (the oracle's traces, <L>\<design>_<colour>.trace).
rem DOA4_SUITCONV_ONLY, if set, limits the all-suits sections to the designs it lists (comma separated; diagnosis only).
rem Prints "skipped sections: K" and "PASSED: N checks, 0 failures" (or FAILED) as its last lines.
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
set "LOG=%OUT%\convert_build.log"
for %%f in (num sort tpr recipe inputs core trace catx bust waist hands) do if exist "%OUT%\obj\doa4_suitconv_%%f.obj" del /q "%OUT%\obj\doa4_suitconv_%%f.obj"
if exist "%OUT%\obj\doa4_suitconv.obj" del /q "%OUT%\obj\doa4_suitconv.obj"
if exist "%OUT%\testobj\suitconv_convert_host_test.obj" del /q "%OUT%\testobj\suitconv_convert_host_test.obj"
if exist "%OUT%\suitconv_convert_host_test.exe" del /q "%OUT%\suitconv_convert_host_test.exe"
type nul > "%LOG%"
for %%f in (num sort tpr recipe inputs core trace catx bust waist hands) do (
  clang-cl %CXXFLAGS% /c "%SRC%\doa4_suitconv_%%f.cpp" /Fo"%OUT%\obj\doa4_suitconv_%%f.obj" >> "%LOG%" 2>&1 || goto failed
)
clang-cl %CXXFLAGS% /c "%SRC%\doa4_suitconv.cpp" /Fo"%OUT%\obj\doa4_suitconv.obj" >> "%LOG%" 2>&1
if errorlevel 1 goto failed
clang-cl %CXXFLAGS% /c "%SRC%\doa4_afs_overlay.cpp" /Fo"%OUT%\testobj\doa4_afs_overlay.obj" >> "%LOG%" 2>&1
if errorlevel 1 goto failed
clang-cl %CXXFLAGS% /c "%SRC%\doa4_xdvdfs.cpp" /Fo"%OUT%\testobj\doa4_xdvdfs.obj" >> "%LOG%" 2>&1
if errorlevel 1 goto failed
clang-cl %CXXFLAGS% /c "%SRC%\doa4_gamefiles.cpp" /Fo"%OUT%\testobj\doa4_gamefiles.obj" >> "%LOG%" 2>&1
if errorlevel 1 goto failed
clang-cl %CXXFLAGS% /c "%~dp0suitconv_convert_host_test.cpp" /Fo"%OUT%\testobj\suitconv_convert_host_test.obj" >> "%LOG%" 2>&1
if errorlevel 1 goto failed
clang-cl /nologo "%OUT%\testobj\suitconv_convert_host_test.obj" "%OUT%\obj\doa4_suitconv_num.obj" "%OUT%\obj\doa4_suitconv_sort.obj" "%OUT%\obj\doa4_suitconv_tpr.obj" "%OUT%\obj\doa4_suitconv_recipe.obj" "%OUT%\obj\doa4_suitconv_inputs.obj" "%OUT%\obj\doa4_suitconv_core.obj" "%OUT%\obj\doa4_suitconv_trace.obj" "%OUT%\obj\doa4_suitconv_catx.obj" "%OUT%\obj\doa4_suitconv_bust.obj" "%OUT%\obj\doa4_suitconv_waist.obj" "%OUT%\obj\doa4_suitconv_hands.obj" "%OUT%\obj\doa4_suitconv.obj" "%OUT%\testobj\doa4_afs_overlay.obj" "%OUT%\testobj\doa4_xdvdfs.obj" "%OUT%\testobj\doa4_gamefiles.obj" bcrypt.lib /Fe"%OUT%\suitconv_convert_host_test.exe" >> "%LOG%" 2>&1
if errorlevel 1 goto failed
rem The FP scan of every converter object comes first: any FMA instruction or C runtime math call (or a scan that
rem cannot run) fails the runner with the default summary shape, as one failed check, and the test does not run.
py -3 -I "%~dp0suitconv_fp_scan.py" "%OUT%\obj" > "%OUT%\convert_scan.log" 2>&1
if errorlevel 1 goto scanfailed
type "%OUT%\convert_scan.log"
"%OUT%\suitconv_convert_host_test.exe"
exit /b %errorlevel%
:scanfailed
type "%OUT%\convert_scan.log"
echo FP SCAN FAILED: a converter object has an FMA instruction or a C runtime math call, or the scan could not run
echo FAILED: 1 checks, 1 failures
exit /b 1
:failed
type "%LOG%"
echo BUILD FAILED
exit /b 1
