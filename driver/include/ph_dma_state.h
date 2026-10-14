// SPDX-License-Identifier: MIT
// Original hardware-state planner; no Windows dependencies or hardware I/O.
// HD Audio SetDmaEngineState requires Pause between Reset and Run.
#ifndef PH_DMA_STATE_H
#define PH_DMA_STATE_H
#include "ph_types.h"

namespace phoebus {
enum class DmaState : uint8_t { Reset = 0, Pause = 1, Run = 2 };

class DmaStatePlanner {
    DmaState state_ = DmaState::Reset;
public:
    DmaState state() const { return state_; }

    // The caller serializes transitions and owns the DMA handle/buffer.
    // Apply receives a target hardware state and returns an NTSTATUS-compatible
    // signed status. Success/information are >=0; warning/error are <0.
    template<class Apply>
    int32_t transition(DmaState next, bool buffer_present, Apply apply) {
        if (next != DmaState::Reset && next != DmaState::Pause && next != DmaState::Run)
            return static_cast<int32_t>(0xc000000du); // STATUS_INVALID_PARAMETER.
        const bool needs_pause =
            (next == DmaState::Run && state_ == DmaState::Reset) ||
            (next == DmaState::Reset && state_ == DmaState::Run);
        // Even a Reset target may need a non-reset intermediate step.
        if (!buffer_present && (next != DmaState::Reset || needs_pause))
            return static_cast<int32_t>(0xc0000010u); // STATUS_INVALID_DEVICE_REQUEST.
        if (next == state_) return 0;
        if (needs_pause) {
            const int32_t status = static_cast<int32_t>(apply(DmaState::Pause));
            if (status < 0) return status;
            state_ = DmaState::Pause;
        }
        const int32_t status = static_cast<int32_t>(apply(next));
        if (status >= 0) state_ = next;
        return status;
    }
};
}
#endif
