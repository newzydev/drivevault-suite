@echo off
rem Opens DriveVault Video Editor Pro from its build folder - nothing is installed, no registry entry is written.
rem Build it first: apps\drivevault-video-editor-pro\scripts\build.ps1
call "%~dp0run-dev.bat" drivevault-video-editor-pro DriveVaultVideoEditorPro.exe %*
