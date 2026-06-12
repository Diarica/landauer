@echo off
setlocal

:: ============================================================
:: uninstall.bat — Stop + uninstall Landauer driver
:: Run as Administrator
:: ============================================================

set "DRV_NAME=landauer"

echo ============================================================
echo  Landauer Uninstall
echo ============================================================
echo.

:: Admin check
net session >nul 2>&1
if %ERRORLEVEL% NEQ 0 (
    echo ERROR: Run as Administrator.
    pause
    exit /b 1
)

:: Stop
echo Stopping driver...
sc stop %DRV_NAME% >nul 2>&1
timeout /t 1 /nobreak >nul

:: Delete
echo Removing driver...
sc delete %DRV_NAME% >nul 2>&1
if %ERRORLEVEL% EQU 0 (
    echo Driver uninstalled successfully.
) else (
    echo Driver was not installed.
)

:: Clean up test certificate (optional)
if exist "%~dp0landauer_test.pfx" (
    echo.
    echo Note: Test certificate files remain:
    echo   %~dp0landauer_test.pfx
    echo   %~dp0landauer_test.cer
    echo Delete them manually if no longer needed.
)

echo.
pause
