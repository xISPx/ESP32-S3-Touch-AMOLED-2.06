@echo off
rem Dumps flash chip id/size via esptool from the PlatformIO packages dir
set PATH=C:\Windows\System32;C:\Windows;C:\Program Files\Git\cmd;%LOCALAPPDATA%\Programs\Python\Python313;%LOCALAPPDATA%\Programs\Python\Python313\Scripts
cd /d "%~dp0"
python -c "import serial,sys; sys.path.insert(0,r'%USERPROFILE%\.platformio\packages\tool-esptoolpy'); import esptool; esptool.main(['--chip','esp32s3','--port','COM3','flash_id'])"
