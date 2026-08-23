@echo off
REM Locate a Visual Studio install with the C++ toolset and export VSINSTALL.
REM
REM Sourced by the other scripts, so it deliberately does not use setlocal:
REM     call "%~dp0find_vs.bat" || exit /b 1
REM
REM vswhere ships with every VS installer since 2017 and lives at a fixed path,
REM which is why it is preferred over hardcoding an install directory: the
REM edition (Community/Professional/Enterprise) and the version directory both
REM vary per machine.

set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
set "VSINSTALL="

if exist "%VSWHERE%" (
    for /f "usebackq tokens=*" %%i in (`
        "%VSWHERE%" -latest -prerelease -products *
        -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64
        -property installationPath
    `) do set "VSINSTALL=%%i"
)

if not defined VSINSTALL (
    echo ERROR: no Visual Studio installation with the C++ toolset was found.
    echo        Install the "Desktop development with C++" workload
    echo        ^(Visual Studio 2026 recommended^) and try again.
    exit /b 1
)

exit /b 0
