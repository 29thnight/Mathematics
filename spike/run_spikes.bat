@echo off
REM Phase 0 spike runner: compiles each VecReg storage strategy with MSVC and clang-cl.
REM Build artifacts go outside the OneDrive-synced tree (see docs/PLAN.md risk table).

set "VS=C:\Program Files\Microsoft Visual Studio\18\Community"
set "OUT=%LOCALAPPDATA%\MathfSpike"
set "SRC=%~dp0"

if not exist "%OUT%" mkdir "%OUT%"
call "%VS%\VC\Auxiliary\Build\vcvars64.bat" >nul 2>&1
pushd "%OUT%"

echo === MSVC (cl.exe) ===
for %%S in (strategy_a_m128_member strategy_b_union strategy_c_float4 strategy_d_vecext) do (
    cl /nologo /std:c++20 /O2 /EHsc /arch:AVX2 /c "%SRC%%%S.cpp" /Fo:%%S_msvc.obj >%%S_msvc.log 2>&1
    if errorlevel 1 ( echo   [FAIL] %%S ) else ( echo   [ OK ] %%S )
)

echo === clang-cl ===
set "PATH=%VS%\VC\Tools\Llvm\x64\bin;%PATH%"
for %%S in (strategy_a_m128_member strategy_b_union strategy_c_float4 strategy_d_vecext) do (
    clang-cl /nologo /std:c++20 /O2 /EHsc /arch:AVX2 /c "%SRC%%%S.cpp" /Fo:%%S_clang.obj >%%S_clang.log 2>&1
    if errorlevel 1 ( echo   [FAIL] %%S ) else ( echo   [ OK ] %%S )
)

popd
echo.
echo Logs: %OUT%
