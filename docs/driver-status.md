# Driver status

Version0.1.0.0 is an experimental x64 stereo PCM implementation. No physical Phoebus or kernel execution was available during local validation. Audio, receiver lock, game fixes and runtime safety are unverified.

Implemented: exact HDA/subsystem checks, documented busV2 interface, bounded direct-route discovery, stereo WaveRT/topology endpoints, real bus DMA/events, capability-based formats, frame alignment, per-stream positions, explicit state transitions, serialized shared-converter ownership and removal/close handling. The kernel source is driver/phoebus.cpp.

Missing: complete mixer/selector graphs, multichannel/capture, external amplifier/GPIO validation, hardware volume/gain/jack controls, encoded passthrough/DDL/DTS, measured codec/link latency, signing and target runtime qualification. Unsupported graphs are skipped rather than guessed.

Removal uses separate engine-allocation and client-buffer lifetimes under serialized operations. Microsoft's modern removal guidance and older FreeDmaEngine reference differ concerning attached buffers. The target bus behaviour and concurrent close need testing. Failed cleanup retains ownership in the adapter registry; failed event-unregistration still needs a tested recovery guarantee and may leave resources retained.

The README describes implemented feature/rate scope, lock order, script effects, build pins and limits. The published binary remains unsigned. It is downloadable for audit and qualified development, not a normal-installation release. No kernel implementation change was made for publication.
