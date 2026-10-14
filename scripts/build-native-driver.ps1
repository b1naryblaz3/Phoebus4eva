param(
    [ValidateSet('Driver', 'Tests', 'All')][string]$Target = 'All',
    [string]$ToolchainRoot = '',
    [string]$OutputDirectory = ''
)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$projectRoot = Split-Path -Parent $PSScriptRoot
if (!$ToolchainRoot) { $ToolchainRoot = Join-Path $projectRoot 'vendor\native-tools' }
if (!$OutputDirectory) { $OutputDirectory = Join-Path $projectRoot 'dist\driver' }
$ToolchainRoot = [IO.Path]::GetFullPath($ToolchainRoot)
$OutputDirectory = [IO.Path]::GetFullPath($OutputDirectory)
$msvcRoot = Join-Path $ToolchainRoot 'msvc\Contents\VC\Tools\MSVC\14.44.35207'
$sdkRoot = Join-Path $ToolchainRoot 'sdk\c'
$wdkRoot = Join-Path $ToolchainRoot 'wdk\c'
$kitVersion = '10.0.26100.0'
$compiler = Join-Path $msvcRoot 'bin\Hostx64\x64\cl.exe'
$linker = Join-Path $msvcRoot 'bin\Hostx64\x64\link.exe'
foreach ($required in @($compiler, $linker, "$wdkRoot\Include\$kitVersion\km\hdaudio.h", "$sdkRoot\Include\$kitVersion\shared\ksmedia.h")) {
    if (!(Test-Path -LiteralPath $required)) { throw "Missing native dependency: $required. Run scripts/setup-native-toolchain.py first." }
}
New-Item -ItemType Directory -Path $OutputDirectory -Force | Out-Null
$commonFlags = @('/nologo', '/std:c++17', '/W4', '/WX', '/X', '/Z7', '/GS', '/Qspectre', '/guard:cf', '/D_WIN32_WINNT=0x0A00')

function Invoke-NativeTool([string]$toolPath, [string[]]$toolArguments) {
    & $toolPath @toolArguments
    if ($LASTEXITCODE -ne 0) { throw "Native tool failed ($LASTEXITCODE): $toolPath" }
}

if ($Target -in @('Tests', 'All')) {
    $testSources = @(Get-ChildItem -LiteralPath (Join-Path $projectRoot 'tests') -Filter '*_tests.cpp' -File | Sort-Object Name)
    if (!$testSources.Count) { throw 'No native test sources found.' }
    foreach ($sourceFile in $testSources) {
        $testName = $sourceFile.BaseName
        $testObject = Join-Path $OutputDirectory "$testName.obj"
        $testExecutable = Join-Path $OutputDirectory "$testName.exe"
        $testFlags = $commonFlags + @('/c', '/MT', '/EHsc', '/O2', "/I$msvcRoot\include", "/I$sdkRoot\Include\$kitVersion\ucrt", "/I$sdkRoot\Include\$kitVersion\shared", "/I$sdkRoot\Include\$kitVersion\um", "/Fo$testObject", $sourceFile.FullName)
        Invoke-NativeTool $compiler $testFlags
        $testLinkFlags = @('/nologo', '/machine:x64', '/subsystem:console', '/dynamicbase', '/nxcompat', '/guard:cf', "/out:$testExecutable", "/pdb:$OutputDirectory\$testName.pdb", "/libpath:$msvcRoot\lib\x64", "/libpath:$sdkRoot\ucrt\x64", "/libpath:$sdkRoot\um\x64", $testObject)
        Invoke-NativeTool $linker $testLinkFlags
        Invoke-NativeTool $testExecutable @()
    }
}

if ($Target -in @('Driver', 'All')) {
    $driverSources = @(Get-ChildItem -LiteralPath (Join-Path $projectRoot 'driver') -Filter '*.cpp' -File | Sort-Object Name)
    if (!$driverSources.Count) { throw 'No driver C++ sources found.' }
    $driverObjects = @()
    foreach ($sourceFile in $driverSources) {
        $driverObject = Join-Path $OutputDirectory ($sourceFile.BaseName + '.obj')
        $driverFlags = $commonFlags + @('/c', '/kernel', '/GR-', '/EHs-c-', '/Zl', '/O2', '/D_AMD64_', '/DAMD64', '/D_WIN64', '/DNTDDI_VERSION=NTDDI_WIN10_CO', "/I$wdkRoot\Include\$kitVersion\km", "/I$wdkRoot\Include\$kitVersion\km\crt", "/I$sdkRoot\Include\$kitVersion\shared", "/I$sdkRoot\Include\$kitVersion\um", "/I$sdkRoot\Include\$kitVersion\ucrt", "/I$msvcRoot\include", "/Fo$driverObject", $sourceFile.FullName)
        Invoke-NativeTool $compiler $driverFlags
        $driverObjects += $driverObject
    }
    $driverBinary = Join-Path $OutputDirectory 'PhoebusW11.sys'
    $driverLinkFlags = @('/nologo', '/driver', '/kernel', '/subsystem:native,10.0', '/entry:GsDriverEntry', '/nodefaultlib', '/machine:x64', '/integritycheck', '/nxcompat', '/dynamicbase', '/guard:cf', '/release', '/debug:full', "/out:$driverBinary", "/pdb:$OutputDirectory\PhoebusW11.pdb", "/map:$OutputDirectory\PhoebusW11.map", "/libpath:$wdkRoot\Lib\$kitVersion\km\x64", 'ntoskrnl.lib', 'hal.lib', 'portcls.lib', 'ks.lib', 'BufferOverflowFastFailK.lib') + $driverObjects
    Invoke-NativeTool $linker $driverLinkFlags
    $sourceInf = Join-Path $projectRoot 'driver\phoebus.inf'
    if (!(Test-Path -LiteralPath $sourceInf)) { throw 'Missing driver/phoebus.inf.' }
    $packageInf = Join-Path $OutputDirectory 'phoebus.inf'
    Copy-Item -LiteralPath $sourceInf -Destination $packageInf -Force
    $infVerifier = Join-Path $wdkRoot "tools\$kitVersion\x64\infverif.exe"
    $inf2cat = Join-Path $wdkRoot "bin\$kitVersion\x86\Inf2Cat.exe"
    foreach ($validator in @($infVerifier, $inf2cat)) {
        if (!(Test-Path -LiteralPath $validator)) { throw "Missing package validator: $validator" }
    }
    Invoke-NativeTool $infVerifier @('/w', '/v', $packageInf)
    Invoke-NativeTool $inf2cat @("/driver:$OutputDirectory", '/os:10_CO_X64,10_NI_X64,10_GE_X64', '/nocat', '/verbose')
    Write-Output "Built unsigned development driver: $driverBinary"
}
