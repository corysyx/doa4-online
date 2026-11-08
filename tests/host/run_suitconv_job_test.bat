@echo off
rem Builds and runs the suit importer's job host test (src/doa4_suitconv_job.cpp: worker, progress, cancel, checked writes and
rem the swap) with clang-cl (no CMake), under the converter's floating-point flags for every translation unit (x86-64-v3 with
rem FMA switched off, no contraction, no fast-math, no errno). Converter objects, the job's included, go to
rem %TEMP%\doa4_suitconv_test\obj\ (named after their source), where suitconv_fp_scan.py scans them before the test runs; the
rem test's own object and the SDK-side sources it links (afs overlay, xdvdfs, gamefiles, manifest, the import loader, the costume tables) go to ...\testobj\.
rem DOA4_SUITCONV_OUT, if set, replaces that output folder (the mutation probes build their copies elsewhere).
rem DOA4_SUITCONV_REUSE=1 (the probes): a converter object that already exists in the output folder is not rebuilt, except
rem the job's own.
rem Environment variables read by the test (all optional, all read-only): DOA4_SUITCONV_DISC (the DOAX2 ISO or an extracted
rem folder), DOA4_SUITCONV_GAME_DIR (the DOA4 game folder) and DOA4_SUITCONV_ORACLE_DIR (the 68 suit files and manifest.txt the
rem reference converter wrote): with all three set the test also runs the real sections (real imports into
rem %TEMP%\doa4_suitconv_real\<case>\ud, compared with the reference file by file; determinism across worker counts, order, FMA3
rem and control-word state; timing and memory). With DOA4_SUITCONV_ONLY=real only those sections run (a run of its own, so the
rem memory figures belong to the imports alone); =fake runs only the fake-world groups; =<section name> only that section.
rem Prints "skipped sections: K", the slowest cancel latency and "PASSED: N checks, 0 failures" (or FAILED) as its last lines.
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
set "LOG=%OUT%\job_build.log"
if exist "%OUT%\obj\doa4_suitconv_job.obj" del /q "%OUT%\obj\doa4_suitconv_job.obj"
if exist "%OUT%\testobj\suitconv_job_host_test.obj" del /q "%OUT%\testobj\suitconv_job_host_test.obj"
if exist "%OUT%\suitconv_job_host_test.exe" del /q "%OUT%\suitconv_job_host_test.exe"
if not "%DOA4_SUITCONV_REUSE%"=="1" (
  for %%f in (num sort tpr recipe inputs core trace catx bust waist hands cloth gencloth) do if exist "%OUT%\obj\doa4_suitconv_%%f.obj" del /q "%OUT%\obj\doa4_suitconv_%%f.obj"
  if exist "%OUT%\obj\doa4_suitconv.obj" del /q "%OUT%\obj\doa4_suitconv.obj"
)
type nul > "%LOG%"
for %%f in (num sort tpr recipe inputs core trace catx bust waist hands cloth gencloth) do (
  if not exist "%OUT%\obj\doa4_suitconv_%%f.obj" clang-cl %CXXFLAGS% /c "%SRC%\doa4_suitconv_%%f.cpp" /Fo"%OUT%\obj\doa4_suitconv_%%f.obj" >> "%LOG%" 2>&1 || goto failed
)
if not exist "%OUT%\obj\doa4_suitconv.obj" (
  clang-cl %CXXFLAGS% /c "%SRC%\doa4_suitconv.cpp" /Fo"%OUT%\obj\doa4_suitconv.obj" >> "%LOG%" 2>&1 || goto failed
)
clang-cl %CXXFLAGS% /c "%SRC%\doa4_suitconv_job.cpp" /Fo"%OUT%\obj\doa4_suitconv_job.obj" >> "%LOG%" 2>&1
if errorlevel 1 goto failed
for %%f in (afs_overlay xdvdfs gamefiles suits_manifest suits costumes) do (
  clang-cl %CXXFLAGS% /c "%SRC%\doa4_%%f.cpp" /Fo"%OUT%\testobj\doa4_%%f.obj" >> "%LOG%" 2>&1 || goto failed
)
clang-cl %CXXFLAGS% /c "%~dp0suitconv_job_host_test.cpp" /Fo"%OUT%\testobj\suitconv_job_host_test.obj" >> "%LOG%" 2>&1
if errorlevel 1 goto failed
clang-cl /nologo "%OUT%\testobj\suitconv_job_host_test.obj" "%OUT%\obj\doa4_suitconv_num.obj" "%OUT%\obj\doa4_suitconv_sort.obj" "%OUT%\obj\doa4_suitconv_tpr.obj" "%OUT%\obj\doa4_suitconv_recipe.obj" "%OUT%\obj\doa4_suitconv_inputs.obj" "%OUT%\obj\doa4_suitconv_core.obj" "%OUT%\obj\doa4_suitconv_trace.obj" "%OUT%\obj\doa4_suitconv_catx.obj" "%OUT%\obj\doa4_suitconv_bust.obj" "%OUT%\obj\doa4_suitconv_waist.obj" "%OUT%\obj\doa4_suitconv_hands.obj" "%OUT%\obj\doa4_suitconv_cloth.obj" "%OUT%\obj\doa4_suitconv_gencloth.obj" "%OUT%\obj\doa4_suitconv.obj" "%OUT%\obj\doa4_suitconv_job.obj" "%OUT%\testobj\doa4_afs_overlay.obj" "%OUT%\testobj\doa4_xdvdfs.obj" "%OUT%\testobj\doa4_gamefiles.obj" "%OUT%\testobj\doa4_suits_manifest.obj" "%OUT%\testobj\doa4_suits.obj" "%OUT%\testobj\doa4_costumes.obj" bcrypt.lib /Fe"%OUT%\suitconv_job_host_test.exe" >> "%LOG%" 2>&1
if errorlevel 1 goto failed
rem The FP scan of every converter object comes first: any FMA instruction or C runtime math call (or a scan that
rem cannot run) fails the runner with the default summary shape, as one failed check, and the test does not run.
py -3 -I "%~dp0suitconv_fp_scan.py" "%OUT%\obj" > "%OUT%\job_scan.log" 2>&1
if errorlevel 1 goto scanfailed
type "%OUT%\job_scan.log"
"%OUT%\suitconv_job_host_test.exe"
exit /b %errorlevel%
:scanfailed
type "%OUT%\job_scan.log"
echo FP SCAN FAILED: a converter object has an FMA instruction or a C runtime math call, or the scan could not run
echo FAILED: 1 checks, 1 failures
exit /b 1
:failed
type "%LOG%"
echo BUILD FAILED
exit /b 1
