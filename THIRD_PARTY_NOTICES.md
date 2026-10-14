# License and dependency boundaries

Original project files are MIT-licensed under LICENSE. That license does not replace third-party licenses or Windows platform terms.

The build obtains pinned Microsoft MSVC components and SDK/WDK headers, import/support libraries and validators. Origins/digests are in scripts/native-toolchain.lock.json. The archives/tools are not bundled in the repository or source release. Read notices/licenses in the downloaded packages and the applicable [Microsoft Build Tools terms](https://visualstudio.microsoft.com/license-terms/vs2022-ga-diagnosticbuildtools/).

The driver links Windows kernel/PortCls/KS/HAL import libraries and BufferOverflowFastFailK stack-cookie support. The compiled image is not claimed to contain only original MIT code. Windows supplies runtime APIs under its own terms. Host tests link Microsoft's static user-mode CRT but those executables are not preview assets. Diagnostics use Windows .NET Framework and CoreAudio/COM.

Public Python scripts use the standard library; Python is not bundled. GitHub workflows use separately licensed actions/checkout, actions/upload-artifact and hosted Windows runners.

No ASUS, C-Media or Dolby binary/library/installer/codec implementation is bundled or linked. Hardware identifiers and API/specification references support interoperability and review. This dependency disclosure is not a legal clearance certification or a claim that every Windows component is open source.
