@echo off
:: ============================================================
:: build.bat  –  xplapi X-Plane 12 plugin
::
:: Usage:
::   build           -> Debug|x64
::   build release   -> Release|x64
::   build clean     -> clean Debug|x64
:: ============================================================

set VSLANG=1033
set PreferredUILang=en-US

set MSBUILD="C:\Program Files\Microsoft Visual Studio\2022\Community\MSBuild\Current\Bin\MSBuild.exe"
set PROJECT=xplapi.vcxproj
set CONFIG=Debug
set PLATFORM=x64
set DEPLOY_DIR=E:\XPL12\X-Plane 12\Resources\plugins\xplapi\64

:: Parse optional arguments  (order-independent: any arg can be 'deploy')
set DEPLOY=0
for %%A in (%*) do (
    if /I "%%A"=="release" set CONFIG=Release
    if /I "%%A"=="deploy"  set DEPLOY=1
)
if /I "%1"=="clean" (
    echo Cleaning %CONFIG%^|%PLATFORM% ...
    %MSBUILD% %PROJECT% /p:Configuration=%CONFIG% /p:Platform=%PLATFORM% /p:PreferredUILang=en-US /t:Clean /nologo
    exit /b %ERRORLEVEL%
)

echo.
echo ============================================================
echo  xplapi Build
echo  Config  : %CONFIG%^|%PLATFORM%
echo ============================================================
echo.

%MSBUILD% %PROJECT% /p:Configuration=%CONFIG% /p:Platform=%PLATFORM% /p:PreferredUILang=en-US /t:Build /m /nologo

if %ERRORLEVEL% NEQ 0 (
    echo.
    echo ============================================================
    echo  BUILD FAILED  (exit code %ERRORLEVEL%^)
    echo ============================================================
    exit /b %ERRORLEVEL%
)

echo.
echo ============================================================
echo  BUILD SUCCEEDED
echo  Output : build\%CONFIG%\win.xpl
echo ============================================================

:: ---- Optional: deploy directly to X-Plane plugins folder ----
if "%DEPLOY%"=="1" (
    echo.
    echo Deploying to %DEPLOY_DIR% ...
    if not exist "%DEPLOY_DIR%" mkdir "%DEPLOY_DIR%"
    copy /Y "build\%CONFIG%\win.xpl" "%DEPLOY_DIR%\win.xpl"
    if %ERRORLEVEL% == 0 (
        echo Deploy OK.
    ) else (
        echo Deploy FAILED.
    )
)

exit /b 0
