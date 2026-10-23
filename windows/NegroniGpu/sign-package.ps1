# Stage the experimental A730 package (package-local.ps1) and test-sign its catalog with the same certificate as the
# touch and key drivers (CN=ios-sweet negroni test). Output: out\package with the .sys, INF, firmware, signed .cat, .cer.
# For a test-signing Windows on the phone only: the KMD has no render path and no paired user-mode driver.
$ErrorActionPreference = 'Stop'
& (Join-Path $PSScriptRoot 'package-local.ps1')
$dest = Join-Path $PSScriptRoot 'out\package'
$kits = 'C:\Program Files (x86)\Windows Kits\10\bin\10.0.28000.0'
$cert = Get-ChildItem Cert:\CurrentUser\My | Where-Object { $_.Subject -eq 'CN=ios-sweet negroni test' -and $_.HasPrivateKey -and $_.NotAfter -gt (Get-Date) } | Select-Object -First 1
if (-not $cert) { throw 'Test certificate CN=ios-sweet negroni test not found (windows\NegroniTouch\sign-v4-catalog.ps1 makes it)' }
& "$kits\x64\signtool.exe" sign /fd SHA256 /sha1 $cert.Thumbprint /s My (Join-Path $dest 'NegroniGpuWddm.cat')
if ($LASTEXITCODE) { throw 'Catalog signing failed' }
Export-Certificate -Cert $cert -FilePath (Join-Path $dest 'NegroniGpuWddm.cer') | Out-Null
Get-ChildItem $dest | Select-Object Name, Length
Write-Host "Signed test package: $dest"
