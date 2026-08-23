@echo off
REM Configure + build + test one preset inside a Visual Studio dev environment.
REM Usage: scripts\build.bat [preset]        (default: msvc-release)
REM
REM Presets: msvc-release, msvc-debug, clang-release, scalar-release,
REM          vs2026-release, vs2026-debug.
REM
REM The vs2026-* names are build presets of the single "vs2026" configure
REM preset -- a Visual Studio solution is multi-config, so one generated
REM solution serves every configuration. The mapping is handled below so the
REM caller can pass one name for all preset families.

setlocal
set "PRESET=%~1"
if "%PRESET%"=="" set "PRESET=msvc-release"
if /i "%PRESET%"=="vs2026" set "PRESET=vs2026-release"

set "CONFIGURE_PRESET=%PRESET%"
if /i "%PRESET:~0,7%"=="vs2026-" set "CONFIGURE_PRESET=vs2026"

call "%~dp0find_vs.bat" || exit /b 1

set "CMAKEDIR=%VSINSTALL%\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin"
set "NINJADIR=%VSINSTALL%\Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja"

call "%VSINSTALL%\VC\Auxiliary\Build\vcvars64.bat" >nul 2>&1
set "PATH=%CMAKEDIR%;%NINJADIR%;%VSINSTALL%\VC\Tools\Llvm\x64\bin;%PATH%"

pushd "%~dp0.."

echo === configure [%CONFIGURE_PRESET%] ===
cmake --preset %CONFIGURE_PRESET% || goto :fail

echo === build [%PRESET%] ===
cmake --build --preset %PRESET% || goto :fail

echo === test [%PRESET%] ===
ctest --preset %PRESET% || goto :fail

popd
echo.
echo BUILD OK [%PRESET%]
exit /b 0

:fail
popd
echo.
echo BUILD FAILED [%PRESET%]
exit /b 1
