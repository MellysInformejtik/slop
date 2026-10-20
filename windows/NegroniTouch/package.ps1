# negroni WinPE: test-sign NegroniTouch.sys, enable test signing in the WinPE BCD, inject the driver into boot.wim.
# Run in an ADMINISTRATOR PowerShell:  powershell -ExecutionPolicy Bypass -File C:\Users\paw\negroni-winpe\package.ps1
$ErrorActionPreference = 'Stop'
$here     = 'C:\Users\paw\negroni-winpe'
$signtool = 'C:\Program Files (x86)\Windows Kits\10\bin\10.0.28000.0\x64\signtool.exe'
Set-Location $here

# 1. Test certificate (CurrentUser\My), reused on later runs
$cert = Get-ChildItem Cert:\CurrentUser\My | Where-Object { $_.Subject -eq 'CN=ios-sweet negroni test' } | Select-Object -First 1
if (-not $cert) {
    $cert = New-SelfSignedCertificate -Type CodeSigningCert -Subject 'CN=ios-sweet negroni test' `
            -CertStoreLocation Cert:\CurrentUser\My -NotAfter (Get-Date).AddYears(5)
}
Write-Host "cert $($cert.Thumbprint)"

# 2. Sign the driver (embedded signature; WinPE runs with testsigning on, see step 4)
& $signtool sign /fd SHA256 /sha1 $cert.Thumbprint /s My "$here\driver\NegroniTouch.sys"
if ($LASTEXITCODE) { throw 'signtool failed' }

# 3. Inject into the Setup image (index 2 boots from the ISO media) and the WinPE image (index 1)
dism /Get-WimInfo /WimFile:"$here\boot.wim"
foreach ($index in 1, 2) {
    dism /Mount-Wim /WimFile:"$here\boot.wim" /Index:$index /MountDir:"$here\mount"
    if ($LASTEXITCODE) { throw "mount $index failed" }
    dism /Image:"$here\mount" /Add-Driver /Driver:"$here\driver\NegroniTouch.inf" /ForceUnsigned
    $added = $LASTEXITCODE
    dism /Unmount-Wim /MountDir:"$here\mount" /Commit
    if ($added) { throw "add-driver to index $index failed" }
}

# 4. Test signing in the media's BCD (the ISO BCD's default entry is the boot.wim ramdisk entry)
bcdedit /store "$here\bcd" /set '{default}' testsigning on
bcdedit /store "$here\bcd" /set '{default}' nointegritychecks on
bcdedit /store "$here\bcd" /enum '{default}'

Write-Host 'DONE: boot.wim and bcd are ready'
