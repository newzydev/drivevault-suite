@echo off
rem Opens DriveVault PDF Pro from its build folder - nothing is installed, no registry entry is written.
rem Build it first: apps\drivevault-pdf-pro\scripts\build.ps1
call "%~dp0run-dev.bat" drivevault-pdf-pro DriveVaultPdfPro.exe %*
