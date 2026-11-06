@echo off
setlocal EnableExtensions
rem PlantCare R7.2 for Windows: pick a number to build, upload and open the serial monitor.
rem Ctrl+C leaves the serial monitor. If Windows then asks to terminate the batch job, press N to come back here.
cd /d "%~dp0"

set "PIO="
where pio >nul 2>nul && set "PIO=pio"
if not defined PIO if exist "%USERPROFILE%\.platformio\penv\Scripts\pio.exe" set "PIO=%USERPROFILE%\.platformio\penv\Scripts\pio.exe"
if not defined PIO (
  echo PlatformIO was not found. Install VS Code and the PlatformIO IDE extension,
  echo open this folder in VS Code once, wait until PlatformIO finishes, then run this file again.
  pause
  exit /b 1
)

:menu
echo.
echo ================ PlantCare R7.2 ================
echo    1  t1_i2c_scan   OLED found at 0x3C?
echo    2  t2_oled       faces, device page, explanation page
echo    3  t3_probe      probe readings / 3-level calibration
echo    4  t4_pir        PIR approach trigger
echo    5  t5_button     button count
echo    6  t6_ring       ring and board LED colours
echo    7  t7_dfplayer   three voice clips
echo    8  t8_power      10 min power stress: ring + voice + Wi-Fi + AI
echo    9  t9_selftest   all modules together
echo   10  t10_ai        Wi-Fi + Gemini explanation
echo   11  app           full program, with TF card
echo   12  app_noTF      full program, no TF card
echo    c  build all 12 programs (no board needed)
echo    m  serial monitor only
echo    e  erase the whole flash (only if 11 keeps rebooting; Wi-Fi, key and settings must be entered again)
echo    q  quit
set "n="
set /p "n=Choose: "
if /i "%n%"=="q" exit /b 0
if /i "%n%"=="c" goto buildall
if /i "%n%"=="m" goto monitor
if /i "%n%"=="e" goto erase
set "E="
if "%n%"=="1" set "E=t1_i2c_scan"
if "%n%"=="2" set "E=t2_oled"
if "%n%"=="3" set "E=t3_probe"
if "%n%"=="4" set "E=t4_pir"
if "%n%"=="5" set "E=t5_button"
if "%n%"=="6" set "E=t6_ring"
if "%n%"=="7" set "E=t7_dfplayer"
if "%n%"=="8" set "E=t8_power"
if "%n%"=="9" set "E=t9_selftest"
if "%n%"=="10" set "E=t10_ai"
if "%n%"=="11" set "E=app"
if "%n%"=="12" set "E=app_noTF"
if not defined E (
  echo Invalid choice.
  goto menu
)
echo ---- %E%: build and upload. Stuck at "Connecting..."? Hold BOOT on the board and tap RST. ----
"%PIO%" run -e %E% -t upload
if errorlevel 1 (
  echo.
  echo UPLOAD FAILED. Check: data USB cable, COM port in Device Manager, no other serial monitor open.
  goto menu
)
echo ---- Serial monitor is open in THIS window: type commands here. Ctrl+C to stop, then press N. ----
echo ---- While it runs, VS Code or any other program cannot open the same COM port. ----
"%PIO%" device monitor -e %E%
goto menu

:monitor
echo ---- Serial monitor is open in THIS window. Ctrl+C to stop, then press N. ----
"%PIO%" device monitor -e app
goto menu

:erase
set "yn="
set /p "yn=Erase ALL flash on the board, including settings and logs? Type YES: "
if not "%yn%"=="YES" goto menu
"%PIO%" run -e app -t erase
echo Done. Now choose 11 to flash the app again, then enter wifi / wifi2 / apikey again.
goto menu

:buildall
"%PIO%" run -e t1_i2c_scan -e t2_oled -e t3_probe -e t4_pir -e t5_button -e t6_ring -e t7_dfplayer -e t8_power -e t9_selftest -e t10_ai -e app -e app_noTF
if errorlevel 1 (
  echo.
  echo BUILD FAILED. Copy all the error text above and send it back.
) else (
  echo.
  echo All 12 programs built OK.
)
goto menu
