param(
    [string]$FirmwareSource = '\\wsl.localhost\Ubuntu\home\paw\ios-work\negroni-firmware\gpu',
    [string]$Inf2CatPath = 'C:\Program Files (x86)\Windows Kits\10\bin\10.0.28000.0\x86\Inf2Cat.exe'
)

$ErrorActionPreference = 'Stop'
$root = (Resolve-Path $PSScriptRoot).Path
$outRoot = [System.IO.Path]::GetFullPath((Join-Path $root 'out'))
$packageDir = [System.IO.Path]::GetFullPath((Join-Path $outRoot 'package'))
$prefix = $outRoot.TrimEnd('\') + '\'
if (-not $packageDir.StartsWith($prefix, [System.StringComparison]::OrdinalIgnoreCase)) {
    throw "Package output escaped the NegroniGpu out directory: $packageDir"
}
if (-not (Test-Path -LiteralPath $FirmwareSource -PathType Container)) {
    throw "Firmware directory not found: $FirmwareSource"
}
if (-not (Test-Path -LiteralPath $Inf2CatPath -PathType Leaf)) {
    throw "Inf2Cat not found: $Inf2CatPath"
}
if (-not (Test-Path -LiteralPath (Join-Path $outRoot 'NegroniGpuWddm.sys') -PathType Leaf)) {
    throw 'Build the ARM64 WDDM prototype before staging the local package.'
}

$firmwareHashes = @{
    'gmu_gen70000.bin' = 'b2d3f0e2a98fd6259ab9ffef16df63265542e6cc9a970d53d476e823d432042a'
    'a730_sqe.fw'      = '4f8b288f340ff5b4881bed8dfea90bb4a1b967556ca2df6d07f4ed4e4a28d071'
    'a730_zap.mdt'     = '0706695b7875fecfb563f0d01cad8c12212e3f1af71ca82f6ff04708a37841fd'
    'a730_zap.b01'     = '1fc98f97cf354b9704b20d0848f7b2739638539a57d88ecf5cc71ca1c5c4e6b9'
}

New-Item -ItemType Directory -Path $packageDir -Force | Out-Null
Copy-Item -LiteralPath (Join-Path $outRoot 'NegroniGpuWddm.sys') -Destination $packageDir -Force
Copy-Item -LiteralPath (Join-Path $root 'NegroniGpuWddm.inf') -Destination $packageDir -Force
foreach ($name in $firmwareHashes.Keys) {
    $source = Join-Path $FirmwareSource $name
    if (-not (Test-Path -LiteralPath $source -PathType Leaf)) {
        throw "Required phone firmware is missing: $source"
    }
    $actual = (Get-FileHash -LiteralPath $source -Algorithm SHA256).Hash.ToLowerInvariant()
    if ($actual -ne $firmwareHashes[$name]) {
        throw "Firmware hash mismatch for $name (expected $($firmwareHashes[$name]), got $actual)."
    }
    Copy-Item -LiteralPath $source -Destination $packageDir -Force
}

& $Inf2CatPath "/driver:$packageDir" '/os:10_NI_ARM64' '/verbose'
if ($LASTEXITCODE -ne 0) {
    throw "Inf2Cat failed with exit code $LASTEXITCODE"
}
Get-ChildItem -LiteralPath $packageDir -File | Get-FileHash -Algorithm SHA256 |
    Select-Object @{Name='File';Expression={ Split-Path $_.Path -Leaf }}, Hash
Write-Host "Offline experimental package staged at: $packageDir"
Write-Warning 'DO NOT install: this package has no render DDIs or user-mode driver.'
