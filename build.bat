@echo off
rem ogg-winmm.HookModule: generates the version resource, then hands off to nmake.
rem   build.bat [build | clean | rebuild | fetch | deps]
rem Needs a registered MSVC toolchain in the environment.
setlocal
cd /d "%~dp0"

set "GEN_RC=obj\ogg-winmm.gen.rc"
set "RC_IN=ogg-winmm.rc.in"
set "VER_FILE=resource\version.txt"

rem Called by build.nmake; not a user subcommand.
if /i "%~1"=="--genrc" goto genrc

set "TARGET=all"
if /i "%~1"=="build"   set "TARGET=all"
if /i "%~1"=="clean"   set "TARGET=clean"
if /i "%~1"=="rebuild" set "TARGET=rebuild"
if /i "%~1"=="deps"    set "TARGET=deps"
if /i "%~1"=="fetch"   set "TARGET=fetch"

rem clean/fetch need no toolchain.
if /i "%TARGET%"=="clean" goto ready
if /i "%TARGET%"=="fetch" goto ready

if not defined VCToolsInstallDir (
    echo Error: no MSVC toolchain in the environment.
    echo        Open a Developer Command Prompt, or run your toolchain's register step.
    exit /b 1
)
where nmake >NUL 2>&1
if errorlevel 1 (
    echo Error: nmake is not on PATH. Run this from a Developer Command Prompt.
    exit /b 1
)
if not exist "deps\libogg-1.3.5\src\framing.c" (
    echo Error: the ogg/vorbis sources are missing from deps\.
    echo        Run: build.bat fetch
    exit /b 1
)

rem Target is always x86; use whichever x86-targeting host toolset is installed.
set "HOSTARCH="
for %%h in (x64 x86) do if not defined HOSTARCH if exist "%VCToolsInstallDir%bin\Host%%h\x86\cl.exe" set "HOSTARCH=%%h"
if not defined HOSTARCH (
    echo Error: no x86-targeting toolset under "%VCToolsInstallDir%bin\"
    echo        Expected Hostx64\x86 or Hostx86\x86 - install the x86 target.
    exit /b 1
)
set "HOSTARG=HOSTARCH=%HOSTARCH%"

:ready
call :genrc
if errorlevel 1 exit /b 1

nmake /NOLOGO /f build.nmake %HOSTARG% %TARGET%
exit /b %errorlevel%

:genrc
if not exist obj mkdir obj
powershell -NoProfile -ExecutionPolicy Bypass -Command "$t=([IO.File]::ReadAllText('%VER_FILE%')).Trim(); $r=$t.TrimStart('v'); if($r.Split('.').Count -lt 3){Write-Host 'Error: resource\version.txt must hold a version like v2026.09.13'; exit 1}; $r=$r.Replace('.',','); $s=([IO.File]::ReadAllText('%RC_IN%')).Replace('__REV__',$r); if((-not (Test-Path '%GEN_RC%')) -or ([IO.File]::ReadAllText('%GEN_RC%') -ne $s)){[IO.File]::WriteAllText('%GEN_RC%',$s,[Text.Encoding]::ASCII); Write-Host ('Version resource: '+$t+' -> '+$r)} else {Write-Host ('Version resource: '+$t+' -> '+$r+' (unchanged)')}"
exit /b %errorlevel%
