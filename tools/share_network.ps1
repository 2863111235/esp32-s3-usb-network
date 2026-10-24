param([switch]$Disable, [int]$UpstreamInterfaceIndex = 8)
$ErrorActionPreference = 'Stop'
$taskIcs = New-Object -ComObject HNetCfg.HNetShare
$taskConnections = @($taskIcs.EnumEveryConnection() | ForEach-Object {
    $taskProps = $taskIcs.NetConnectionProps($_)
    $taskConfig = $taskIcs.INetSharingConfigurationForINetConnection($_)
    [pscustomobject]@{ Connection=$_; Config=$taskConfig; Name=$taskProps.Name; Guid=$taskProps.Guid; DeviceName=$taskProps.DeviceName }
})
$taskUsb = @(Get-NetAdapter | Where-Object { $_.MacAddress -eq '02-A3-53-19-00-01' })
if ($taskUsb.Count -ne 1) { throw 'Expected one ESP32-S3 USB NCM adapter (02-A3-53-19-00-01).' }
$taskUsbGuid = ([guid]$taskUsb[0].InterfaceGuid).ToString('B')
$taskPrivate = @($taskConnections | Where-Object { $_.Guid -eq $taskUsbGuid })
if ($taskPrivate.Count -ne 1) { throw 'USB adapter was not found in Windows ICS.' }
$taskStatePath = Join-Path $PSScriptRoot 'network_sharing_state.json'
if ($Disable) {
    if (-not (Test-Path -LiteralPath $taskStatePath)) { throw 'No saved sharing configuration.' }
    $taskState = Get-Content -LiteralPath $taskStatePath -Raw | ConvertFrom-Json
    foreach ($taskGuid in @($taskState.PrivateGuid, $taskState.PublicGuid)) {
        $taskItem = $taskConnections | Where-Object Guid -eq $taskGuid
        if ($taskItem -and $taskItem.Config.SharingEnabled) { $taskItem.Config.DisableSharing() }
    }
    'ESP32 USB Internet sharing disabled.'
    return
}
$taskUpstream = Get-NetAdapter -InterfaceIndex $UpstreamInterfaceIndex
if ($taskUpstream.Status -ne 'Up' -or -not $taskUpstream.HardwareInterface) {
    throw 'The verified wired upstream adapter has changed; inspect the configuration first.'
}
$taskPublicGuid = ([guid]$taskUpstream.InterfaceGuid).ToString('B')
$taskPublic = @($taskConnections | Where-Object { $_.Guid -eq $taskPublicGuid })
if ($taskPublic.Count -ne 1) { throw 'Wired upstream was not found in Windows ICS.' }
foreach ($taskItem in $taskConnections) {
    if ($taskItem.Config.SharingEnabled -and $taskItem.Guid -notin @($taskPublic[0].Guid, $taskPrivate[0].Guid)) {
        throw ('Another Internet sharing configuration is already active: ' + $taskItem.Name)
    }
}
if (-not (Test-Path -LiteralPath $taskStatePath)) {
    if ($taskPublic[0].Config.SharingEnabled -or $taskPrivate[0].Config.SharingEnabled) {
        throw 'Sharing was already enabled without a saved baseline.'
    }
    [pscustomobject]@{
        PublicName=$taskPublic[0].Name; PublicGuid=$taskPublic[0].Guid
        PrivateName=$taskPrivate[0].Name; PrivateGuid=$taskPrivate[0].Guid
        OriginalSharingEnabled=$false; CreatedAt=[DateTime]::UtcNow.ToString('o')
    } | ConvertTo-Json | Set-Content -LiteralPath $taskStatePath -Encoding utf8
}
$taskRebind = $false
if ($taskPublic[0].Config.SharingEnabled -and $taskPrivate[0].Config.SharingEnabled) {
    # After USB re-enumeration ICS can retain its flags but lose the DHCP binding.
    $taskPrivateAddresses = @(Get-NetIPAddress -InterfaceIndex $taskUsb[0].InterfaceIndex -AddressFamily IPv4 |
        ForEach-Object IPAddress)
    $taskListeners = @(Get-NetUDPEndpoint -LocalPort 67 -ErrorAction SilentlyContinue |
        Where-Object { $_.LocalAddress -eq '0.0.0.0' -or $_.LocalAddress -in $taskPrivateAddresses })
    $taskRebind = $taskListeners.Count -eq 0
}
try {
    if ($taskRebind) {
        $taskPrivate[0].Config.DisableSharing()
        $taskPublic[0].Config.DisableSharing()
    }
    if (-not $taskPublic[0].Config.SharingEnabled) { $taskPublic[0].Config.EnableSharing(0) }
    if (-not $taskPrivate[0].Config.SharingEnabled) { $taskPrivate[0].Config.EnableSharing(1) }
} catch {
    foreach ($taskItem in @($taskPrivate[0], $taskPublic[0])) {
        if ($taskItem.Config.SharingEnabled) { $taskItem.Config.DisableSharing() }
    }
    throw
}
[pscustomobject]@{
    Public=$taskPublic[0].Name; Private=$taskPrivate[0].Name
    PublicSharing=$taskPublic[0].Config.SharingEnabled
    PrivateSharing=$taskPrivate[0].Config.SharingEnabled
    ReboundAfterMissingDhcp=$taskRebind
} | ConvertTo-Json -Compress

