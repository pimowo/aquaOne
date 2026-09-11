@echo off
cd /d "C:\Users\piotrek\Documents\PlatformIO\Projects\GasSense"
REM Czyszczenie poprzedniego buildu
if exist ".pio\build\gassense\firmware.elf" del ".pio\build\gassense\firmware.elf"
REM Buildowanie
"C:\Users\piotrek\.platformio\penv\Scripts\platformio.exe" run --environment gassense
REM Sprawdzenie wyniku
if exist ".pio\build\gassense\firmware.elf" (
    echo.
    echo ========== BUILD SUCCESS ==========
    echo firmware.elf size:
    for /f "usebackq" %%A in ('.pio\build\gassense\firmware.elf') do echo %%~zA bytes
    dir /s ".pio\build\gassense\firmware.elf"
) else (
    echo.
    echo ========== BUILD FAILED ==========
    echo firmware.elf not created
)
pause
