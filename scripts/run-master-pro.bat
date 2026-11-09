@echo off
rem Opens DriveVault Master Pro from its build folder - nothing is installed, no registry entry is written.
rem Build it first: apps\drivevault-master-pro\scripts\build.ps1
call "%~dp0run-dev.bat" drivevault-master-pro DriveVaultMasterPro.exe %*
