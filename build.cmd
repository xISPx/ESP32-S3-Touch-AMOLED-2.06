@echo off
rem Clean native-Windows build environment for PlatformIO
rem (avoids MSYS/Git-Bash PATH mangling that hides the Xtensa toolchain)
set PATH=C:\Windows\System32;C:\Windows;C:\Program Files\Git\cmd;%LOCALAPPDATA%\Programs\Python\Python313;%LOCALAPPDATA%\Programs\Python\Python313\Scripts
cd /d "%~dp0"
pio run %*
