@echo off
REM Calypso - Windows convenience build script
REM Attempts to find MSVC (via vcvarsall), falls back to MinGW g++

setlocal enabledelayedexpansion

set "OUTDIR=build"
if not exist "%OUTDIR%" mkdir "%OUTDIR%"

REM Check if cl.exe is available
where cl.exe >nul 2>nul
if %ERRORLEVEL% == 0 (
    echo [*] Found MSVC cl.exe, building with MSVC...
    goto :build_msvc
)

REM Try to find vcvarsall.bat
set "VCVARS="
for /f "tokens=*" %%i in ('"%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath 2^>nul') do (
    set "VCVARS=%%i\VC\Auxiliary\Build\vcvarsall.bat"
)

if defined VCVARS if exist "!VCVARS!" (
    echo [*] Found vcvarsall at: !VCVARS!
    call "!VCVARS!" x64 >nul 2>nul
    goto :build_msvc
)

REM Try MinGW
where g++.exe >nul 2>nul
if %ERRORLEVEL% == 0 (
    echo [*] Found MinGW g++, building with MinGW...
    goto :build_mingw
)

echo [!] Error: No C++ compiler found. Install MSVC or MinGW.
exit /b 1

:build_msvc
cl.exe /std:c++latest /EHsc /O2 /W3 /DWIN32 /D_WINDOWS /DNOMINMAX ^
    /Iinclude /Ithird_party/tiny-aes /Ithird_party/miniz /Ithird_party/lz4 ^
    src\main.cpp src\cli.cpp src\crypto.cpp src\encoding.cpp ^
    src\compression.cpp src\payload.cpp src\pe_parser.cpp ^
    src\stub_generator.cpp src\compiler.cpp ^
    third_party\tiny-aes\aes.c third_party\miniz\miniz.c third_party\lz4\lz4.c ^
    /Fe:%OUTDIR%\Calypso.exe ^
    /link /SUBSYSTEM:CONSOLE bcrypt.lib advapi32.lib
if %ERRORLEVEL% neq 0 (
    echo [!] Build failed.
    exit /b 1
)
echo [+] Build successful: %OUTDIR%\Calypso.exe
goto :end

:build_mingw
g++.exe -std=c++23 -O2 -Wall -DWIN32 -D_WINDOWS -DNOMINMAX ^
    -Iinclude -Ithird_party/tiny-aes -Ithird_party/miniz -Ithird_party/lz4 ^
    src/main.cpp src/cli.cpp src/crypto.cpp src/encoding.cpp ^
    src/compression.cpp src/payload.cpp src/pe_parser.cpp ^
    src/stub_generator.cpp src/compiler.cpp ^
    third_party/tiny-aes/aes.c third_party/miniz/miniz.c third_party/lz4/lz4.c ^
    -o %OUTDIR%/Calypso.exe ^
    -lbcrypt -ladvapi32
if %ERRORLEVEL% neq 0 (
    echo [!] Build failed.
    exit /b 1
)
echo [+] Build successful: %OUTDIR%\Calypso.exe
goto :end

:end
endlocal
