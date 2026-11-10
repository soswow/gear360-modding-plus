#ifndef MCB_PRODUCER_RING_H
#define MCB_PRODUCER_RING_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Producer-side ring arithmetic of the stock RTOS MCB writer.
 *
 * THIS IS A REFERENCE MODEL OF STOCK BEHAVIOUR, NOT A SAFE ADMISSION CHECK AND
 * NOT A TRANSPORT.  It reproduces the writer as it is, including the cases
 * that are unsafe for a receiver.  Use it to reason about what the stock
 * producer does to the shared control ring.  Do not use it to decide whether a
 * write is safe, and do not use it to receive anything.
 *
 * This header models one thing only: the ring behaviour that the stock RTOS
 * producer applies to the shared control ring that our Linux receiver reads.
 * The rules are transcribed from the pinned RTOS image, not guessed:
 *
 *   FUN_800377dc  free-distance computation
 *       returns 0 when either cursor is out of range,
 *       (R + N) - W when R <= W, and R - W when R > W.
 *
 *   FUN_80037d6c  the packet writer
 *       admits a message of M bytes only when M + 5 < free,
 *       copies a six-byte outer header and M message bytes into the ring,
 *       and publishes the producer cursor as (W + M + 6) mod N.
 *
 *   FUN_80069d24  the ordinary bridge sender
 *       refuses any message longer than 395 bytes before the writer is
 *       reached.  That 395 is a ceiling on one bridge message.  It is not an
 *       established capacity of the shared control ring; the ring size below
 *       is a model input, and no live capacity is established anywhere here.
 *
 * The model exists so that a receive contract can be stated against the real
 * producer instead of an assumed one.  It performs no I/O, takes no lock,
 * copies nothing, reserves nothing and touches no device.  It establishes no
 * cross-CPU visibility, no sole receiver ownership, no freshness, no request
 * association and no sensor provenance.  It is not a transport.
 *
 * Ring model (caller supplied, bytes):
 *
 *   capacity   N, the ring size in bytes, N >= 1
 *   producer   W, the producer's published write cursor, W in [0, N)
 *   consumer   R, the consumer's published read cursor,  R in [0, N)
 *
 * N is a parameter of the model.  The live capacity of the shared control ring
 * is not established; N = 395 appears in the tests only as a synthesis
 * counterexample input.
 *
 * The producer reads R from the shared word the Linux consumer writes after it
 * has copied the bytes out.  Whether that cursor is truthful is NOT knowable
 * from this model.  producer == consumer is ambiguous between an empty ring
 * and a full ring, and the two are distinguishable only under an externally
 * maintained no-full invariant, which this model neither holds nor checks.
 *
 * Consequently mcb_producer_write.clobbers_unread reports the overlap of the
 * written byte intervals with the interval [R, W) taken from the two cursor
 * values passed in.  That is an arithmetic overlap against an assumed
 * interval.  It does not establish that the consumer cursor was truthful, it
 * cannot detect a stale cursor, and it is not a live guarantee.
 */

#define MCB_PRODUCER_OUTER_HEADER ((size_t)6u)

/* The ordinary bridge sender's ceiling on one message. */
#define MCB_PRODUCER_MAX_BRIDGE_MESSAGE ((size_t)395u)

typedef enum mcb_producer_status {
    MCB_PRODUCER_OK = 0,
    MCB_PRODUCER_INVALID_ARGUMENT = 1,
    MCB_PRODUCER_INVALID_CAPACITY = 2,
    MCB_PRODUCER_INVALID_CURSOR = 3,
    MCB_PRODUCER_REFUSED = 4
} mcb_producer_status;

/*
 * The producer's own free-distance value.
 *
 * This reproduces FUN_800377dc exactly, including its zero result for a
 * cursor outside [0, capacity).  Callers that must reject an invalid cursor
 * should use mcb_producer_describe, which reports it separately.
 */
size_t mcb_producer_free_distance(size_t capacity, size_t producer, size_t consumer);

