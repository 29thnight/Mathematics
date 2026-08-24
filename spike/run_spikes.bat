@echo off
REM Phase 0 spike runner: compiles each vec_reg storage strategy with MSVC and clang-cl.
REM Build artifacts go outside the OneDrive-synced tree (see docs/PLAN.md risk table).

set "vs_install=C:\Program Files\Microsoft Visual Studio\18\Community"
set "output_dir=%LOCALAPPDATA%\MathematicsSpike"
set "source_dir=%~dp0"

if not exist "%output_dir%" mkdir "%output_dir%"
call "%vs_install%\VC\Auxiliary\Build\vcvars64.bat" >nul 2>&1
pushd "%output_dir%"

echo === MSVC (cl.exe) ===
for %%s in (strategy_a_m128_member strategy_b_union strategy_c_float4 strategy_d_vecext) do (
    cl /nologo /std:c++20 /O2 /EHsc /arch:AVX2 /c "%source_dir%%%s.cpp" /Fo:%%s_msvc.obj >%%s_msvc.log 2>&1
    if errorlevel 1 ( echo   [FAIL] %%s ) else ( echo   [ OK ] %%s )
)

echo === clang-cl ===
set "PATH=%vs_install%\VC\Tools\Llvm\x64\bin;%PATH%"
for %%s in (strategy_a_m128_member strategy_b_union strategy_c_float4 strategy_d_vecext) do (
    clang-cl /nologo /std:c++20 /O2 /EHsc /arch:AVX2 /c "%source_dir%%%s.cpp" /Fo:%%s_clang.obj >%%s_clang.log 2>&1
    if errorlevel 1 ( echo   [FAIL] %%s ) else ( echo   [ OK ] %%s )
)

popd
echo.
echo Logs: %output_dir%
