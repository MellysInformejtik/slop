# Run on the OnePlus Windows installation, before and after connecting a hub.
# Read-only diagnostics. Does not install drivers, change USB role or power GPIOs.
param([string]$OutputDirectory = "$env:USERPROFILE\Desktop\NegroniUsb")
$ErrorActionPreference = 'Continue'
New-Item -ItemType Directory -Force -Path $OutputDirectory | Out-Null
$report = Join-Path $OutputDirectory ('usb-' + (Get-Date -Format yyyyMMdd-HHmmss) + '.txt')
Start-Transcript -Path $report
try {
    Get-CimInstance Win32_OperatingSystem | Select-Object Caption, Version, OSArchitecture
    $devices = Get-PnpDevice -PresentOnly | Where-Object {
        $_.Class -in @('USB','USBDevice','UCM') -or
        $_.InstanceId -match 'QCOM0497|PNP0CA1|QCOM0C8E|^URS\\' -or
        $_.FriendlyName -match 'USB|UCSI|Type.C|Role Switch'
    }
    $devices | Format-Table Status, Class, FriendlyName, InstanceId -AutoSize -Wrap
    foreach ($device in $devices) {
        "`nDEVICE: $($device.InstanceId)"
        Get-PnpDeviceProperty -InstanceId $device.InstanceId -KeyName `
            'DEVPKEY_Device_ProblemCode','DEVPKEY_Device_ProblemStatus',`
            'DEVPKEY_Device_HardwareIds','DEVPKEY_Device_Service',`
            'DEVPKEY_Device_DriverInfPath','DEVPKEY_Device_Parent' |
            Format-List KeyName, Data
    }
    pnputil /enum-devices /class USB /connected /drivers
    Get-CimInstance Win32_USBController | Format-List Name, PNPDeviceID, Status
    Get-CimInstance Win32_USBHub | Format-Table Name, PNPDeviceID, Status -Wrap
    Get-WinEvent -FilterHashtable @{LogName='System'; StartTime=(Get-Date).AddHours(-2)} |
        Where-Object { $_.ProviderName -match 'Kernel-PnP|USB|Ucm' } |
        Select-Object -First 80 TimeCreated, Id, ProviderName, Message | Format-List
} finally {
    Stop-Transcript
}
Write-Host "Saved $report"
