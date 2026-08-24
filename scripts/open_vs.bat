@echo off
REM Generate the Visual Studio solution and open it.
REM Usage: scripts\open_vs.bat [--no-open]
REM
REM Generation targets %LOCALAPPDATA%\MathfBuild\vs2026 rather than the repo:
REM this tree is OneDrive-synced, and syncing intermediate build files mid-build
REM corrupts them (docs/PLAN.md risk table).
REM
REM No vcvars call here: the Visual Studio generator drives MSBuild, which finds
REM its own toolset. Only the Ninja presets need a developer environment.

setlocal
call "%~dp0find_vs.bat" || exit /b 1

set "PATH=%VSINSTALL%\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin;%PATH%"
set "SLNDIR=%LOCALAPPDATA%\MathfBuild\vs2026"

pushd "%~dp0.."

echo === generate [vs2026] ===
cmake --preset vs2026 || goto :fail

popd

REM The VS 2026 generator writes the XML solution format (.slnx); older
REM generators write the classic .sln. Accept whichever is there.
set "SLN="
if exist "%SLNDIR%\Mathf.slnx" set "SLN=%SLNDIR%\Mathf.slnx"
if not defined SLN if exist "%SLNDIR%\Mathf.sln" set "SLN=%SLNDIR%\Mathf.sln"

if not defined SLN (
    echo.
    echo ERROR: no solution file under %SLNDIR% after a successful generate.
    exit /b 1
)

echo.
echo SOLUTION: %SLN%

if /i "%~1"=="--no-open" exit /b 0

echo === opening in Visual Studio ===
start "" "%VSINSTALL%\Common7\IDE\devenv.exe" "%SLN%"
exit /b 0

:fail
popd
echo.
echo GENERATE FAILED [vs2026]
exit /b 1
