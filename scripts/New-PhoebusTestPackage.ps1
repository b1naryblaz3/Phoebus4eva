# SPDX-License-Identifier: MIT
# Creates a separate development package. Does not install certificates/drivers,
# modify boot/security settings, or change the original unsigned build.
[CmdletBinding()]
param([string]$OutputDirectory = '')
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$projectRoot = Split-Path $PSScriptRoot -Parent
$runId = [Guid]::NewGuid().ToString('N')
if (!$OutputDirectory) { $OutputDirectory = Join-Path $projectRoot "dist\test-signed-$runId" }
$OutputDirectory = [IO.Path]::GetFullPath($OutputDirectory)
if (Test-Path -LiteralPath $OutputDirectory) { throw 'Choose a new output directory; existing files are never overwritten.' }
$toolRoot = Join-Path $projectRoot 'vendor\native-tools'
$signTool = Join-Path $toolRoot 'sdk\c\bin\10.0.26100.0\x64\signtool.exe'
$inf2cat = Join-Path $toolRoot 'wdk\c\bin\10.0.26100.0\x86\Inf2Cat.exe'
$sysSource = Join-Path $projectRoot 'dist\driver\PhoebusW11.sys'
$infSource = Join-Path $projectRoot 'dist\driver\phoebus.inf'
foreach ($file in @($signTool,$inf2cat,$sysSource,$infSource)) {
    if (!(Test-Path -LiteralPath $file -PathType Leaf)) { throw "Missing: $file" }
}
if ((Get-AuthenticodeSignature -LiteralPath $signTool).Status -ne 'Valid') { throw 'SignTool signature is not valid.' }
$originalSysHash = (Get-FileHash -LiteralPath $sysSource -Algorithm SHA256).Hash.ToLowerInvariant()
if ($originalSysHash -ne 'ec72a75a0a42a7520a742c26d958efecbf84b06012c87250588262ea01e8f2c2') {
    throw 'This helper is pinned to the v0.1.0-preview.1 binary. Review/update its source provenance before signing a different build.'
}
$privateParent = Join-Path $projectRoot 'reports\signing-private'
New-Item -ItemType Directory -Path $privateParent -Force | Out-Null
$privateDirectory = Join-Path $privateParent $runId
New-Item -ItemType Directory -Path $privateDirectory | Out-Null
$userSid = [Security.Principal.WindowsIdentity]::GetCurrent().User.Value
& "$env:WINDIR\System32\icacls.exe" $privateDirectory '/inheritance:r' '/grant:r' "*$($userSid):(OI)(CI)F" | Out-Null
if ($LASTEXITCODE -ne 0) { throw 'Cannot restrict temporary private-key directory.' }
$pfxPath = Join-Path $privateDirectory 'temporary-signing-key.pfx'
$rsa = $null
$certificate = $null
try {
    $rsa = [Security.Cryptography.RSA]::Create()
    $rsa.KeySize = 3072
    $request = [Security.Cryptography.X509Certificates.CertificateRequest]::new(
        'CN=Phoebus4eva development test only', $rsa,
        [Security.Cryptography.HashAlgorithmName]::SHA256,
        [Security.Cryptography.RSASignaturePadding]::Pkcs1)
    $request.CertificateExtensions.Add([Security.Cryptography.X509Certificates.X509BasicConstraintsExtension]::new($false,$false,0,$true))
    $request.CertificateExtensions.Add([Security.Cryptography.X509Certificates.X509KeyUsageExtension]::new(
        [Security.Cryptography.X509Certificates.X509KeyUsageFlags]::DigitalSignature,$true))
    $usages = [Security.Cryptography.OidCollection]::new()
    [void]$usages.Add([Security.Cryptography.Oid]::new('1.3.6.1.5.5.7.3.3'))
    $request.CertificateExtensions.Add([Security.Cryptography.X509Certificates.X509EnhancedKeyUsageExtension]::new($usages,$true))
    $now = [DateTimeOffset]::UtcNow
    $certificate = $request.CreateSelfSigned($now.AddMinutes(-10),$now.AddDays(90))
    $randomBytes = New-Object byte[] 48
    $rng = [Security.Cryptography.RandomNumberGenerator]::Create()
    try { $rng.GetBytes($randomBytes) } finally { $rng.Dispose() }
    $pfxPassword = [Convert]::ToBase64String($randomBytes)
    [IO.File]::WriteAllBytes($pfxPath,$certificate.Export([Security.Cryptography.X509Certificates.X509ContentType]::Pfx,$pfxPassword))
    New-Item -ItemType Directory -Path $OutputDirectory | Out-Null
    Copy-Item -LiteralPath $sysSource -Destination (Join-Path $OutputDirectory 'PhoebusW11.sys')
    Copy-Item -LiteralPath $infSource -Destination (Join-Path $OutputDirectory 'phoebus.inf')
    # Sign the image BEFORE generating its catalog: signing changes image bytes.
    & $signTool sign /q /fd SHA256 /f $pfxPath /p $pfxPassword (Join-Path $OutputDirectory 'PhoebusW11.sys')
    if ($LASTEXITCODE -ne 0) { throw 'SYS signing failed.' }
    & $inf2cat "/driver:$OutputDirectory" '/os:10_CO_X64,10_NI_X64,10_GE_X64'
    if ($LASTEXITCODE -ne 0) { throw 'Catalog creation failed.' }
    & $signTool sign /q /fd SHA256 /f $pfxPath /p $pfxPassword (Join-Path $OutputDirectory 'phoebus.cat')
    if ($LASTEXITCODE -ne 0) { throw 'Catalog signing failed.' }
    [IO.File]::WriteAllBytes((Join-Path $OutputDirectory 'Phoebus4eva-Test.cer'),$certificate.Export([Security.Cryptography.X509Certificates.X509ContentType]::Cert))
    $signatures = @()
    foreach ($name in @('PhoebusW11.sys','phoebus.cat')) {
        $signature = Get-AuthenticodeSignature -LiteralPath (Join-Path $OutputDirectory $name)
        if (!$signature.SignerCertificate -or $signature.SignerCertificate.Thumbprint -ne $certificate.Thumbprint) {
            throw "Wrong/missing signer: $name"
        }
        if ($signature.Status -ne 'Valid' -and $signature.StatusMessage -notmatch 'terminated in a root certificate which is not trusted') {
            throw "Invalid signature: $name ($($signature.Status))"
        }
        $signatures += [ordered]@{file=$name;status=[string]$signature.Status;message=$signature.StatusMessage}
    }
    $metadata = [ordered]@{
        schema=1; status='self-signed development test only; not Microsoft signed or hardware tested'
        source_commit='b20bcc186fc758065a0ef312303d1949ff796813'
        unsigned_sys_sha256=$originalSysHash
        certificate_subject=$certificate.Subject
        certificate_thumbprint_sha1=$certificate.Thumbprint
        certificate_sha256=(Get-FileHash -LiteralPath (Join-Path $OutputDirectory 'Phoebus4eva-Test.cer') -Algorithm SHA256).Hash.ToLowerInvariant()
        not_before_utc=$certificate.NotBefore.ToUniversalTime().ToString('o')
        not_after_utc=$certificate.NotAfter.ToUniversalTime().ToString('o')
        timestamped=$false; tool_kit='10.0.26100.6584'; catalog_os_checks=@('10_CO_X64','10_NI_X64','10_GE_X64')
        separate_26h2_check=$false; hardware_tested=$false
        system_trust_changed=$false; boot_settings_changed=$false; driver_installed=$false
        authenticode_observations=$signatures
    }
    $metadata | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath (Join-Path $OutputDirectory 'test-signing.json') -Encoding UTF8
    if ((Get-FileHash -LiteralPath $sysSource -Algorithm SHA256).Hash.ToLowerInvariant() -ne $originalSysHash) {
        throw 'Original unsigned binary changed.'
    }
    Write-Output "Created development test package: $OutputDirectory"
    Write-Output "Public certificate SHA-1 thumbprint: $($certificate.Thumbprint)"
    Write-Output 'Self-signed certificate is intentionally not trusted here. Normal Windows installation is not supported.'
} finally {
    # Exact freshly created file only; no recursive deletion or shared-key cleanup.
    if (Test-Path -LiteralPath $pfxPath) { Remove-Item -LiteralPath $pfxPath -Force }
    if ($certificate) { $certificate.Dispose() }
    if ($rsa) { $rsa.Dispose() }
    $pfxPassword = $null
    if ((Test-Path -LiteralPath $privateDirectory) -and !(Get-ChildItem -LiteralPath $privateDirectory -Force)) {
        [IO.Directory]::Delete($privateDirectory,$false)
    }
}
