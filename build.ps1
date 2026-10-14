[CmdletBinding()]
param()
$ErrorActionPreference = 'Stop'
$compiler = Join-Path $env:WINDIR 'Microsoft.NET\Framework64\v4.0.30319\csc.exe'
if (-not (Test-Path -LiteralPath $compiler)) {
    throw 'The Windows .NET Framework C# compiler was not found. See README.md for build requirements.'
}
$outputDirectory = Join-Path $PSScriptRoot 'dist'
New-Item -ItemType Directory -Path $outputDirectory -Force | Out-Null
$arguments = @('/nologo', '/target:exe', '/platform:x64', '/optimize+', '/warnaserror+',
    '/reference:System.Web.Extensions.dll',
    ('/out:' + (Join-Path $outputDirectory 'PhoebusProbe.exe')),
    (Join-Path $PSScriptRoot 'src\PhoebusProbe.cs'))
& $compiler @arguments
if ($LASTEXITCODE -ne 0) { throw "Compilation failed ($LASTEXITCODE)." }
Write-Host "Built $(Join-Path $outputDirectory 'PhoebusProbe.exe')"
