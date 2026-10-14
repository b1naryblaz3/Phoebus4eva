# Validation record

Local source validation on14October2026:

- Microsoft MSVC/SDK/WDK compilation/linkage with /W4 /WX passed.
- 9,315 protocol/format checks,34 DMA ordering/failure checks and cyclic-position tests passed.
- InfVerif /w /v reported a valid INF.
- Inf2Cat /nocat reported no errors/warnings for the pinned kit's Windows11 21H2/22H2/24H2 identifiers. No separate26H2 check is claimed.
- SYS is x64 Native with stack-cookie support, ASLR, NX, CFG and integrity flags. PE imports are ntoskrnl.exe, HAL.dll and portcls.sys. Static Microsoft support code is also linked.
- Focused static review examined locks, registry traversal, event lifetime and removal/close handling. Definite issues found were corrected. This is not a complete security audit.

The initial downloaded kernel image has SHA-256 ec72a75a0a42a7520a742c26d958efecbf84b06012c87250588262ea01e8f2c2, size33,792bytes. Release metadata and checksums describe the exact asset published. Build timestamps/debug paths can change identity on self compilation. The image may contain an embedded local debug-path reference; PDB/maps and personal machine reports are not included in preview assets.

No physical Phoebus, kernel load, audible output, receiver lock, game fix, power/resume, removal stress, Driver Verifier or HLK result has been observed by this project. No signed catalog or Microsoft signing submission is supplied. Source/package checks establish different properties from runtime correctness. Actual [GitHub Actions runs](https://github.com/b1naryblaz3/Phoebus4eva/actions) are the authority for CI outcomes; a prepared workflow alone is not a passing run.

The diagnostics executable was compiled and exercised against Windows CoreAudio without installing this driver. That exercises its query path, not Phoebus compatibility. Diagnostics read configuration and write local reports without playback/recording or setting changes.
