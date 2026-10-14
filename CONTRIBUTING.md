# Contributing

Keep changes narrow, explain their behaviour and give actual validation plus gaps. Hardware output, receiver lock and game fixes need observed results; host logic tests and compilation do not establish those outcomes.

Use original code or compatible openly licensed dependencies with notices/provenance. Do not submit vendor binaries, effects/encoder libraries, copied proprietary source, undocumented firmware payloads, credentials or personal reports. Contributions to original project files are offered under the MIT license.

Preserve exact hardware matches, IRQL/callback contracts and security support. Review initialization failures and concurrent teardown. Avoid automatic trust/boot-policy changes, broad device IDs and guessed rate/gain controls. Add meaningful tests for changed protocol/state logic and run build/package checks.

Hardware reports should include OS/game versions, output path, receiver, format, baseline and reproducible steps. Redact identifying device/endpoint data where possible. Distinguish observations from explanations.
