param([switch]$Renew)
$ErrorActionPreference = 'Stop'
$taskUsb = @(Get-NetAdapter | Where-Object MacAddress -eq '02-A3-53-19-00-01')
if ($taskUsb.Count -ne 1) { throw 'Expected one ESP32-S3 USB NCM adapter.' }
$taskName = $taskUsb[0].Name
$taskIndex = $taskUsb[0].InterfaceIndex
$taskIcs = New-Object -ComObject HNetCfg.HNetShare
foreach ($taskConnection in $taskIcs.EnumEveryConnection()) {
    $taskProperties = $taskIcs.NetConnectionProps($taskConnection)
    $taskConfig = $taskIcs.INetSharingConfigurationForINetConnection($taskConnection)
    if ($taskProperties.Guid -eq ([guid]$taskUsb[0].InterfaceGuid).ToString('B') -and $taskConfig.SharingEnabled) {
        throw 'This USB adapter still has ICS enabled. Disable the hotspot sharing configuration first.'
    }
}
if (-not $Renew) {
    # Remove only this project's former ICS address; leave unrelated addresses alone.
    Get-NetIPAddress -InterfaceIndex $taskIndex -AddressFamily IPv4 -ErrorAction SilentlyContinue |
        Where-Object IPAddress -eq '192.168.137.1' | Remove-NetIPAddress -Confirm:$false
    Set-NetIPInterface -InterfaceIndex $taskIndex -AddressFamily IPv4 -Dhcp Enabled
    Set-DnsClientServerAddress -InterfaceIndex $taskIndex -ResetServerAddresses
}
& ipconfig.exe /renew $taskName
if ($LASTEXITCODE -ne 0) { throw 'USB adapter DHCP renewal failed.' }
Get-NetIPConfiguration -InterfaceIndex $taskIndex
