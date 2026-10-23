@echo off
rem installs C:\NegroniGpu\NegroniGpuWddm.inf, then the report
powershell -NoProfile -Command "Start-Process powershell -Verb RunAs -ArgumentList '-NoProfile -ExecutionPolicy Bypass -File C:\NegroniDiag\negroni-diag.ps1 -Install'"
