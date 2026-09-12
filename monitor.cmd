@echo off
rem Clean native-Windows environment for PlatformIO (see build.cmd)
set PATH=C:\Windows\System32;C:\Windows;C:\Program Files\Git\cmd;%LOCALAPPDATA%\Programs\Python\Python313;%LOCALAPPDATA%\Programs\Python\Python313\Scripts
cd /d "%~dp0"
pio device monitor --baud 115200 --quiet
