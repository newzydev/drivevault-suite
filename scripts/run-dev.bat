@echo off
rem Shared helper for run-master-pro.bat / run-video-editor.bat: puts the suite's Qt on PATH and starts the exe of a Release build.
rem Usage: run-dev.bat <app folder under apps\> <exe name> [program arguments]
setlocal
set "ROOT=%~dp0.."
set "APP=%~1"
set "EXE=%~2"
shift
shift

rem Qt: DV_QT_DIR if set, otherwise the highest version in .tools\Qt
if not defined DV_QT_DIR (
    for /f "delims=" %%D in ('dir /b /ad /o-n "%ROOT%\.tools\Qt" 2^>nul') do (
        if not defined DV_QT_DIR set "DV_QT_DIR=%ROOT%\.tools\Qt\%%D\msvc2022_64"
    )
)
if not defined DV_QT_DIR (
    echo Qt not found. Set DV_QT_DIR or install .tools\Qt ^(see docs\DEVELOPMENT.md^).
    pause
    exit /b 1
)
set "PATH=%DV_QT_DIR%\bin;%PATH%"

set "BIN=%ROOT%\apps\%APP%\build\release\bin"
if not exist "%BIN%\%EXE%" (
    echo %EXE% not found in "%BIN%".
    echo Build it first: apps\%APP%\scripts\build.ps1
    pause
    exit /b 1
)

rem Collect the remaining arguments (shift does not change %*)
set "ARGS="
:collect
if "%~1"=="" goto run
set ARGS=%ARGS% %1
shift
goto collect

:run
start "" "%BIN%\%EXE%" %ARGS%
endlocal
