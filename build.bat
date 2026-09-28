@echo off
rem ogg-winmm.HookModule build entry point: generates the version resource from
rem resource\version.txt, then hands the build to nmake.
rem
rem   build.bat [build | clean | rebuild | fetch | deps | --genrc]
rem
rem The pinned toolchain below is used only if nmake is not already on PATH.
setlocal
cd /d "%~dp0"

set "GEN_RC=obj\ogg-winmm.gen.rc"
set "RC_IN=ogg-winmm.rc.in"
set "VER_FILE=resource\version.txt"

if /i "%~1"=="--genrc" goto genrc

rem Overridable so the script follows the same pin as build.nmake.
if not defined MSVC_TOOLSET set "MSVC_TOOLSET=C:\MSVC\VC\Tools\MSVC\14.40.33807"
where nmake >NUL 2>&1 || set "PATH=%MSVC_TOOLSET%\bin\Hostx64\x86;%PATH%"

rem Subcommands map onto nmake targets; anything else is a plain build.
set "TARGET=all"
if /i "%~1"=="build"   set "TARGET=all"
if /i "%~1"=="clean"   set "TARGET=clean"
if /i "%~1"=="rebuild" set "TARGET=rebuild"
if /i "%~1"=="deps"    set "TARGET=deps"
if /i "%~1"=="fetch"   set "TARGET=fetch"

rem The sources are worth naming plainly before nmake reports them missing.
if /i not "%TARGET%"=="fetch" if /i not "%TARGET%"=="clean" (
    if not exist "deps\libogg-1.3.5\src\framing.c" (
        echo Error: the ogg/vorbis sources are missing from deps\.
        echo        Run: build.bat fetch
        exit /b 1
    )
)

call :genrc
if errorlevel 1 exit /b 1

nmake /NOLOGO /f build.nmake %TARGET%
exit /b %errorlevel%

:genrc
if not exist obj mkdir obj
powershell -NoProfile -ExecutionPolicy Bypass -Command "$t=([IO.File]::ReadAllText('%VER_FILE%')).Trim(); $r=$t.TrimStart('v'); if($r.Split('.').Count -lt 3){Write-Host 'Error: resource\version.txt must hold a version like v2026.09.13'; exit 1}; $r=$r.Replace('.',','); $s=([IO.File]::ReadAllText('%RC_IN%')).Replace('__REV__',$r); if((-not (Test-Path '%GEN_RC%')) -or ([IO.File]::ReadAllText('%GEN_RC%') -ne $s)){[IO.File]::WriteAllText('%GEN_RC%',$s,[Text.Encoding]::ASCII); Write-Host ('Version resource: '+$t+' -> '+$r)} else {Write-Host ('Version resource: '+$t+' -> '+$r+' (unchanged)')}"
exit /b %errorlevel%
