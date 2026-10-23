# On the PC, the phone in Mu mass storage: put the diagnostics and the GPU test package into the phone's Windows.
#   Administrator PowerShell:  .\install-diag.ps1 -Windows D:
# Desktop (all users): "Negroni diagnostics", "Install GPU driver", "Turn GPU driver off"; Startup: the report at
# every logon; C:\NegroniDiag (the script), C:\NegroniGpu (the signed package from ..\NegroniGpu\sign-package.ps1).
param([Parameter(Mandatory)][string]$Windows)
$ErrorActionPreference = 'Stop'
$here = Split-Path -Parent $MyInvocation.MyCommand.Path
$Windows = $Windows.TrimEnd('\')
if (-not (Test-Path "$Windows\Windows\System32\config\SYSTEM")) { throw "$Windows is not a Windows partition" }
$package = Join-Path $here '..\NegroniGpu\out\package'
if (-not (Test-Path (Join-Path $package 'NegroniGpuWddm.cat'))) { throw "no signed package at $package (run NegroniGpu\sign-package.ps1)" }

$desktop = "$Windows\Users\Public\Desktop"
$startup = "$Windows\ProgramData\Microsoft\Windows\Start Menu\Programs\StartUp"
foreach ($d in "$Windows\NegroniDiag", "$Windows\NegroniGpu", $desktop, $startup) { New-Item -ItemType Directory -Force $d | Out-Null }
Copy-Item (Join-Path $here 'negroni-diag.ps1') "$Windows\NegroniDiag\" -Force
Copy-Item (Join-Path $package '*') "$Windows\NegroniGpu\" -Force
foreach ($f in 'Negroni diagnostics.cmd', 'Install GPU driver.cmd', 'Turn GPU driver off.cmd') { Copy-Item (Join-Path $here $f) $desktop -Force }
Copy-Item (Join-Path $here 'Negroni diagnostics at logon.cmd') $startup -Force
Get-ChildItem "$Windows\NegroniGpu", $desktop | Select-Object FullName, Length
Write-Host 'done: eject the phone and boot Windows'
