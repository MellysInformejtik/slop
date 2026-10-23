@echo off
rem at every logon: the report without a prompt (some sections need admin; tap Negroni diagnostics for all)
powershell -NoProfile -WindowStyle Hidden -ExecutionPolicy Bypass -File C:\NegroniDiag\negroni-diag.ps1
