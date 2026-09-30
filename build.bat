@echo off
rem ogg-winmm.HookModule: generates the version resource, then hands off to nmake.
rem   build.bat [build | clean | fetch | deps], or with no arguments for a menu.
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

set "TARGET="
set "VERB="
if /i "%~1"=="build" set "TARGET=all"
if /i "%~1"=="clean" set "TARGET=clean"
if /i "%~1"=="deps"  set "TARGET=deps"
if /i "%~1"=="fetch" set "TARGET=fetch"

if not defined TARGET (
    if "%~1"=="" goto menu
    echo Error: unknown subcommand '%~1'
    echo        Usage: build.bat [build ^| clean ^| fetch ^| deps]
    exit /b 1
)

rem clean/fetch need no toolchain.
if /i "%TARGET%"=="clean" goto nmake
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
if not exist "deps\libogg-1.3.5\src\framing.c" (
    echo Error: the ogg/vorbis sources are missing from deps\
    echo        Run: build.bat fetch
    exit /b 1
)
rem build.nmake resolves the host toolset and decides which arches can be built.

if /i "%TARGET%"=="deps" goto nmake

:nmake
nmake /NOLOGO /f build.nmake %TARGET%
exit /b %errorlevel%

:genrc
if not exist obj mkdir obj
powershell -NoProfile -ExecutionPolicy Bypass -Command "$t=([IO.File]::ReadAllText('%VER_FILE%')).Trim(); $r=$t.TrimStart('v'); if($r.Split('.').Count -lt 3){Write-Host 'Error: resource\version.txt must hold a version like v1.2.3'; exit 1}; $r=$r.Replace('.',','); $s=([IO.File]::ReadAllText('%RC_IN%')).Replace('__REV__',$r); $m='Version resource: '+$t+' -> '+$r; if((-not (Test-Path '%GEN_RC%')) -or ([IO.File]::ReadAllText('%GEN_RC%') -ne $s)){[IO.File]::WriteAllText('%GEN_RC%',$s,[Text.Encoding]::ASCII)} else {$m=$m+' (unchanged)'}; Write-Host $m"
exit /b %errorlevel%

:menu
echo   1. build
echo   2. clean
echo   3. fetch
echo   4. quit
set "CHOICE="
set /p "CHOICE=Choice: "
if "%CHOICE%"=="1" set "VERB=build"
if "%CHOICE%"=="2" set "VERB=clean"
if "%CHOICE%"=="3" set "VERB=fetch"
if /i "%CHOICE%"=="4" exit /b 0
if not defined VERB goto menu
call "%~f0" %VERB%
set "RC=%errorlevel%"
if not "%VERB%"=="clean" goto close
choice /C yn /N /M "Build again? (y/n) "
if errorlevel 2 exit /b %RC%
call "%~f0" build
set "RC=%errorlevel%"

:close
echo.
echo Press any key to close...
pause >nul
exit /b %RC%
