// SPDX-License-Identifier: MIT
// Failure-injection tests for state/call ordering, not real DMA/performance.
#include "../driver/include/ph_dma_state.h"
#include <stdio.h>
#include <stdlib.h>

static unsigned checks;
static void check(bool condition, const char* message) {
    ++checks;
    if (!condition) { fprintf(stderr, "FAIL: %s\n", message); exit(1); }
}
using phoebus::DmaState;
using phoebus::DmaStatePlanner;

struct FakeBus {
    DmaState states[16]{};
    unsigned calls = 0;
    unsigned fail_on = 0;
    int32_t success = 0;
    int32_t failure = static_cast<int32_t>(0xc0000185u);
    int32_t apply(DmaState state) {
        check(calls < 16, "bounded fake call history");
        states[calls++] = state;
        return calls == fail_on ? failure : success;
    }
    void clear() { calls = fail_on = 0; success = 0; }
};

int main() {
    DmaStatePlanner planner;
    FakeBus bus;
    auto apply = [&bus](DmaState state) { return bus.apply(state); };
    check(planner.state() == DmaState::Reset, "initial hardware reset");
    check(planner.transition(DmaState::Reset, false, apply) == 0 && !bus.calls,
        "reset without buffer is already satisfied");
    check(planner.transition(DmaState::Run, false, apply) < 0 && !bus.calls &&
        planner.state() == DmaState::Reset, "cannot run without buffer");
    check(planner.transition(DmaState::Pause, false, apply) < 0 && !bus.calls,
        "cannot pause without buffer");
    check(planner.transition(static_cast<DmaState>(3), true, apply) < 0 && !bus.calls,
        "reject invalid target state");

    check(planner.transition(DmaState::Run, true, apply) == 0 && bus.calls == 2 &&
        bus.states[0] == DmaState::Pause && bus.states[1] == DmaState::Run &&
        planner.state() == DmaState::Run, "reset to run passes through pause");
    bus.clear();
    check(planner.transition(DmaState::Run, true, apply) == 0 && !bus.calls,
        "same state does not program hardware again");
    check(planner.transition(DmaState::Run, false, apply) < 0 && !bus.calls,
        "same non-reset state still requires buffer");
    check(planner.transition(DmaState::Reset, false, apply) < 0 && !bus.calls &&
        planner.state() == DmaState::Run, "cannot perform intermediate pause without buffer");
    check(planner.transition(DmaState::Reset, true, apply) == 0 && bus.calls == 2 &&
        bus.states[0] == DmaState::Pause && bus.states[1] == DmaState::Reset &&
        planner.state() == DmaState::Reset, "run to reset passes through pause");

    bus.clear(); bus.fail_on = 1;
    check(planner.transition(DmaState::Run, true, apply) == bus.failure && bus.calls == 1 &&
        planner.state() == DmaState::Reset, "failed intermediate pause retains reset");
    bus.clear(); bus.fail_on = 2;
    check(planner.transition(DmaState::Run, true, apply) == bus.failure && bus.calls == 2 &&
        planner.state() == DmaState::Pause, "failed final run retains successful pause");
    bus.clear();
    check(planner.transition(DmaState::Run, true, apply) == 0 && bus.calls == 1 &&
        bus.states[0] == DmaState::Run, "retry final run without redundant pause");

    bus.clear(); bus.fail_on = 1;
    check(planner.transition(DmaState::Reset, true, apply) == bus.failure && bus.calls == 1 &&
        planner.state() == DmaState::Run, "failed pause while stopping retains run");
    bus.clear(); bus.fail_on = 2;
    check(planner.transition(DmaState::Reset, true, apply) == bus.failure && bus.calls == 2 &&
        planner.state() == DmaState::Pause, "failed reset retains successful pause");
    bus.clear();
    check(planner.transition(DmaState::Reset, true, apply) == 0 && bus.calls == 1 &&
        bus.states[0] == DmaState::Reset, "retry reset from known pause");

    bus.clear(); bus.success = 1; // Successful informational status.
    check(planner.transition(DmaState::Pause, true, apply) == 1 &&
        planner.state() == DmaState::Pause, "positive status records success");
    bus.clear(); bus.fail_on = 1;
    bus.failure = static_cast<int32_t>(0x80000005u); // Warning is not NT_SUCCESS.
    check(planner.transition(DmaState::Run, true, apply) == bus.failure &&
        planner.state() == DmaState::Pause, "negative warning cannot advance state");
    bus.clear();
    check(planner.transition(DmaState::Reset, false, apply) == 0 && bus.calls == 1 &&
        bus.states[0] == DmaState::Reset, "pause to reset is valid without buffer");
    printf("PASS: %u DMA transition ordering/failure checks\n", checks);
    return 0;
}
