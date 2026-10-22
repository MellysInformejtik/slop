# Run build-trust.sh first. Keeps the v4 SYS unchanged; signs its INF catalog.
$ErrorActionPreference = 'Stop'
$dest = Join-Path $PSScriptRoot 'out\v4-catalog'
New-Item -ItemType Directory -Force $dest | Out-Null
Copy-Item (Join-Path $PSScriptRoot 'out\v4\NegroniTouch.inf'),(Join-Path $PSScriptRoot 'out\v4\NegroniTouch.sys') $dest -Force
$kits = 'C:\Program Files (x86)\Windows Kits\10\bin\10.0.28000.0'
& "$kits\x86\Inf2Cat.exe" "/driver:$dest" /os:10_CO_ARM64 /uselocaltime
if ($LASTEXITCODE) { throw 'Catalog generation failed' }
$cert = Get-ChildItem Cert:\CurrentUser\My | Where-Object { $_.Subject -eq 'CN=ios-sweet negroni test' -and $_.HasPrivateKey -and $_.NotAfter -gt (Get-Date) } | Select-Object -First 1
if (-not $cert) {
    $cert = New-SelfSignedCertificate -Type CodeSigningCert -Subject 'CN=ios-sweet negroni test' -CertStoreLocation Cert:\CurrentUser\My -NotAfter (Get-Date).AddYears(5)
}
& "$kits\x64\signtool.exe" sign /fd SHA256 /sha1 $cert.Thumbprint /s My (Join-Path $dest 'negronitouch.cat')
if ($LASTEXITCODE) { throw 'Catalog signing failed' }
Export-Certificate -Cert $cert -FilePath (Join-Path $dest 'NegroniTouch.cer') | Out-Null
Write-Host "Signed package: $dest (test-signing PE only)"
