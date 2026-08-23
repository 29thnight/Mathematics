@echo off
REM Emits assembly listings for the codegen spike so Mathf's output can be
REM diffed against DirectXMath's, function by function.

set "VS=C:\Program Files\Microsoft Visual Studio\18\Community"
set "OUT=%LOCALAPPDATA%\MathfSpike"
set "SRC=%~dp0"

if not exist "%OUT%" mkdir "%OUT%"
call "%VS%\VC\Auxiliary\Build\vcvars64.bat" >nul 2>&1
pushd "%OUT%"

REM NOTE: /Fa takes no colon separator (unlike /Fo). /Fa:name silently misbehaves.
echo === MSVC /FA ===
cl /nologo /std:c++20 /O2 /EHsc /arch:AVX2 /fp:fast /c "%SRC%codegen_vs_dxmath.cpp" ^
   /FAs /Facodegen_msvc.asm /Focodegen_msvc.obj >codegen_msvc.log 2>&1
if exist codegen_msvc.asm ( echo   [ OK ] codegen_msvc.asm ) else ( echo   [FAIL] see codegen_msvc.log )

echo === clang-cl -S ===
set "PATH=%VS%\VC\Tools\Llvm\x64\bin;%PATH%"
clang-cl /nologo /std:c++20 /O2 /EHsc /arch:AVX2 /fp:fast /c "%SRC%codegen_vs_dxmath.cpp" ^
   /FA /Facodegen_clang.asm /Focodegen_clang.obj >codegen_clang.log 2>&1
if exist codegen_clang.asm ( echo   [ OK ] codegen_clang.asm ) else ( echo   [FAIL] see codegen_clang.log )

REM The prototypes above are historical; this one checks the shipping headers.
echo === library vs DirectXMath (MSVC) ===
cl /nologo /std:c++20 /O2 /EHsc /arch:AVX2 /fp:fast /utf-8 /I"%SRC%..\include" ^
   /c "%SRC%codegen_library.cpp" /FAs /Falibrary_msvc.asm /Folibrary_msvc.obj >library_msvc.log 2>&1
if exist library_msvc.asm ( echo   [ OK ] library_msvc.asm ) else ( echo   [FAIL] see library_msvc.log )

echo === library vs DirectXMath (clang-cl) ===
clang-cl /nologo /std:c++20 /O2 /EHsc /arch:AVX2 /fp:fast /utf-8 /I"%SRC%..\include" ^
   /c "%SRC%codegen_library.cpp" /FA /Falibrary_clang.asm /Folibrary_clang.obj >library_clang.log 2>&1
if exist library_clang.asm ( echo   [ OK ] library_clang.asm ) else ( echo   [FAIL] see library_clang.log )

popd
echo Output: %OUT%
