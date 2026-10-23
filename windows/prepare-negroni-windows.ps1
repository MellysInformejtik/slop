# Prepare the Windows installed on the phone, offline, while Mu exposes its storage over USB (mass storage):
#   - skip the whole first-run setup: local account "paw" (no password), auto logon (windows\unattend-negroni.xml)
#   - BypassNRO in case a network page still shows
#   - test signing on, in the BCD of the phone's EFI partition
#   - the OnePlus touch driver (NegroniTouch) and key driver (NegroniKeys: power, volume), test-signed catalogs
#
# Administrator PowerShell:
#   .\prepare-negroni-windows.ps1 -Windows X: -Efi Y: [-Cpus 4]
# -Windows: the phone's Windows partition (has \Windows\System32), -Efi: its EFI partition (has \EFI\Microsoft\Boot\BCD)
param(
    [Parameter(Mandatory)][string]$Windows,
    [Parameter(Mandatory)][string]$Efi,
    [string]$Driver = '',
    [string]$KeysDriver = '',
    [int]$Cpus = 8      # 8: all cores; 4: only the four small cores (older builds blue-screen with 0x3E on all 8)
)
$ErrorActionPreference = 'Stop'
# (Windows PowerShell 5.1 has no $PSScriptRoot while parameter defaults are evaluated)
$here = Split-Path -Parent $MyInvocation.MyCommand.Path
if (-not $Driver) { $Driver = Join-Path $here 'NegroniTouch\out\v4-catalog' }
if (-not $KeysDriver) { $KeysDriver = Join-Path $here 'NegroniKeys\out\pkg' }
$Windows = $Windows.TrimEnd('\'); $Efi = $Efi.TrimEnd('\')
if (-not (Test-Path "$Windows\Windows\System32\config\SOFTWARE")) { throw "$Windows is not a Windows partition" }
$bcd = "$Efi\EFI\Microsoft\Boot\BCD"
if (-not (Test-Path $bcd)) { throw "no BCD at $bcd" }

Write-Host '== answer file (skips setup)'
New-Item -ItemType Directory -Force "$Windows\Windows\Panther" | Out-Null
Copy-Item (Join-Path $here 'unattend-negroni.xml') "$Windows\Windows\Panther\unattend.xml" -Force
# Windows also looks here on first boot (implicit answer file search order)
Copy-Item (Join-Path $here 'unattend-negroni.xml') "$Windows\Windows\System32\Sysprep\unattend.xml" -Force

Write-Host '== BypassNRO'
reg load HKLM\NEGRONI_SW "$Windows\Windows\System32\config\SOFTWARE" | Out-Null
try {
    reg add HKLM\NEGRONI_SW\Microsoft\Windows\CurrentVersion\OOBE /v BypassNRO /t REG_DWORD /d 1 /f | Out-Null
} finally {
    [gc]::Collect(); Start-Sleep 1
    reg unload HKLM\NEGRONI_SW | Out-Null
}

Write-Host '== test signing'
$entries = bcdedit /store $bcd /enum osloader
bcdedit /store $bcd /set '{default}' testsigning on
bcdedit /store $bcd /set '{default}' nointegritychecks on
if ($Cpus -ge 8) {
    bcdedit /store $bcd /deletevalue '{default}' numproc 2>$null | Out-Null
} else {
    bcdedit /store $bcd /set '{default}' numproc $Cpus
}
bcdedit /store $bcd /enum '{default}' | Select-String 'testsigning|nointegritychecks|numproc|device|path'

Write-Host '== touch driver'
Get-ChildItem $Driver -Filter *.cer | ForEach-Object {
    reg load HKLM\NEGRONI_SW "$Windows\Windows\System32\config\SOFTWARE" | Out-Null
    try {
        $cert = New-Object System.Security.Cryptography.X509Certificates.X509Certificate2 $_.FullName
        foreach ($store in 'Root', 'TrustedPublisher') {
            $key = "HKLM\NEGRONI_SW\Microsoft\SystemCertificates\$store\Certificates\$($cert.Thumbprint)"
            reg add $key /f | Out-Null
            $blob = [System.BitConverter]::ToString($cert.RawData).Replace('-', '')
            reg add $key /v Blob /t REG_BINARY /d $blob /f | Out-Null
        }
    } finally {
        [gc]::Collect(); Start-Sleep 1
        reg unload HKLM\NEGRONI_SW | Out-Null
    }
}
foreach ($inf in "$Driver\NegroniTouch.inf", "$KeysDriver\NegroniKeys.inf") {
    dism /Image:"$Windows\" /Add-Driver /Driver:"$inf" /ForceUnsigned
    if ($LASTEXITCODE -ne 0) { Write-Warning "DISM could not add $inf ($LASTEXITCODE): install it on the phone with pnputil instead" }
}
dism /Image:"$Windows\" /Get-Drivers | Select-String -Context 0,6 'negronitouch|negronikeys'

Write-Host 'done: eject the phone and boot Windows'
