@echo off
rem report: GPU, touch, keys, driver errors, crashes (asks for admin once)
powershell -NoProfile -Command "Start-Process powershell -Verb RunAs -ArgumentList '-NoProfile -ExecutionPolicy Bypass -File C:\NegroniDiag\negroni-diag.ps1 '"
