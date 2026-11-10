#ifndef MCB_RING_H
#define MCB_RING_H

#include "mcb_frame.h"

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Pure admission planner for one already encoded outgoing Linux control frame
 * entering the reviewed circular byte transport.
 *
 * This header plans only.  It never copies packet bytes, never reserves ring
 * space, never takes a lock and never touches a device.
 *
 * Ring model (caller supplied, bytes):
 *
 *   capacity   N, the ring size in bytes, N >= 1
 *   producer   W, the next write index, W in [0, N)
 *   consumer   R, the next read index,  R in [0, N)
 *
 *   used  = (W + N - R) mod N
 *   free  = N - used
 *
 * Full and empty share equal cursors: W == R cannot express "N bytes
 * occupied".  The planner resolves that ambiguity by keeping one byte of
 * slack, so a ring driven only by this planner never reaches the ambiguous
 * state.  See the caller obligations below.
 *
 * Outer overhead is exactly 6 bytes: a 4-byte length header plus 2 bytes of
 * transport overhead.  The encoded frame itself is 8 through 388 bytes and
 * needs no even length.
 */

#define MCB_RING_OUTER_OVERHEAD ((size_t)6u)

typedef enum mcb_ring_admission_status {
    MCB_RING_ADMISSION_OK = 0,
    MCB_RING_ADMISSION_INVALID_ARGUMENT = 1,
    MCB_RING_ADMISSION_INVALID_CAPACITY = 2,
    MCB_RING_ADMISSION_INVALID_CURSOR = 3,
    MCB_RING_ADMISSION_INVALID_LENGTH = 4,
    MCB_RING_ADMISSION_INSUFFICIENT_SPACE = 5
} mcb_ring_admission_status;

typedef struct mcb_ring_admission {
    /* frame_size + MCB_RING_OUTER_OVERHEAD: bytes the packet occupies. */
    size_t total_bytes;
    /* First ring index the packet is written to: the producer cursor. */
    size_t start_index;
    /* Bytes written from start_index up to the end of the ring. */
    size_t first_length;
    /* Bytes written after wrapping to index 0.  Zero when there is no wrap. */
    size_t second_length;
    /* Producer cursor after the packet: (start_index + total_bytes) mod N. */
    size_t new_producer;
} mcb_ring_admission;

/*
 * Plan the admission of one encoded frame into the ring.
 *
 * frame_size is the size in bytes of the already encoded outgoing frame and
 * must be within MCB_FRAME_TX_MIN_SIZE .. MCB_FRAME_TX_MAX_SIZE (8..388).
 * The frame bytes are not inspected and are not copied; only the size
 * matters.
 *
 * On MCB_RING_ADMISSION_OK the plan describes at most two ring ranges,
 * [start_index, start_index + first_length) and [0, second_length), holding
 * total_bytes == first_length + second_length bytes.  The caller writes the
 * packet bytes and the transport's own header and overhead bytes into those
 * ranges and then publishes new_producer.
 *
 * Admission requires total_bytes < free.  Equality is refused: consuming the
 * whole free area would leave the ring with W == R, which is
 * indistinguishable from empty.  Refusing it keeps one byte of slack.
 *
 * On failure the plan object is left unchanged and a non-OK status is
 * returned.  Checks are applied in this order:
 * out_plan null, capacity zero, producer or consumer outside [0, capacity),
 * frame size outside 8..388, then insufficient space.
 *
 * Caller obligations:
 *
 *   - The caller must serialize the plan, the byte copy and the publication
 *     of new_producer.  This function performs no synchronization and no
 *     atomic reservation, so a concurrent producer or consumer can invalidate
 *     the plan between the call and the copy.
 *   - The caller must treat W == R as "empty".  The planner assumes that the
 *     ring is never filled to capacity, and the one byte of slack keeps that
 *     true for any caller that admits only through this planner.
 *   - The caller must not change producer or consumer between the call and
 *     the copy, and must not admit another packet in between.
 *   - The caller owns the packet bytes and the transport framing.  This
 *     planner does not define header, trailer or overhead contents.
 *   - capacity, producer and consumer must describe the same ring.
 */
mcb_ring_admission_status mcb_ring_plan_admission(size_t capacity,
                                                  size_t producer,
                                                  size_t consumer,
                                                  size_t frame_size,
                                                  mcb_ring_admission *out_plan);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* MCB_RING_H */
