#pragma once
#include "ph_types.h"

// Pure arithmetic shared by the kernel stream and host tests. State belongs to
// one stream; querying another stream cannot alter this stream's pause cursor.
namespace phoebus {
enum class StreamState : uint8_t { Stop, Acquire, Pause, Run };
struct PositionTracker {
    StreamState state = StreamState::Stop;
    uint32_t frozen = 0;

    bool transition(StreamState next, uint32_t dma, uint32_t bytes) {
        if (!bytes && next == StreamState::Run) return false;
        if (next == StreamState::Stop) frozen = 0;
        else if (state == StreamState::Run && next != StreamState::Run)
            frozen = bytes ? dma % bytes : 0;
        state = next;
        return true;
    }
    bool snapshot(uint32_t dma, uint32_t bytes, uint32_t fifo,
                  uint64_t& play, uint64_t& write) const {
        play = write = 0;
        if (!bytes) return false;
        if (state == StreamState::Run) play = dma % bytes;
        else play = frozen % bytes;
        write = (play + static_cast<uint64_t>(fifo)) % bytes;
        return true;
    }
};
}
