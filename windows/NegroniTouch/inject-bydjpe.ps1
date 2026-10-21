# Add the OnePlus 10 Pro (negroni / SM8450) touch driver to BYDJPE ARM64.
# BYDJPE's ARM64 WIM is edited with wimlib; its own ARM64 drvload.exe loads
# the driver at startup. The existing Explorer/taskbar shell is preserved.
# Run from an elevated PowerShell window. This does not flash a device.
[CmdletBinding()]
param(
    [string]$MediaRoot = 'C:\Users\paw\Downloads\BYDJPE-V1.1',
    [string]$DriverRoot = 'C:\Users\paw\negroni-winpe\driver',
    [string]$ToolRoot,
    [string]$ExpectedSysSha256 = 'EADF2E050290E4CBD8911E310FB90E141FB8EBD885BD056C62CA16A7EFAE05F0'
)

$ErrorActionPreference = 'Stop'
if (-not $ToolRoot) { $ToolRoot = Join-Path $PSScriptRoot 'out\wimlib-1.14.5' }
$logDirectory = Join-Path $PSScriptRoot 'out'
New-Item -ItemType Directory -Force -Path $logDirectory | Out-Null
$transcript = Join-Path $logDirectory ('bydjpe-touch-' + (Get-Date -Format 'yyyyMMdd-HHmmss') + '.log')
Start-Transcript -LiteralPath $transcript | Out-Null
$identity = [Security.Principal.WindowsIdentity]::GetCurrent()
$principal = [Security.Principal.WindowsPrincipal]::new($identity)
if (-not $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) {
    throw 'Run this script from an Administrator PowerShell window.'
}

$wim = Join-Path $MediaRoot 'sources\boot.wim'
$bcd = Join-Path $MediaRoot 'efi\microsoft\boot\bcd'
$driverInf = Join-Path $DriverRoot 'NegroniTouch.inf'
$driverSys = Join-Path $DriverRoot 'NegroniTouch.sys'
$wimlib = Join-Path $ToolRoot 'wimlib-imagex.exe'
foreach ($path in @($wim, $bcd, $driverInf, $driverSys, $wimlib)) {
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) { throw "Missing input: $path" }
}

$actualHash = (Get-FileHash -LiteralPath $driverSys -Algorithm SHA256).Hash
if ($actualHash -ne $ExpectedSysSha256) {
    throw "Driver hash mismatch. Expected $ExpectedSysSha256, found $actualHash"
}

$info = & $wimlib info $wim 2>&1 | Out-String
if ($LASTEXITCODE -ne 0 -or $info -notmatch 'Architecture:\s+ARM64' -or $info -notmatch 'Image Count:\s+1') {
    throw 'Expected a single-image ARM64 BYDJPE boot.wim; refusing to edit this image.'
}

$stamp = Get-Date -Format 'yyyyMMdd-HHmmss'
$driverDestination = Join-Path $MediaRoot 'drivers\NegroniTouch'
$wimBackup = Join-Path (Split-Path $wim) "boot-before-negroni-touch-$stamp.wim"
$bcdBackup = "$bcd-before-negroni-touch-$stamp"
$work = Join-Path $logDirectory "bydjpe-stage-$stamp"
New-Item -ItemType Directory -Force -Path $driverDestination, $work | Out-Null
Copy-Item -LiteralPath $wim -Destination $wimBackup
Copy-Item -LiteralPath $bcd -Destination $bcdBackup
Copy-Item -LiteralPath $driverInf, $driverSys -Destination $driverDestination -Force
Write-Host "Backups: $wimBackup and $bcdBackup"

$startnetDir = Join-Path $work 'startnet'
New-Item -ItemType Directory -Force -Path $startnetDir | Out-Null
& $wimlib extract $wim 1 '\Windows\System32\startnet.cmd' "--dest-dir=$startnetDir"
if ($LASTEXITCODE -ne 0) { throw "Could not extract BYDJPE startnet.cmd ($LASTEXITCODE)" }
$startnet = Join-Path $startnetDir 'startnet.cmd'
if (-not (Test-Path -LiteralPath $startnet -PathType Leaf)) { throw 'BYDJPE startnet.cmd was not found.' }
$startnetText = [IO.File]::ReadAllText($startnet)
$loadCommand = 'drvload %SystemRoot%\System32\NegroniTouch\NegroniTouch.inf > %SystemDrive%\NegroniTouch-drvload.log 2>&1'
if ($startnetText -notmatch '(?i)NegroniTouch\.inf') {
    $startnetText = [regex]::Replace($startnetText, '(?im)^@echo off\s*', "`@echo off`r`n$loadCommand`r`n", 1)
}
if ($startnetText -notmatch '(?i)NegroniTouch\.inf') {
    throw 'Could not insert the runtime driver load into startnet.cmd.'
}
if ($startnetText -notmatch '(?im)^start\s+%SystemRoot%\\explorer\.exe\s*$') {
    if ($startnetText -match '(?im)^exit\s*$') {
        $startnetText = [regex]::Replace($startnetText, '(?im)^exit\s*$', "start %SystemRoot%\explorer.exe`r`nexit", 1)
    } else {
        $startnetText = $startnetText.TrimEnd() + "`r`nstart %SystemRoot%\explorer.exe`r`n"
    }
}
$startnet = Join-Path $work 'startnet.cmd'
[IO.File]::WriteAllText($startnet, $startnetText, [Text.Encoding]::ASCII)

