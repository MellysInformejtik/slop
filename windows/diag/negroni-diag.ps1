# Negroni Windows diagnostics on the phone itself (no USB, no network): what the GPU, touch and key drivers did,
# in a report opened in Notepad. Also saved to the desktop and C:\negroni-diag.txt.
#   negroni-diag.ps1 [-Install] [-Off] [-NoOpen]
#   -Install  install C:\NegroniGpu\NegroniGpuWddm.inf (pnputil) before the report
#   -Off      turn the GPU driver off (service Start=4, device disabled) before the report
param([switch]$Install, [switch]$Off, [switch]$NoOpen)
$ErrorActionPreference = 'Continue'
$lines = New-Object System.Collections.Generic.List[string]
function Say([string]$s) { $lines.Add($s) }
function Run([string]$title, [scriptblock]$what) {
    Say ''; Say "==== $title ===="
    try { (& $what 2>&1 | Out-String -Width 200).TrimEnd() -split "`r?`n" | ForEach-Object { Say $_ } }
    catch { Say "  (failed: $($_.Exception.Message))" }
}
$admin = ([Security.Principal.WindowsPrincipal][Security.Principal.WindowsIdentity]::GetCurrent()).IsInRole(
    [Security.Principal.WindowsBuiltInRole]::Administrator)

if ($Install) {
    Run 'Install GPU driver' { pnputil /add-driver C:\NegroniGpu\NegroniGpuWddm.inf /install }
}
if ($Off) {
    Run 'Turn GPU driver off' {
        reg add HKLM\SYSTEM\CurrentControlSet\Services\NegroniGpuWddm /v Start /t REG_DWORD /d 4 /f
        pnputil /disable-device "ACPI\QCOM0A73\0"
    }
}

$stages = @{ 0 = 'never ran'; 1 = 'DriverEntry'; 2 = 'AddDevice'; 3 = 'StartDevice entered'; 4 = 'device information';
             5 = 'register windows mapped'; 6 = 'firmware package'; 7 = 'read-only power check'; 8 = 'STARTED' }
$svc = 'HKLM:\SYSTEM\CurrentControlSet\Services\NegroniGpuWddm'
$gpu = Get-ItemProperty $svc -ErrorAction SilentlyContinue

Say "NEGRONI DIAGNOSTICS  $(Get-Date -Format 'yyyy-MM-dd HH:mm:ss')  (admin: $admin)"
Say "Windows $([Environment]::OSVersion.Version)  build $((Get-ItemProperty 'HKLM:\SOFTWARE\Microsoft\Windows NT\CurrentVersion').DisplayVersion)"
Say ''
Say '---- SUMMARY ----'
foreach ($d in @(@('GPU   (A730)', 'ACPI\QCOM0A73\0'), @('Touch', 'ACPI\NEGT0001\0'), @('Keys ', 'ACPI\NEGK0001\0'))) {
    $p = Get-PnpDevice -InstanceId $d[1] -ErrorAction SilentlyContinue
    if ($p) {
        $code = (Get-PnpDeviceProperty -InstanceId $d[1] -KeyName DEVPKEY_Device_ProblemCode -ErrorAction SilentlyContinue).Data
        Say ("{0}: {1}  status {2}  problem code {3}  driver {4}" -f $d[0], $p.FriendlyName, $p.Status, $code,
             (Get-PnpDeviceProperty -InstanceId $d[1] -KeyName DEVPKEY_Device_DriverInfPath -ErrorAction SilentlyContinue).Data)
    } else {
        Say ("{0}: device {1} NOT PRESENT (ACPI table missing?)" -f $d[0], $d[1])
    }
}
if ($gpu) {
    $st = [int]$gpu.Stage
    Say ("GPU driver: last stage {0} ({1}), status 0x{2:X8}, detail 0x{3:X}, service Start={4}" -f $st, $stages[$st],
         [uint32]$gpu.Status, [uint32]$gpu.Detail, $gpu.Start)
    foreach ($n in 'GccGpuCfgAhb', 'GpuccCxGdsc', 'GpuccGxGdsc', 'GpuccAhb', 'GpuccGmuClock', 'GmuAoCounterDelta', 'GmuFwInit',
                   'UnexpectedRangeStart', 'UnexpectedRangeLength') {
        if ($null -ne $gpu.$n) { Say ("  {0} = 0x{1:X8}" -f $n, [uint32]$gpu.$n) }
    }
} else {
    Say 'GPU driver: not installed (tap "Install GPU driver")'
}
$keys = Get-ItemProperty 'HKLM:\SYSTEM\CurrentControlSet\Services\NegroniKeys' -ErrorAction SilentlyContinue
if ($keys -and $null -ne $keys.Stage) {
    Say ("Keys driver: stage {0}, status 0x{1:X8}, detail 0x{2:X}" -f $keys.Stage, [uint32]$keys.Status, [uint32]$keys.Detail)
}
$bug = Get-WinEvent -FilterHashtable @{ LogName = 'System'; Id = 1001; ProviderName = 'Microsoft-Windows-WER-SystemErrorReporting' } -MaxEvents 1 -ErrorAction SilentlyContinue
if ($bug) { Say ("Last bugcheck: {0}  {1}" -f $bug.TimeCreated, ($bug.Message -split "`n")[0]) } else { Say 'Last bugcheck: none recorded' }

Run 'Devices with problems' { pnputil /enum-devices /problem }
Run 'Display adapters' { pnputil /enum-devices /class Display }
Run 'GPU driver registry' { reg query HKLM\SYSTEM\CurrentControlSet\Services\NegroniGpuWddm }
Run 'Driver services' { foreach ($s in 'NegroniGpuWddm', 'NegroniTouch', 'NegroniKeys', 'BasicDisplay') { sc.exe query $s | Select-String 'SERVICE_NAME|STATE|WIN32_EXIT' } }
Run 'Driver install log (setupapi, Negroni and failures)' {
    $log = "$env:SystemRoot\INF\setupapi.dev.log"
    if (Test-Path $log) { Select-String -Path $log -Pattern '!!!', 'NegroniGpu', 'QCOM0A73' | Select-Object -Last 60 | ForEach-Object { $_.Line } }
}
Run 'System errors (newest 25)' {
    Get-WinEvent -FilterHashtable @{ LogName = 'System'; Level = 1, 2 } -MaxEvents 25 -ErrorAction SilentlyContinue |
        ForEach-Object { "{0}  {1}  {2}: {3}" -f $_.TimeCreated, $_.Id, $_.ProviderName, (($_.Message -split "`n")[0]) }
}
Run 'Crash dumps' { Get-ChildItem C:\Windows\Minidump, C:\Windows\MEMORY.DMP -ErrorAction SilentlyContinue | Select-Object FullName, Length, LastWriteTime }
if ($admin) { Run 'Boot settings' { bcdedit /enum '{current}' | Select-String 'testsigning|nointegritychecks|numproc|path' } }

$text = $lines -join "`r`n"
$desk = [Environment]::GetFolderPath('Desktop')
$written = $null
# the desktop is writable without admin (the run at logon has none); C:\ only with it
foreach ($f in @((Join-Path $desk 'negroni-diag.txt'), 'C:\negroni-diag.txt')) {
    try {
        Set-Content -Path $f -Value $text -Encoding UTF8 -ErrorAction Stop
        if (-not $written) { $written = $f }
    } catch { }
}
if (-not $written) {
    $written = Join-Path $env:TEMP 'negroni-diag.txt'
    Set-Content -Path $written -Value $text -Encoding UTF8
}
if (-not $NoOpen) { Start-Process notepad.exe $written }
