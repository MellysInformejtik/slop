# Windows Wi-Fi for negroni (QCA6490 on PCIe0, brought up by Mu's NegroniPcieDxe; ACPI PCI0/MCFG in the
# negroni tables): Qualcomm's qcwlan8350 from the WOA Lahaina package, with the phone's own board data file
# (bdwlan.elf from modem_a) in place of Lahaina's, a new catalog, test-signed (the phone runs in test mode).
# Vendor files stay out of git: they come from \\wsl.localhost\Ubuntu\home\paw\ios-work\...
#   .\make-package.ps1     -> out\package
$ErrorActionPreference = 'Stop'
$here = Split-Path -Parent $MyInvocation.MyCommand.Path
$src = '\\wsl.localhost\Ubuntu\home\paw\ios-work\windows-drivers\lahaina-wlan'
$board = '\\wsl.localhost\Ubuntu\home\paw\ios-work\negroni-firmware\modem\img\image\bdwlan.elf'
$dest = Join-Path $here 'out\package'
$kits = 'C:\Program Files (x86)\Windows Kits\10\bin\10.0.28000.0'
if (Test-Path $dest) { Remove-Item -Recurse -Force $dest }
New-Item -ItemType Directory -Force $dest | Out-Null
foreach ($f in 'qcwlan8350.inf', 'qcwlan8350.sys', 'wlanfw.mbn', 'wlanfw20.mbn', 'm3.bin', 'regdb.bin', 'Data.msc', 'Data20.msc') {
    Copy-Item (Join-Path $src $f) $dest
}
Copy-Item $board (Join-Path $dest 'bdwlan.elf')
& "$kits\x86\Inf2Cat.exe" "/driver:$dest" /os:10_NI_ARM64 /uselocaltime
if ($LASTEXITCODE) { throw 'Catalog generation failed' }
$cert = Get-ChildItem Cert:\CurrentUser\My | Where-Object { $_.Subject -eq 'CN=ios-sweet negroni test' -and $_.HasPrivateKey -and $_.NotAfter -gt (Get-Date) } | Select-Object -First 1
if (-not $cert) { throw 'Test certificate CN=ios-sweet negroni test not found' }
& "$kits\x64\signtool.exe" sign /fd SHA256 /sha1 $cert.Thumbprint /s My (Join-Path $dest 'qcwlan8350.cat')
if ($LASTEXITCODE) { throw 'Catalog signing failed' }
Get-ChildItem $dest | Select-Object Name, Length
Write-Host "Wi-Fi package: $dest"
