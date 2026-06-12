@echo off
setlocal

echo === Landauer Driver Build (Legacy NT) ===
echo.

set "PF86=C:\Program Files (x86)"
set "PF=C:\Program Files"

:: Find vcvars using dir trick to avoid (x86) parens
set "VCVARS=%PF86%\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat"
dir "%VCVARS%" >nul 2>&1 || set "VCVARS=%PF%\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat"
dir "%VCVARS%" >nul 2>&1 || set "VCVARS=%PF86%\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat"
dir "%VCVARS%" >nul 2>&1 || set "VCVARS=%PF%\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat"
dir "%VCVARS%" >nul 2>&1 || (echo ERROR: vcvars64.bat not found & exit /b 1)

echo VS: %VCVARS%
call "%VCVARS%" >nul 2>&1

:: Find SDK
set "SDK_INC="
set "SDK_LIB="
set "KIT_BASE=%PF86%\Windows Kits\10"

dir "%KIT_BASE%\Include\10.0.28000.0\km\ntddk.h" >nul 2>&1 && set "SDK_INC=%KIT_BASE%\Include\10.0.28000.0" && set "SDK_LIB=%KIT_BASE%\Lib\10.0.28000.0"
dir "%KIT_BASE%\Include\10.0.26100.0\km\ntddk.h" >nul 2>&1 && set "SDK_INC=%KIT_BASE%\Include\10.0.26100.0" && set "SDK_LIB=%KIT_BASE%\Lib\10.0.26100.0"

if "%SDK_INC%"=="" (echo ERROR: ntddk.h not found & exit /b 1)

set "KM_INC=%SDK_INC%\km"
set "KM_LIB=%SDK_LIB%\km\x64"

echo KM Include: %KM_INC%
echo KM Lib: %KM_LIB%
echo.

set "OUT=Release"
if "%1"=="debug" set "OUT=Debug"
if not exist "%OUT%" mkdir "%OUT%"

echo Compiling...

:: Response file for cl flags (avoids space-in-path issues)
(
echo /nologo /c /GS- /Gs- /kernel /Zi /Od
echo /D_AMD64_
echo /D_WIN64
echo /DNTDDI_VERSION=NTDDI_WIN10_CO
echo /I"%KM_INC%"
echo /I".."
) > "%OUT%\cflags.rsp"

cl @"%OUT%\cflags.rsp" /Fo"%OUT%\driver.obj" driver.c
if %ERRORLEVEL% NEQ 0 (echo ERROR: driver.c & exit /b 1)
echo   driver.c ok

cl @"%OUT%\cflags.rsp" /Fo"%OUT%\dispatch.obj" dispatch.c
if %ERRORLEVEL% NEQ 0 (echo ERROR: dispatch.c & exit /b 1)
echo   dispatch.c ok

cl @"%OUT%\cflags.rsp" /Fo"%OUT%\pci_provider.obj" pci_provider.c
if %ERRORLEVEL% NEQ 0 (echo ERROR: pci_provider.c & exit /b 1)
echo   pci_provider.c ok

cl @"%OUT%\cflags.rsp" /Fo"%OUT%\bar_table.obj" bar_table.c
if %ERRORLEVEL% NEQ 0 (echo ERROR: bar_table.c & exit /b 1)
echo   bar_table.c ok

echo Linking...
link /nologo /SUBSYSTEM:NATIVE /DRIVER /ENTRY:DriverEntry /OUT:"%OUT%\landauer.sys" /MACHINE:X64 /LIBPATH:"%KM_LIB%" "%OUT%\driver.obj" "%OUT%\dispatch.obj" "%OUT%\pci_provider.obj" "%OUT%\bar_table.obj" ntoskrnl.lib hal.lib

if %ERRORLEVEL% NEQ 0 (echo LINK FAILED. & exit /b 1)

echo.
echo === Build successful ===
dir "%OUT%\landauer.sys"