typedef struct mcb_producer_state {
    /* The producer's free distance, FUN_800377dc. */
    size_t free_distance;
    /* Bytes the producer believes are occupied: capacity - free_distance. */
    size_t occupied;
    /*
     * The largest message the producer would admit in this state.  Zero when
     * nothing is admissible, which is the case once the free distance drops
     * to five bytes or less.
     */
    size_t max_admitted_message;
    /*
     * The single message length that would publish the producer cursor equal
     * to the consumer cursor.  That collapses the full ring onto the empty
     * ring: the frame just written is never handed to the consumer.  The value
     * equals max_admitted_message, so a producer that sends its largest
     * admissible message always collapses the ring.
     */
    size_t collapse_message_length;
    /*
     * True when producer == consumer, which the producer reads as empty.  The
     * consumer reads the same equality as empty as well, so a ring that the
     * producer has just filled is indistinguishable from an empty one here.
     */
    int empty;
    /* True when the free distance is too small for any message at all. */
    int near_full;
} mcb_producer_state;

/*
 * Describe the ring as the producer sees it.
 *
 * On failure the state object is left unchanged.  Checks are applied in this
 * order: out_state null, capacity zero, then producer or consumer outside
 * [0, capacity).
 */
mcb_producer_status mcb_producer_describe(size_t capacity,
                                          size_t producer,
                                          size_t consumer,
                                          mcb_producer_state *out_state);

typedef struct mcb_producer_write {
    /* The producer's free distance at the time of the write. */
    size_t free_distance;
    /* Ring bytes the packet occupies: message_length + 6. */
    size_t ring_extent;
    /* The producer cursor the writer would publish. */
    size_t new_producer;
    /* Packet bytes written from the producer cursor up to the ring end. */
    size_t bytes_before_wrap;
    /* Packet bytes written after wrapping to ring index 0. */
    size_t bytes_after_wrap;
    /*
     * True when the published cursor equals the consumer cursor.  The frame is
     * then lost: the consumer reads the ring as empty.
     */
    int collapses;
    /*
     * True when the written byte intervals overlap the interval the consumer
     * has not consumed, computed as [R, W) from the cursor values supplied to
     * this call.
     *
     * This is an overlap calculation against an assumed interval.  It does not
     * establish that the consumer cursor was truthful, and it cannot detect a
     * stale cursor: producer == consumer is ambiguous between empty and full
     * without an external no-full invariant.  No live guarantee is claimed.
     */
    int clobbers_unread;
} mcb_producer_write;

/*
 * Plan one stock producer write.
 *
 * message_length is the M of FUN_80037d6c: the message bytes that follow the
 * six-byte outer header.  The plan is refused with MCB_PRODUCER_REFUSED when
 * the producer's own admission would refuse it, which is the case when
 * message_length + 5 >= free distance.
 *
 * The stock writer is UNSAFE at that boundary and the model keeps it unsafe:
 * a message that consumes the exact free distance is admitted, publishes a
 * cursor equal to the consumer cursor, and on the wrap branch whose header
 * fits before the boundary it copies one byte short while still publishing
 * the full requested extent.
 *
 * On failure the write object is left unchanged.
 */
mcb_producer_status mcb_producer_plan_write(size_t capacity,
                                            size_t producer,
                                            size_t consumer,
                                            size_t message_length,
                                            mcb_producer_write *out_write);

/*
 * The ring bytes one kernel packet read consumes for an inner frame of
 * inner_length bytes.
 *
 * The Linux kernel's packet read first takes the four-byte outer marker and
 * then the inner frame, so the ring advances by inner_length + 4 bytes.  For
 * the ordinary control frame the inner size is the producer's message length
 * plus two, so the ring advance equals the producer's packet extent.
 *
 * Overflow contract: an inner_length above SIZE_MAX - 4 cannot be advanced
 * and is reported as 0.  Every valid result is at least 4, so 0 is the
 * unambiguous failure marker and never a valid advance.
 */
size_t mcb_producer_ring_advance(size_t inner_length);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* MCB_PRODUCER_RING_H */
