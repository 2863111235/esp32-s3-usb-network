param(
    [ValidateSet('hotspot','wifi')][string]$Mode = 'hotspot',
    [string]$Cli = 'arduino-cli',
    [string]$ConfigFile,
    [string]$Port,
    [switch]$Upload
)
$ErrorActionPreference = 'Stop'
$taskRoot = Split-Path $PSScriptRoot -Parent
$taskSketchName = if ($Mode -eq 'wifi') { 'esp32_usb_wifi_adapter' } else { 'esp32_usb_hotspot' }
$taskSketch = Join-Path $taskRoot ('sketches\' + $taskSketchName)
$taskBuild = Join-Path ([IO.Path]::GetTempPath()) ('esp32-s3-usb-network-' + $Mode)
$taskBoard = 'esp32:esp32:esp32s3:USBMode=default,CDCOnBoot=default,FlashSize=16M,PartitionScheme=default,PSRAM=opi,UploadSpeed=921600'
$taskCommon = @('--fqbn',$taskBoard)
if ($ConfigFile) { $taskCommon += @('--config-file',$ConfigFile) }
& $Cli compile @taskCommon --libraries (Join-Path $taskRoot 'libraries') --build-path $taskBuild $taskSketch
if ($LASTEXITCODE -ne 0) { throw 'Arduino CLI compile failed.' }
if ($Upload) {
    if (-not $Port) { throw 'Provide the verified ROM download port with -Port.' }
    & $Cli upload @taskCommon --port $Port --input-dir $taskBuild $taskSketch
    if ($LASTEXITCODE -ne 0) { throw 'Arduino CLI upload failed.' }
}
Write-Output ('Build directory: ' + $taskBuild)
