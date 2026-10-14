# Manual native build

Use x64 Windows, Python 3.11+ and Windows PowerShell 5.1+. Inspect the source, dependency lock and scripts before running them. No card-vendor installation files or physical card are required for compiling.

## Commands

~~~powershell
python .\scripts\setup-native-toolchain.py
python .\scripts\setup-native-toolchain.py --verify-only
powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\build-native-driver.ps1 -Target Tests
powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\build-native-driver.ps1 -Target Driver
# Or both:
powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\build-native-driver.ps1 -Target All
~~~

Pins: MSVC compiler 19.44.35229/tool directory 14.44.35207; SDK/WDK NuGet 10.0.26100.6584; kit directory 10.0.26100.0; minimum API NTDDI_WIN10_CO. The eight exact URLs, lengths and hashes are in scripts/native-toolchain.lock.json. About 430 MB is downloaded; extracted files need more space.

Setup uses standard-library HTTPS, verifies archives before extraction and rejects escaping member paths. Default tools stay under vendor/native-tools, with no global PATH changes or system/driver installation. Verify-only checks cached archives, not every extracted file. Custom paths use setup --root and build -ToolchainRoot. Microsoft's component licenses/notices apply; do not redistribute the tool directory.

The build script shows all compiler/linker flags and explicit libraries. /kernel, C++17, no RTTI/exceptions, stack cookies, Spectre mitigations, CFG and /W4 /WX are enabled. GsDriverEntry provides the WDK stack-cookie initialization before DriverEntry. No build target signs or installs the driver.

Outputs include dist/driver/PhoebusW11.sys, INF, PDB/map/object files and host tests. InfVerif /w /v and Inf2Cat /nocat are mandatory checks for the pinned tool's 21H2/22H2/24H2 identifiers. There is no distinct 26H2 check in this kit, no generated catalog and no hardware acceptance claim.

## Inspect output and tools

~~~powershell
& .\vendor\native-tools\msvc\Contents\VC\Tools\MSVC\14.44.35207\bin\Hostx64\x64\dumpbin.exe /headers .\dist\driver\PhoebusW11.sys
& .\vendor\native-tools\msvc\Contents\VC\Tools\MSVC\14.44.35207\bin\Hostx64\x64\dumpbin.exe /imports .\dist\driver\PhoebusW11.sys
Get-FileHash .\dist\driver\PhoebusW11.sys -Algorithm SHA256
Get-AuthenticodeSignature .\vendor\native-tools\msvc\Contents\VC\Tools\MSVC\14.44.35207\bin\Hostx64\x64\cl.exe
~~~

Compiler timestamps/debug paths can vary across builds; binary identity is not promised. Source, tool pins and runtime properties need separate review. Compile does not establish audible output or kernel safety.

## Package after review

~~~powershell
# Replace the placeholder with the full public source commit:
python .\scripts\package-development.py --source-commit <full-40-character-source-commit>
~~~

Packaging includes only scripts/publication-files.json's public file set, the project's SYS/INF and read-only diagnostic executable. It writes source/binary ZIPs, checksums and source/version metadata, without upload or installation.

[Official WDK build guidance](https://learn.microsoft.com/en-us/windows-hardware/drivers/develop/building-a-driver), [WDK NuGet](https://learn.microsoft.com/en-us/windows-hardware/drivers/install-the-wdk-using-nuget), [kit versioning](https://learn.microsoft.com/en-us/windows-hardware/drivers/download-the-wdk#kit-versioning).
