@echo off
rem Opens DriveVault Browser Pro from its build folder - nothing is installed, no registry entry is written.
rem Build it first: apps\drivevault-browser-pro\scripts\build.ps1
call "%~dp0run-dev.bat" drivevault-browser-pro DriveVaultBrowserPro.exe %*