$commands = @(
    "add '$driverInf' '\Windows\System32\NegroniTouch\NegroniTouch.inf'"
    "add '$driverSys' '\Windows\System32\NegroniTouch\NegroniTouch.sys'"
    "add '$startnet' '\Windows\System32\startnet.cmd'"
) -join "`n"
$psi = [Diagnostics.ProcessStartInfo]::new()
$psi.FileName = $wimlib
$psi.Arguments = "update `"$wim`" 1"
$psi.UseShellExecute = $false
$psi.CreateNoWindow = $true
$psi.RedirectStandardInput = $true
$psi.RedirectStandardOutput = $true
$psi.RedirectStandardError = $true
$process = [Diagnostics.Process]::Start($psi)
$stdoutTask = $process.StandardOutput.ReadToEndAsync()
$stderrTask = $process.StandardError.ReadToEndAsync()
$process.StandardInput.WriteLine($commands)
$process.StandardInput.Close()
$process.WaitForExit()
$stdout = $stdoutTask.Result
$stderr = $stderrTask.Result
Write-Host $stdout
if ($stderr) { Write-Host $stderr }
if ($process.ExitCode -ne 0) { throw "wimlib update failed ($($process.ExitCode))." }

# Verify that the correct binary and startup hook landed in the ARM64 image.
$verifyDir = Join-Path $work 'verify'
New-Item -ItemType Directory -Force -Path $verifyDir | Out-Null
& $wimlib extract $wim 1 '\Windows\System32\NegroniTouch\NegroniTouch.inf' '\Windows\System32\NegroniTouch\NegroniTouch.sys' '\Windows\System32\startnet.cmd' "--dest-dir=$verifyDir"
if ($LASTEXITCODE -ne 0) { throw "Could not verify updated WIM ($LASTEXITCODE)" }
$verifiedSys = Join-Path $verifyDir 'NegroniTouch.sys'
if ((Get-FileHash -LiteralPath $verifiedSys -Algorithm SHA256).Hash -ne $ExpectedSysSha256) {
    throw 'The driver inside the WIM does not match the expected OnePlus driver.'
}
$verifiedStartnet = [IO.File]::ReadAllText((Join-Path $verifyDir 'startnet.cmd'))
if ($verifiedStartnet -notmatch '(?i)drvload.*NegroniTouch\.inf') {
    throw 'The updated WIM startnet.cmd does not load the OnePlus touch driver.'
}
if ($verifiedStartnet -notmatch '(?im)^start\s+%SystemRoot%\\explorer\.exe\s*$') {
    throw 'The updated WIM startnet.cmd does not start Explorer for the taskbar.'
}

& bcdedit.exe /store $bcd /set '{default}' testsigning on
if ($LASTEXITCODE -ne 0) { throw "bcdedit testsigning failed ($LASTEXITCODE)" }
& bcdedit.exe /store $bcd /set '{default}' nointegritychecks on
if ($LASTEXITCODE -ne 0) { throw "bcdedit nointegritychecks failed ($LASTEXITCODE)" }
$entry = & bcdedit.exe /store $bcd /enum '{default}'
if ($LASTEXITCODE -ne 0) { throw "bcdedit verification failed ($LASTEXITCODE)" }
$entry

Write-Host "DONE: BYDJPE now contains NegroniTouch.sys ($actualHash)."
Write-Host 'startnet loads the driver, then starts Explorer for the taskbar; the existing WinPE shell setup is preserved.'
Write-Host "Updated WIM SHA256: $((Get-FileHash -LiteralPath $wim -Algorithm SHA256).Hash)"
Stop-Transcript | Out-Null
