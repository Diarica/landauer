@echo off
setlocal enabledelayedexpansion

:: ============================================================
:: install.bat — One-click: build + sign + install + start
:: Run as Administrator
:: ============================================================

set "DRV_NAME=landauer"
set "DRV_DIR=%~dp0driver"
set "CLI_DIR=%~dp0cli"
set "DRV_PATH=%DRV_DIR%\Release\landauer.sys"
set "CLI_PATH=%CLI_DIR%\Release\landauer.exe"
set "CERT_PFX=%~dp0landauer_test.pfx"

echo ============================================================
echo  Landauer One-Click Install
echo ============================================================
echo.

:: Admin check
net session >nul 2>&1
if %ERRORLEVEL% NEQ 0 (
    echo ERROR: Run as Administrator.
    pause
    exit /b 1
)

:: Testsigning check
bcdedit /enum | findstr /C:"testsigning" | findstr /C:"Yes" >nul
if %ERRORLEVEL% NEQ 0 (
    echo WARNING: testsigning is OFF.
    echo Run: bcdedit /set testsigning on
    echo Then REBOOT and run this script again.
    echo.
    pause
    exit /b 1
)

:: ======== STEP 1: STOP any existing driver ========
echo [1/5] Stopping existing driver...
sc stop %DRV_NAME% >nul 2>&1
sc delete %DRV_NAME% >nul 2>&1
timeout /t 1 /nobreak >nul
echo   Done.

:: ======== STEP 2: BUILD driver + CLI ========
echo.
echo [2/5] Building driver...
pushd "%DRV_DIR%"
call make.bat
set DRV_OK=%ERRORLEVEL%
popd
if %DRV_OK% NEQ 0 (echo ERROR: Driver build failed. & pause & exit /b 1)

echo.
echo [2/5] Building CLI...
pushd "%CLI_DIR%"
call make.bat
set CLI_OK=%ERRORLEVEL%
popd
if %CLI_OK% NEQ 0 (echo ERROR: CLI build failed. & pause & exit /b 1)

:: ======== STEP 3: SIGN ========
echo.
echo [3/5] Signing driver...

:: Find signtool
set "SIGNTOOL="
for /d %%d in ("C:\PROGRA~2\Windows Kits\10\bin\10.*") do (
    if exist "%%d\x64\signtool.exe" set "SIGNTOOL=%%d\x64\signtool.exe"
)
if "%SIGNTOOL%"=="" (
    for /d %%d in ("C:\Program Files (x86)\Windows Kits\10\bin\10.*") do (
        dir "%%d\x64\signtool.exe" >nul 2>&1 && set "SIGNTOOL=%%d\x64\signtool.exe"
    )
)
if "%SIGNTOOL%"=="" (echo ERROR: signtool.exe not found. & pause & exit /b 1)

:: Create cert if needed
if not exist "%CERT_PFX%" (
    powershell -Command "$c=New-SelfSignedCertificate -Type CodeSigningCert -Subject 'CN=Landauer' -KeyUsage DigitalSignature -CertStoreLocation 'Cert:\CurrentUser\My'; $p=ConvertTo-SecureString -String 'landauer' -Force -AsPlainText; Export-PfxCertificate -Cert $c -FilePath '%CERT_PFX%' -Password $p"
)

:: Sign
"%SIGNTOOL%" sign /fd SHA256 /f "%CERT_PFX%" /p landauer /tr http://timestamp.digicert.com /td SHA256 "%DRV_PATH%"
if %ERRORLEVEL% NEQ 0 (echo ERROR: Signing failed. & pause & exit /b 1)

:: Export .cer for trust
powershell -ExecutionPolicy Bypass -Command "$p=ConvertTo-SecureString -String 'landauer' -Force -AsPlainText; $c=Import-PfxCertificate -FilePath '%CERT_PFX%' -Password $p -CertStoreLocation 'Cert:\CurrentUser\My'; Export-Certificate -Cert $c -FilePath '%~dp0landauer_test.cer' -Type CERT" >nul 2>&1

:: Trust the cert
certutil -addstore Root "%~dp0landauer_test.cer" >nul 2>&1
echo   Signed and trusted.

:: ======== STEP 4: INSTALL AND START ========
echo.
echo [4/5] Installing driver...
sc create %DRV_NAME% type=kernel binPath="%DRV_PATH%" start=demand error=normal
if %ERRORLEVEL% NEQ 0 (echo ERROR: sc create failed. & pause & exit /b 1)

echo [5/5] Starting driver...
sc start %DRV_NAME%
if %ERRORLEVEL% NEQ 0 (
    echo ERROR: sc start failed.
    echo.
    echo Check:
    echo   - Secure Boot is OFF in BIOS
    echo   - bcdedit /set testsigning on and REBOOT
    echo   - Run: sc query %DRV_NAME%
    pause
    exit /b 1
)

echo.
echo ============================================================
echo  INSTALL COMPLETE
echo ============================================================
echo.
echo Driver: OK
echo Test it:
echo   "%CLI_PATH%" driver status
echo   "%CLI_PATH%" pci list
echo.
echo CLI is at: %CLI_PATH%
echo (add this directory to PATH for convenience)
echo.
"%CLI_PATH%" driver status
pause
