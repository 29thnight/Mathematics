@echo off
REM Configure + build + test one preset inside a Visual Studio dev environment.
REM Usage: scripts\build.bat [preset]        (default: msvc-release)

setlocal
set "PRESET=%~1"
if "%PRESET%"=="" set "PRESET=msvc-release"

set "VS=C:\Program Files\Microsoft Visual Studio\18\Community"
set "CMAKEDIR=%VS%\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin"
set "NINJADIR=%VS%\Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja"

call "%VS%\VC\Auxiliary\Build\vcvars64.bat" >nul 2>&1
set "PATH=%CMAKEDIR%;%NINJADIR%;%VS%\VC\Tools\Llvm\x64\bin;%PATH%"

pushd "%~dp0.."

echo === configure [%PRESET%] ===
cmake --preset %PRESET% || goto :fail

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
