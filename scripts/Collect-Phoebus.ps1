[CmdletBinding()]
param([string]$OutputDirectory = (Join-Path (Split-Path $PSScriptRoot -Parent) 'reports'))
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path $PSScriptRoot -Parent
$probe = Join-Path $projectRoot 'dist\PhoebusProbe.exe'
if (-not (Test-Path -LiteralPath $probe)) { throw 'Build first: .\build.ps1' }
New-Item -ItemType Directory -Path $OutputDirectory -Force | Out-Null
$errorsFound = [System.Collections.Generic.List[string]]::new()
$devices = @()
$drivers = @()
try {
    $devices = @(Get-CimInstance Win32_PnPEntity | Where-Object {
        $_.PNPDeviceID -match 'HDAUDIO\\FUNC_01&VEN_13F6&DEV_8888&SUBSYS_104384D5' -or
        $_.PNPDeviceID -match '^PCI\\VEN_13F6&DEV_5011'
    } | Select-Object Name,Status,ConfigManagerErrorCode,PNPDeviceID,Service)
    $drivers = @(Get-CimInstance Win32_PnPSignedDriver | Where-Object {
        $_.DeviceID -match 'HDAUDIO\\FUNC_01&VEN_13F6&DEV_8888&SUBSYS_104384D5' -or
        $_.DeviceName -match 'Phoebus'
    } | Select-Object DeviceName,DeviceID,DriverVersion,InfName,DriverProviderName,IsSigned,Signer)
} catch { $errorsFound.Add('PnP inspection failed: ' + $_.Exception.Message) }
$windows = $null
try {
    $windows = Get-ItemProperty -LiteralPath 'HKLM:\SOFTWARE\Microsoft\Windows NT\CurrentVersion' |
        Select-Object DisplayVersion,CurrentBuildNumber,UBR
} catch { $errorsFound.Add('Windows build inspection failed: ' + $_.Exception.Message) }
$legacyHooks = @()
try {
    $legacyHooks = @(Get-Process -ErrorAction Stop | Where-Object {
        $_.ProcessName -match '^(ExMgr|ExMgr64|HsMgr|HsMgr64|CmeAuVist|CmeAuVist64|ATLOISAService)$'
    } | Select-Object ProcessName,Id)
} catch { $errorsFound.Add('Legacy process inspection failed: ' + $_.Exception.Message) }
$report = [ordered]@{
    CapturedAtUtc = [DateTime]::UtcNow.ToString('o')
    Windows = $windows
    DetectionStatus = $(if ($errorsFound.Count) { 'inspection-incomplete' } elseif ($devices.Count) { 'hardware-detected' } else { 'hardware-not-detected' })
    PhoebusDevices = $devices
    PhoebusDrivers = $drivers
    LegacyAudioProcesses = $legacyHooks
    Errors = @($errorsFound.ToArray())
    Limitation = 'No settings changed. Audio hooks being present does not establish the cause of glitches.'
}
$inventoryPath = Join-Path $OutputDirectory 'inventory.json'
$report | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $inventoryPath -Encoding UTF8
& $probe --json (Join-Path $OutputDirectory 'endpoints.json')
$probeResult = $LASTEXITCODE
Write-Host "Inventory: $inventoryPath"
Write-Host "Phoebus detection: $($report.DetectionStatus)"
if ($errorsFound.Count) { throw ($errorsFound -join '; ') }
if ($probeResult -ne 0) { throw "Endpoint probe returned $probeResult. Review its report for errors." }
