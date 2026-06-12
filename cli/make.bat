@echo off
setlocal

echo === Landauer CLI Build ===
echo.

:: Find vcvars64.bat
set "VCVARS="
set "P1=C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat"
set "P2=C:\Program Files\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat"
set "P3=C:\Program Files (x86)\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat"
set "P4=C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat"

dir "%P1%" >nul 2>&1 && set "VCVARS=%P1%"
if "%VCVARS%"=="" dir "%P2%" >nul 2>&1 && set "VCVARS=%P2%"
if "%VCVARS%"=="" dir "%P3%" >nul 2>&1 && set "VCVARS=%P3%"
if "%VCVARS%"=="" dir "%P4%" >nul 2>&1 && set "VCVARS=%P4%"
if "%VCVARS%"=="" (echo ERROR: Visual Studio 2022 not found & exit /b 1)

echo VS: %VCVARS%
call "%VCVARS%" >nul 2>&1

:: msbuild is on PATH after vcvars
where msbuild >nul 2>&1
if %ERRORLEVEL% NEQ 0 (
    echo ERROR: MSBuild not found after vcvars
    exit /b 1
)

set "CFG=Release"
if "%1"=="debug" set "CFG=Debug"

echo Building %CFG%^|x64...
msbuild landauer.vcxproj /p:Configuration=%CFG% /p:Platform=x64 /t:Build /v:minimal

if %ERRORLEVEL% NEQ 0 (
    echo.
    echo BUILD FAILED.
    exit /b %ERRORLEVEL%
)

echo.
echo === Build successful ===
dir /b %CFG%\landauer.exe 2>nul
