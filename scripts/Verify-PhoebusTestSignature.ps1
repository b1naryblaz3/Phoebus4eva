# SPDX-License-Identifier: MIT
# Read-only cryptographic checks; never imports a certificate or modifies trust.
param([Parameter(Mandatory=$true)][string]$PackageDirectory,
      [Parameter(Mandatory=$true)][string]$PythonPath)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
Add-Type -AssemblyName System.Security
$root = (Resolve-Path -LiteralPath $PackageDirectory).Path
$cert = [Security.Cryptography.X509Certificates.X509Certificate2]::new(
    [IO.File]::ReadAllBytes((Join-Path $root 'Phoebus4eva-Test.cer')))
$temporaryContent = Join-Path ([IO.Path]::GetTempPath()) ('phoebus-cms-' + [Guid]::NewGuid().ToString('N') + '.der')
try {
    $sysBytes = [IO.File]::ReadAllBytes((Join-Path $root 'PhoebusW11.sys'))
    $pe = [BitConverter]::ToInt32($sysBytes,60)
    $security = $pe + 24 + 112 + 32
    $offset = [BitConverter]::ToInt32($sysBytes,$security)
    $size = [BitConverter]::ToInt32($sysBytes,$offset)
    if ($offset -lt 0 -or $size -lt 8 -or ([long]$offset + $size) -gt $sysBytes.Length) { throw 'Invalid certificate bounds.' }
    $signatureBytes = New-Object byte[] ($size - 8)
    [Array]::Copy($sysBytes,$offset+8,$signatureBytes,0,$signatureBytes.Length)
    $imageCms = [Security.Cryptography.Pkcs.SignedCms]::new()
    $imageCms.Decode($signatureBytes)
    $imageCms.CheckSignature($true)
    $catCms = [Security.Cryptography.Pkcs.SignedCms]::new()
    $catCms.Decode([IO.File]::ReadAllBytes((Join-Path $root 'phoebus.cat')))
    $catCms.CheckSignature($true)
    foreach ($cms in @($imageCms,$catCms)) {
        if ($cms.SignerInfos.Count -ne 1 -or $cms.SignerInfos[0].Certificate.Thumbprint -ne $cert.Thumbprint) { throw 'Unexpected signer.' }
    }
    [IO.File]::WriteAllBytes($temporaryContent,$imageCms.ContentInfo.Content)
    & $PythonPath (Join-Path $PSScriptRoot 'verify-test-image.py') (Join-Path $root 'PhoebusW11.sys') $temporaryContent
    if ($LASTEXITCODE -ne 0) { throw 'Image digest check failed.' }
    Write-Output 'PASS: SYS/CAT CMS signatures match the included certificate. Certificate trust, expiry policy, catalog membership and target kernel acceptance are separate checks.'
} finally {
    if (Test-Path -LiteralPath $temporaryContent) { Remove-Item -LiteralPath $temporaryContent -Force }
    $cert.Dispose()
}
