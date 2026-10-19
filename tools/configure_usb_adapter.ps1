param([switch]$Restore)
$ErrorActionPreference = 'Stop'
$taskUsb = @(Get-NetAdapter | Where-Object MacAddress -eq '02-A3-53-19-00-01')
if ($taskUsb.Count -ne 1) { throw 'Expected one ESP32-S3 USB adapter.' }
$taskName = $taskUsb[0].Name
$taskBaseline = Join-Path $PSScriptRoot 'usb_adapter_original_settings.json'
$taskKeys = @('*IPChecksumOffloadIPv4','*TCPChecksumOffloadIPv4','*TCPChecksumOffloadIPv6','*UDPChecksumOffloadIPv4','*UDPChecksumOffloadIPv6','*LsoV2Ipv4','*LsoV2Ipv6','*RscIpv4','*RscIpv6')
if ($Restore) {
    if (-not (Test-Path -LiteralPath $taskBaseline)) { throw 'Original USB adapter settings are missing.' }
    $taskSettings = Get-Content -LiteralPath $taskBaseline -Raw | ConvertFrom-Json
    foreach ($taskSetting in $taskSettings) {
        if ($taskSetting.RegistryKeyword -in $taskKeys) {
            Set-NetAdapterAdvancedProperty -Name $taskName -RegistryKeyword $taskSetting.RegistryKeyword -RegistryValue $taskSetting.RegistryValue -NoRestart
        }
    }
} else {
    if (-not (Test-Path -LiteralPath $taskBaseline)) {
        Get-NetAdapterAdvancedProperty -Name $taskName | Select-Object RegistryKeyword,RegistryValue |
            ConvertTo-Json -Depth 4 | Set-Content -LiteralPath $taskBaseline -Encoding UTF8
    }
    Disable-NetAdapterChecksumOffload -Name $taskName -NoRestart -Confirm:$false
    Disable-NetAdapterLso -Name $taskName -NoRestart -Confirm:$false
    Disable-NetAdapterRsc -Name $taskName -NoRestart -Confirm:$false
}
Restart-NetAdapter -Name $taskName -Confirm:$false
Write-Output 'USB adapter settings applied. Wait for the adapter to reconnect before enabling ICS.'
