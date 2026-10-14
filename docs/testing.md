# Hardware qualification plan

Use a recoverable development machine with the exact Phoebus HDA identity, alternate audio and an appropriate signed driver-testing environment. The preview itself does not configure that environment or bypass Windows security. No tests below have been claimed to pass on hardware.

## Before a future installation

Record OS build, hardware/device stack, installed published INF/version, receiver model and existing symptoms. Preserve the working package and recovery route. Read signing/install limits in README. The read-only collector can gather baseline metadata; redact reports before posting.

## Required tests

| Area | Exercise | Record |
| --- | --- | --- |
| Startup | Exact-match binding, no-card/unsupported graph, partial registration failures | Endpoint count, identities, NTSTATUS/errors |
| Analogue | Speakers/headphones, left/right identity, mute/volume and output levels | Actual audio, distortion, gain and external amp behaviour |
| Optical PCM | Each advertised rate/precision at a supported receiver | Reported rate, lock, dropouts and channel identity |
| Buffering | Small/large buffers, one/two notifications, shared/exclusive modes | Actual interrupts, cyclic cursors, underruns and timing |
| Concurrency | Endpoint conflicts, rapid open/close, close during removal | Ownership, deadlocks, retained resources and late callbacks |
| Power | Sleep, hibernate, resume, app switching | Restoration, pops, endpoint availability and register safety |
| Games | Same Worms Armageddon replay and WoW scene before/after change | Versions/edition, format/output/settings, first glitch and counts |
| Failures | Allocation, codec timeout, reset/unregister/free failures | Correct unwind and safe caller-event lifetimes |
| Kernel qualification | Appropriate Driver Verifier/HLK and stress on a test machine | Findings, crash analysis, fixes and repeat results |
| Deployment | Correct signing/package/OS checks and tested rollback | Acceptance, precise new INF name and recovery result |

Do not equate format-query support with audible output or receiver compatibility. Change one variable at a time and restart applications where necessary. Keep 44.1kHz and48kHz families separate in comparisons, then test higher rates only when receiver and codec support them. PCM, compressed movie passthrough and realtime surround encoding are separate features; this preview implements only stereo PCM.

Driver Verifier can intentionally expose faults and crash a test machine. Configure it only as part of qualified development with recovery available. Host protocol/state tests cannot replace these runtime tests.

Use Device Manager rollback or the recorded known-good package when recovering a future signed test installation. Never delete a guessed driver-store INF or force-remove unrelated devices. Keep sensitive machine IDs and user data out of public issue reports.
