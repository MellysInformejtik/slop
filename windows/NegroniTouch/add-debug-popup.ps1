[CmdletBinding()]
param([string]$Wim = 'C:\Users\paw\Downloads\BYDJPE-V1.1\sources\boot.wim')
$ErrorActionPreference = 'Stop'
$tool = Join-Path $PSScriptRoot 'out\wimlib-1.14.5\wimlib-imagex.exe'
$stamp = Get-Date -Format 'yyyyMMdd-HHmmss'
$work = Join-Path $PSScriptRoot "out\debug-popup-$stamp"
New-Item -ItemType Directory -Path $work | Out-Null
& $tool extract $Wim 1 /Windows/System32/winpeshl.ini "--dest-dir=$work" --no-acls
if ($LASTEXITCODE -ne 0) { throw 'Cannot read the PE shell configuration' }
$shell = Join-Path $work 'winpeshl.ini'
$text = [IO.File]::ReadAllText($shell)
$hook = '%SYSTEMROOT%\System32\cmd.exe, /c %SYSTEMROOT%\System32\NegroniDebugLaunch.cmd'
if ($text -notmatch '(?im)^\[LaunchApps\]\s*$') { throw 'Expected a LaunchApps section' }
if ($text -notmatch 'NegroniDebugLaunch') {
    # Append to LaunchApps only, even if another section follows it.
    $text = [regex]::Replace($text, '(?ims)(^\[LaunchApps\][^\r\n]*\r?\n)(.*?)(?=^\[|\z)', {
        param($m)
        $m.Groups[1].Value + $m.Groups[2].Value.TrimEnd() + "`r`n$hook`r`n"
    })
}
[IO.File]::WriteAllText($shell, $text, [Text.Encoding]::ASCII)
foreach ($name in @('NegroniDebug.cmd', 'NegroniDebugLaunch.cmd')) {
    $content = [IO.File]::ReadAllText((Join-Path $PSScriptRoot $name)) -replace '\r?\n', "`r`n"
    [IO.File]::WriteAllText((Join-Path $work $name), $content, [Text.Encoding]::ASCII)
}
$backup = Join-Path (Split-Path $Wim) "boot-before-debug-popup-$stamp.wim"
Copy-Item -LiteralPath $Wim -Destination $backup
$commands = @('winpeshl.ini', 'NegroniDebug.cmd', 'NegroniDebugLaunch.cmd') | ForEach-Object {
    "add '$(Join-Path $work $_)' '/Windows/System32/$_'"
}
$commands | & $tool update $Wim 1
if ($LASTEXITCODE -ne 0) { throw "WIM update failed; backup is $backup" }
& $tool verify $Wim
if ($LASTEXITCODE -ne 0) { throw "WIM verification failed; backup is $backup" }
$verify = Join-Path $work 'verified'
New-Item -ItemType Directory -Path $verify | Out-Null
& $tool extract $Wim 1 /Windows/System32/winpeshl.ini /Windows/System32/NegroniDebug.cmd /Windows/System32/NegroniDebugLaunch.cmd "--dest-dir=$verify" --no-acls
if ($LASTEXITCODE -ne 0) { throw 'Cannot extract updated files' }
foreach ($name in @('winpeshl.ini', 'NegroniDebug.cmd', 'NegroniDebugLaunch.cmd')) {
    if ((Get-FileHash (Join-Path $work $name)).Hash -ne (Get-FileHash (Join-Path $verify $name)).Hash) {
        throw "Verification mismatch: $name"
    }
}
Write-Host "Backup: $backup"
Get-FileHash -LiteralPath $Wim -Algorithm SHA256
