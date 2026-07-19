@echo off
:: ============================================================
:: build.bat  –  xplapi X-Plane 12 plugin
::
:: Usage:
::   build              -> Release|x64  (default, distributable)
::   build debug        -> Debug|x64    (local dev only, NOT for distribution)
::   build clean        -> clean Release|x64
::   build log          -> write output to build.log
::   build log deploy   -> combine options
:: ============================================================

set VSLANG=1033
set PreferredUILang=en-US

set MSBUILD="C:\Program Files\Microsoft Visual Studio\2022\Community\MSBuild\Current\Bin\MSBuild.exe"
set PROJECT=xplapi.vcxproj
set CONFIG=Release
set PLATFORM=x64
set DEPLOY_DIR=E:\XPL12\X-Plane 12\Resources\plugins\xplapi\64
set BUILD_LOG=build.log

:: Parse optional arguments  (order-independent)
set DEPLOY=0
set LOG=0
for %%A in (%*) do (
    if /I "%%A"=="debug"   set CONFIG=Debug
    if /I "%%A"=="release" set CONFIG=Release
    if /I "%%A"=="deploy"  set DEPLOY=1
    if /I "%%A"=="log"     set LOG=1
)
if /I "%1"=="clean" (
    echo Cleaning %CONFIG%^|%PLATFORM% ...
    if "%LOG%"=="1" (
        %MSBUILD% %PROJECT% /p:Configuration=%CONFIG% /p:Platform=%PLATFORM% /p:PreferredUILang=en-US /t:Clean /nologo > "%BUILD_LOG%" 2>&1
        type "%BUILD_LOG%"
    ) else (
        %MSBUILD% %PROJECT% /p:Configuration=%CONFIG% /p:Platform=%PLATFORM% /p:PreferredUILang=en-US /t:Clean /nologo
    )
    exit /b %ERRORLEVEL%
)

:: ---- Redirect to log file if requested ----
if "%LOG%"=="1" (
    echo Logging to %BUILD_LOG%
    call :do_build > "%BUILD_LOG%" 2>&1
    set BUILD_EXIT=%ERRORLEVEL%
    type "%BUILD_LOG%"
    exit /b %BUILD_EXIT%
) else (
    call :do_build
    exit /b %ERRORLEVEL%
)

:do_build
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
    
    echo Deploying static web files to %DEPLOY_DIR%\..\www ...
    xcopy /E /I /Y "www" "%DEPLOY_DIR%\..\www"

    if %ERRORLEVEL% == 0 (
        echo Deploy OK.
    ) else (
        echo Deploy FAILED.
    )
)

exit /b 0
