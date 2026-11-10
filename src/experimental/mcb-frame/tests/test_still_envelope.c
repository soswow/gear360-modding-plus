#include "mcb_frame.h"
#include "mcb_ring.h"
#include "still_request.h"

#include <assert.h>
#include <stdint.h>
#include <string.h>

/* The reviewed default ring capacity in bytes. */
#define TEST_RING_CAPACITY ((size_t)395u)

/* Fixed vector from the retained 22 October 2026 software-submit output.
 * The parent verified the raw output against its run-manifest hash. These
 * bytes start at category; the driver's two-byte route is outside this frame.
 * This checks encoding only, not acquisition or completion.
 */
static const uint8_t observed_outgoing[56] = {
    0x83, 0x00, 0x01, 0x02, 0x30, 0x00, 0x16, 0x00,
    0x01, 0x01, 0x01, 0x00, 0x00, 0x00, 0x01, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0xd6, 0x09, 0x00, 0x00, 0x00, 0x00,
    0x36, 0xca, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
};

static void test_observed_request(void)
{
    still_request_options options;
    still_request_storage_info storage;
    uint8_t body[STILL_REQUEST_PAYLOAD_SIZE];
    uint8_t envelope[STILL_REQUEST_PAYLOAD_SIZE + MCB_FRAME_TX_MIN_SIZE];
    size_t envelope_size = 0;

    still_request_options_device_shutter(&options);
    storage.remaining_count = 2518u;
    storage.free_space_bytes = UINT64_C(7687503872);

    assert(still_request_encode_ex(body, sizeof(body), &storage, &options) == 0);
    assert(mcb_frame_encode_linux(UINT16_C(0x83), UINT16_C(0x201),
                                  body, sizeof(body), envelope, sizeof(envelope),
                                  &envelope_size) == MCB_FRAME_OK);
    assert(envelope_size == sizeof(observed_outgoing));
    assert(memcmp(envelope, observed_outgoing, sizeof(observed_outgoing)) == 0);
}

/* Synthetic storage values; envelope from submit.forty-eight-byte-request. */
int main(void)
{
    test_observed_request();
    static const uint8_t expected[] = {
        0x83, 0x00, 0x01, 0x02, 0x30, 0x00, /* category, attribute, body size */
        0x01, 0x00, 0x01, 0x01,             /* 0x01010001 */
        0x01, 0x00, 0x00, 0x00,
        0x01, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x44, 0x33, 0x22, 0x11,             /* remaining count */
        0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88, /* free bytes */
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00                          /* trailer */
    };
    const still_request_storage_info storage = {
        UINT32_C(0x11223344), UINT64_C(0x8877665544332211)
    };
    uint8_t body[STILL_REQUEST_PAYLOAD_SIZE];
    uint8_t envelope[STILL_REQUEST_PAYLOAD_SIZE + MCB_FRAME_TX_MIN_SIZE];
    size_t envelope_size = 0;
    unsigned char snapshot[sizeof(mcb_ring_admission)];
    mcb_ring_admission plan;
    size_t required;

    assert(still_request_encode(body, sizeof(body), &storage) == 0);
    assert(mcb_frame_encode_linux(UINT16_C(0x83), UINT16_C(0x201),
                                  body, sizeof(body), envelope, sizeof(envelope),
                                  &envelope_size) == MCB_FRAME_OK);
    assert(envelope_size == sizeof(expected));
    assert(memcmp(envelope, expected, sizeof(expected)) == 0);

    /*
     * Compose the outgoing envelope with the ring admission planner.  The
     * packet extent is derived from the encoder result above, not from a
     * constant, so this checks the boundary between the outgoing frame helper
     * and the transport planner.  Nothing here touches a device.
     */
    required = envelope_size + MCB_RING_OUTER_OVERHEAD;

    /* Empty ring: the packet occupies one contiguous range from index 0. */
    memset(&plan, 0x5a, sizeof(plan));
    assert(mcb_ring_plan_admission(TEST_RING_CAPACITY, 0, 0, envelope_size,
                                   &plan) == MCB_RING_ADMISSION_OK);
    assert(plan.total_bytes == required);
    assert(plan.start_index == 0);
    assert(plan.first_length == required);
    assert(plan.second_length == 0);
    assert(plan.new_producer == required);
    assert(plan.new_producer < TEST_RING_CAPACITY);
    /* The encoded frame is contained in the admitted range. */
    assert(plan.first_length >= envelope_size);

    /*
     * Exact free space: a ring of exactly the packet extent leaves the packet
     * filling the whole free area, which is refused.  The plan is unchanged.
     */
    memset(&plan, 0x5a, sizeof(plan));
    memcpy(snapshot, &plan, sizeof(snapshot));
    assert(mcb_ring_plan_admission(required, 0, 0, envelope_size, &plan) ==
           MCB_RING_ADMISSION_INSUFFICIENT_SPACE);
    assert(memcmp(&plan, snapshot, sizeof(snapshot)) == 0);

    /* One byte more: the packet fits and one byte of slack remains. */
    memset(&plan, 0x5a, sizeof(plan));
    assert(mcb_ring_plan_admission(required + 1u, 0, 0, envelope_size, &plan) ==
           MCB_RING_ADMISSION_OK);
    assert(plan.total_bytes == required);
    assert(plan.start_index == 0);
    assert(plan.first_length == required);
    assert(plan.second_length == 0);
    assert(plan.new_producer == required);
    assert(required + 1u - plan.new_producer == 1u);

    /*
     * The same extent one byte short of the ring end: the packet wraps into
     * two ranges and still leaves one byte of slack.
     */
    memset(&plan, 0x5a, sizeof(plan));
    assert(mcb_ring_plan_admission(TEST_RING_CAPACITY,
                                   TEST_RING_CAPACITY - required + 1u, 2,
                                   envelope_size, &plan) ==
           MCB_RING_ADMISSION_OK);
    assert(plan.total_bytes == required);
    assert(plan.start_index == TEST_RING_CAPACITY - required + 1u);
    assert(plan.first_length == required - 1u);
    assert(plan.second_length == 1u);
    assert(plan.start_index + plan.first_length == TEST_RING_CAPACITY);
    assert(plan.new_producer == 1u);

    /*
     * The same extent ending exactly on the last ring byte: one range, and
     * the producer wraps back to zero.
     */
    memset(&plan, 0x5a, sizeof(plan));
    assert(mcb_ring_plan_admission(TEST_RING_CAPACITY,
                                   TEST_RING_CAPACITY - required, 1,
                                   envelope_size, &plan) ==
           MCB_RING_ADMISSION_OK);
    assert(plan.total_bytes == required);
    assert(plan.start_index == TEST_RING_CAPACITY - required);
    assert(plan.first_length == required);
    assert(plan.second_length == 0);
    assert(plan.start_index + plan.first_length == TEST_RING_CAPACITY);
    assert(plan.new_producer == 0);
    return 0;
}
