#include "mcb_producer_ring.h"

#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>

/*
 * The reference model must link and behave identically from C++.  The ring
 * size below is a synthesis counterexample input, not an established capacity
 * of the shared control ring.
 */
#define SYNTHETIC_RING_SIZE 395u

int main()
{
    mcb_producer_state state{};
    mcb_producer_write write{};

    assert(mcb_producer_describe(SYNTHETIC_RING_SIZE, 62, 0, &state) == MCB_PRODUCER_OK);
    assert(state.free_distance == 333);
    assert(state.max_admitted_message == 327);
    assert(state.collapse_message_length == 327);

    assert(mcb_producer_plan_write(SYNTHETIC_RING_SIZE, 0, 0, 56, &write) == MCB_PRODUCER_OK);
    assert(write.ring_extent == 62);
    assert(write.new_producer == 62);

    assert(mcb_producer_plan_write(SYNTHETIC_RING_SIZE, 62, 0, 327, &write) == MCB_PRODUCER_OK);
    assert(write.collapses == 1);
    assert(write.clobbers_unread == 0);

    assert(mcb_producer_plan_write(SYNTHETIC_RING_SIZE, 0, 30, 56, &write) == MCB_PRODUCER_REFUSED);

    assert(mcb_producer_ring_advance(58) == 62);

    // The advance overflow boundary is visible from C++ as well: every valid
    // advance is at least 4 and zero is the failure marker.
    assert(mcb_producer_ring_advance(SIZE_MAX - 4) == SIZE_MAX);
    assert(mcb_producer_ring_advance(SIZE_MAX - 3) == 0);
    assert(mcb_producer_ring_advance(SIZE_MAX) == 0);
    assert(mcb_producer_ring_advance(0) == 4);

    return 0;
}
