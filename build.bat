@echo off
rem ogg-winmm.HookModule: generates the version resource, then hands off to nmake.
rem   build.bat [build | clean | cleandep | fetch], or with no arguments for a menu.
setlocal
cd /d "%~dp0"

set "GEN_RC=obj\ogg-winmm.gen.rc"
set "RC_IN=ogg-winmm.rc.in"
set "VER_FILE=resource\version.txt"
set "VSPF=%ProgramFiles(x86)%"
if not defined VSPF set "VSPF=%ProgramFiles%"
set "VSWHERE=%VSPF%\Microsoft Visual Studio\Installer\vswhere.exe"

rem Called by build.nmake; not a user subcommand.
if /i "%~1"=="--genrc" goto genrc
if /i "%~1"=="--fetchdep" goto fetchdep
if /i "%~1"=="--stamp" goto stamp

set "TARGET="
set "VERB="
if /i "%~1"=="build" set "TARGET=all"
if /i "%~1"=="clean" set "TARGET=clean"
if /i "%~1"=="cleandep" set "TARGET=cleandep"
if /i "%~1"=="fetch" set "TARGET=fetch"

if not defined TARGET (
    if "%~1"=="" goto menu
    echo Error: unknown subcommand '%~1'
    echo        Usage: build.bat [build ^| clean ^| cleandep ^| fetch]
    exit /b 1
)

rem clean/cleandep/fetch need no toolchain.
if /i "%TARGET%"=="clean" goto nmake
if /i "%TARGET%"=="cleandep" goto nmake
if /i "%TARGET%"=="fetch" goto nmake

if defined VCToolsInstallDir (
    echo   toolchain found in the environment
) else (
    echo   toolchain not in the environment - looking for a Visual Studio Developer Command Prompt
    if exist "%VSWHERE%" for /f "usebackq delims=" %%i in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do call "%%i\Common7\Tools\VsDevCmd.bat"
)
if not defined VCToolsInstallDir (
    echo Error: no valid Visual Studio toolchain found
    exit /b 1
)
rem build.nmake resolves the host toolset, checks the vendored sources are present,
rem and decides which arches can be built.

:nmake
nmake /NOLOGO /f build.nmake %TARGET%
exit /b %errorlevel%

:genrc
if not exist obj mkdir obj
powershell -NoProfile -ExecutionPolicy Bypass -Command "$t=([IO.File]::ReadAllText('%VER_FILE%')).Trim(); $r=$t.TrimStart('v'); if($r.Split('.').Count -lt 3){Write-Host 'Error: resource\version.txt must hold a version like v1.2.3'; exit 1}; $r=$r.Replace('.',','); $s=([IO.File]::ReadAllText('%RC_IN%')).Replace('__REV__',$r); $m='Version resource: '+$t+' -> '+$r; if((-not (Test-Path '%GEN_RC%')) -or ([IO.File]::ReadAllText('%GEN_RC%') -ne $s)){[IO.File]::WriteAllText('%GEN_RC%',$s,[Text.Encoding]::ASCII)} else {$m=$m+' (unchanged)'}; Write-Host $m"
exit /b %errorlevel%

:fetchdep
rem Called by build.nmake for each pinned dependency; not a user subcommand.
rem Transactional: download to a .part file, verify the hash, extract into a scratch
rem dir, then move the finished tree into place - deps\ never holds a half tree.
set "DEP=%~2"
set "VER=%~3"
set "SHA=%~4"
set "URL=%~5"
set "NAME=%DEP%-%VER%"
set "DEST=deps\%NAME%"
set "PART=deps\%NAME%.tar.gz.part"
set "SCRATCH=deps\_fetch"
if exist "%DEST%" (
    echo   %NAME% is already fetched
) else (
    echo === fetching %NAME% ===
    if exist "%SCRATCH%" rmdir /s /q "%SCRATCH%"
    if exist "%PART%" del /q "%PART%"
    curl -fsSL -o "%PART%" "%URL%"
    if errorlevel 1 (
        echo Error: could not download %URL%
        if exist "%PART%" del /q "%PART%"
        exit /b 1
    )
    powershell -NoProfile -ExecutionPolicy Bypass -Command "if((Get-FileHash -Algorithm SHA256 '%PART%').Hash -ne '%SHA%'){Write-Host 'Error: %NAME% does not match the pinned SHA-256'; exit 1}"
    if errorlevel 1 (
        if exist "%PART%" del /q "%PART%"
        exit /b 1
    )
    mkdir "%SCRATCH%"
    tar -xzf "%PART%" -C "%SCRATCH%"
    if errorlevel 1 (
        echo Error: could not extract %NAME%
        rmdir /s /q "%SCRATCH%"
        del /q "%PART%"
        exit /b 1
    )
    move "%SCRATCH%\%NAME%" "%DEST%" >nul
    if errorlevel 1 (
        echo Error: %NAME% was not found inside the tarball
        rmdir /s /q "%SCRATCH%"
        del /q "%PART%"
        exit /b 1
    )
    rmdir /s /q "%SCRATCH%"
    del /q "%PART%"
)
rem Report - never delete - a differently-versioned tree left beside the pinned one.
for /d %%d in ("deps\%DEP%-*") do if /i not "%%~nxd"=="%NAME%" echo   note: %%d is not the pinned version and is unused
exit /b 0

:stamp
rem Called by build.nmake; keys the cached dependency libraries to the toolset in use.
rem When the identity changes the stale libraries are dropped here, before nmake
rem decides what is out of date, so they get rebuilt in this same run.
set "STAMP=deps\toolset.stamp"
set "NOW=%~2 %~3 %~4"
set "OLD="
if exist "%STAMP%" <"%STAMP%" set /p "OLD="
if "%OLD%"=="%NOW%" exit /b 0
if not exist deps mkdir deps
>"%STAMP%" echo %NOW%
if not defined OLD exit /b 0
del /q deps\lib\*.lib 2>nul
echo   toolset changed - dropped the cached dependency libraries
exit /b 0

:menu
cls
echo   1. fetch dependencies
echo   2. build
echo   3. clean
echo   4. clean dependencies
echo   5. quit
set "VERB="
choice /C 12345 /N /M "Choice: "
if errorlevel 255 exit /b 1
if errorlevel 5 exit /b 0
if errorlevel 4 (set "VERB=cleandep" & goto chosen)
if errorlevel 3 (set "VERB=clean" & goto chosen)
if errorlevel 2 (set "VERB=build" & goto chosen)
if errorlevel 1 (set "VERB=fetch" & goto chosen)
if not defined VERB exit /b 1

:chosen
call "%~f0" %VERB%
if errorlevel 1 goto failed
goto menu

:failed
echo.
echo Press any key to continue...
pause >nul
goto menu
