@echo off
setlocal
title OnePlus touch and USB diagnostics
color 0F
echo Collecting OnePlus touch and USB diagnostics...
set "DBG=%SystemDrive%\negroni-pe-debug.txt"
call :collect > "%DBG%" 2>&1
cls
echo ONEPLUS TOUCH AND USB - diagnostic console
echo.
if exist "%SystemDrive%\NegroniTouch-trust.txt" type "%SystemDrive%\NegroniTouch-trust.txt"
type "%SystemDrive%\NegroniTouch-debug-load.txt"
echo.
echo ---- driver service ----
sc query NegroniTouch | findstr /i "STATE WIN32_EXIT"
echo ---- setupapi: NegroniTouch errors ----
findstr /i /c:"!!!" /c:"NegroniTouch" "%SystemRoot%\inf\setupapi.dev.log" | findstr /v /i "reg query"
echo ---- touch device status (ACPI\NEGT0001) ----
pnputil /enum-devices /instanceid "ACPI\NEGT0001\0" | findstr /i "Status Problem Driver"
echo Full report: %DBG%
for %%d in (C D E F G H I J K L M N O P Q R S T U V W Y Z) do if exist "%%d:\sources\boot.wim" (
  copy /y "%DBG%" "%%d:\negroni-pe-debug.txt" >nul 2>&1
  if not errorlevel 1 echo Report also saved to %%d:\negroni-pe-debug.txt
)
echo Photograph this window.
endlocal
exit /b

:collect
ver
echo ==== Package trust ====
if exist "%SystemRoot%\System32\NegroniTouch\TrustTouch.exe" "%SystemRoot%\System32\NegroniTouch\TrustTouch.exe" > "%SystemDrive%\NegroniTouch-trust.txt" 2>&1
if exist "%SystemDrive%\NegroniTouch-trust.txt" type "%SystemDrive%\NegroniTouch-trust.txt"
echo ==== Touch driver load ====
drvload "%SystemRoot%\System32\NegroniTouch\NegroniTouch.inf" > "%SystemDrive%\NegroniTouch-debug-load.txt" 2>&1
type "%SystemDrive%\NegroniTouch-debug-load.txt"
echo ==== Touch state ====
reg query HKLM\SYSTEM\CurrentControlSet\Services\NegroniTouch /s
reg query HKLM\SYSTEM\CurrentControlSet\Enum\ACPI\NEGT0001 /s
sc query vhf
echo ==== USB services ====
sc query USBXHCI
sc query UCX01000
sc query USBHUB3
echo ==== Device errors ====
pnputil /enum-devices /problem
echo ==== USB devices ====
pnputil /enum-devices /class USB
reg query HKLM\SYSTEM\CurrentControlSet\Enum\USB /s
echo ==== Touch state after enumeration ====
reg query HKLM\SYSTEM\CurrentControlSet\Services\NegroniTouch
echo ==== Driver installation log ====
type "%SystemRoot%\inf\setupapi.dev.log"
exit /b
