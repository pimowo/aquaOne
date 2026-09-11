@echo off
setlocal
cd /d "C:\Users\piotrek\Documents\PlatformIO\Projects\GasSense"
"C:\Users\piotrek\.platformio\penv\Scripts\platformio.exe" run --environment gassense > build_full.log 2>&1
if exist ".pio\build\gassense\firmware.elf" (
    echo SUCCESS: firmware.elf created
    type build_full.log | findstr /I "Linking"
    type build_full.log | findstr /I "=========="
) else (
    echo FAILED: firmware.elf not found
    type build_full.log | findstr /I "error"
    type build_full.log | findstr /I "undefined"
)
