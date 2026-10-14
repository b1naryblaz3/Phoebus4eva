#include "../driver/include/ph_position.h"
#include <stdio.h>
#include <stdlib.h>
static void check(bool ok, const char* text) {
    if (!ok) { fprintf(stderr, "FAIL: %s\n", text); exit(1); }
}
int main() {
    using namespace phoebus;
    PositionTracker a, b;
    uint64_t p, w;
    check(!a.transition(StreamState::Run, 0, 0), "no zero-sized running buffer");
    check(!a.snapshot(1, 0, 4, p, w) && p == 0 && w == 0, "no division by zero");
    check(a.transition(StreamState::Run, 0, 4096), "start a");
    check(b.transition(StreamState::Run, 0, 4096), "start b");
    check(a.snapshot(4000, 4096, 256, p, w) && p == 4000 && w == 160, "FIFO wrap");
    check(a.transition(StreamState::Pause, 4072, 4096), "pause a");
    check(b.transition(StreamState::Pause, 1024, 4096), "pause b");
    check(b.snapshot(3333, 4096, 64, p, w) && p == 1024, "b holds its cursor");
    check(a.snapshot(2222, 4096, 64, p, w) && p == 4072 && w == 40, "a independent of b");
    check(a.transition(StreamState::Run, 4072, 4096), "resume a");
    check(a.snapshot(8196, 4096, 0, p, w) && p == 4 && w == 4, "cyclic cursor");
    check(a.transition(StreamState::Stop, 4, 4096), "stop a");
    check(a.snapshot(2000, 4096, 0, p, w) && p == 0 && w == 0, "stop resets cursor");
    check(b.snapshot(0, 4096, UINT32_MAX, p, w) && p == 1024 && w == 1023, "wide addition");
    puts("position_tests: PASS");
    return 0;
}
