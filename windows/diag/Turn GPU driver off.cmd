@echo off
rem recovery: GPU driver service Start=4 and the device disabled, then the report
powershell -NoProfile -Command "Start-Process powershell -Verb RunAs -ArgumentList '-NoProfile -ExecutionPolicy Bypass -File C:\NegroniDiag\negroni-diag.ps1 -Off'"
