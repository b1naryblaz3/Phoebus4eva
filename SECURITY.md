# Safety review and reporting

This is an unsigned developer preview. No physical Phoebus playback, kernel runtime, game fix or receiver lock has been validated. It must not be represented as a certified or production-safe driver.

Review buffer/engine ownership, event storage lifetime, IRQL, codec responses, node/format bounds, partial initialization, failure unwind, lock order and concurrent close/power/removal. The modern engine/client-buffer lifetime split and failed event-unregistration recovery need hardware testing. See the README's audit map and known boundaries.

No script installs a driver, changes boot security, trusts a certificate or runs vendor installers. Setup downloads pinned Microsoft components; builds execute compiler/linker/tests/validators. Verification hashes files and is not a signature or safety certification.

Report non-sensitive correctness issues in GitHub Issues with source commit, OS build, matching hardware, reproducible steps and observed behaviour. Do not attach credentials, unredacted reports, private data or vendor files. For suspected exploitable defects, use private reporting if available; otherwise ask for a private contact in a minimal issue without posting exploit details. No response-time commitment is claimed.

A normal-installation release requires hardware qualification, applicable signing, power/removal stress and a tested recovery procedure. Compilation, scans, licensing and signatures answer different questions; none replaces runtime review.
