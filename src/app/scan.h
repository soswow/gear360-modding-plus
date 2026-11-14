/* The sweep's RAW scan (T31): where each frame clips and where it is too
 * dark, in 32 x 32 blocks, from the RAW while it is still in memory.
 *
 * The RAW is 5824 x 2896 samples, 16-bit little-endian, 10 significant bits
 * stored x64 (black 4106, white 65472), CFA GRBG, two 2912-sample halves:
 * the left half is lens 0, the right half lens 1 (gear360-calibration/
 * PHOTOMETRY.md). Each half has the calibrated 2896-square crop 8 samples in
 * from its left edge, with the usable image circle at the crop's centre,
 * radius 1433.5 (gear360-hdri/configs/sm-r210-board-calibration.json). The
 * scan uses a slightly smaller radius, G360_SCAN_RADIUS, so the rim's
 * fall-off never counts as shadow.
 *
 * Sampling: every other 2 x 2 CFA quad across and down (quads at x and y
 * multiples of 4), so all four colours are seen and a quarter of the samples
 * read. A 32 x 32 block holds 8 x 8 = 64 sampled quads. Only quads whose
 * centre is inside the circle count.
 *
 * Per quad:
 *   clipped  any of its four samples is at white (G360_SCAN_WHITE);
 *   dark     the mean of its two greens, black subtracted, is below
 *            G360_SCAN_DARK (net DN).
 * Per block (G360_SCAN_CELL_BYTES, in this order):
 *   n        sampled quads inside the circle (0 = outside, 1..64);
 *   clip     clipped quads;
 *   dark     dark quads;
 *   level    the mean green (net, 0 or more) as round(16 log2(1 + mean)), 0..255;
 *   peak     the brightest sample of the block (net, any colour), same scale.
 *
 * Pure code: no capture/SDK calls, tested on the host (tests/test_scan.py). */
#ifndef G360_APP_SCAN_H
#define G360_APP_SCAN_H
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

#define G360_SCAN_WIDTH 5824u
#define G360_SCAN_HEIGHT 2896u
#define G360_SCAN_STRIDE 11648u
#define G360_SCAN_HALF 2912u
#define G360_SCAN_LENSES 2u
#define G360_SCAN_BLACK 4106u
#define G360_SCAN_WHITE 65472u
/* The circle: centre (in each half) and radius, in samples. */
#define G360_SCAN_CENTER_X 1455.5
#define G360_SCAN_CENTER_Y 1447.5
#define G360_SCAN_RADIUS 1420.0
/* Below this net green a quad counts as dark: 16 of the 10-bit counts,
   about 5.9 EV under white. */
#define G360_SCAN_DARK 1024u
#define G360_SCAN_BLOCK 32u
#define G360_SCAN_COLS 91u /* 2912 / 32 */
#define G360_SCAN_ROWS 91u /* 2896 / 32, rounded up */
#define G360_SCAN_CELL_BYTES 5u
#define G360_SCAN_LENS_BYTES (G360_SCAN_COLS * G360_SCAN_ROWS * G360_SCAN_CELL_BYTES)

/* When a sweep may stop: after at least G360_SCAN_MIN_FRAMES frames, once
   the dark quads are at most G360_SCAN_STOP_PERMILLE thousandths of the
   sampled ones (a plain sweep), or every target block is measured or still
   clipped (an ND sweep, below). */
#define G360_SCAN_MIN_FRAMES 2u
#define G360_SCAN_STOP_PERMILLE 2u

/* Which lenses an ND filter covers (a bit each). */
#define G360_SCAN_ND_LEFT 1u
#define G360_SCAN_ND_RIGHT 2u

typedef struct g360_scan_frame {
    char stamp[16];       /* the sweep's YYYYMMDD-HHMMSS */
    uint32_t index;       /* 1 for the sweep's first frame */
    uint32_t nd;          /* G360_SCAN_ND_* bits; 0: a plain sweep */
    int32_t tv;
    uint32_t exposure_numerator, exposure_denominator;
    uint32_t scan_us;     /* how long the scan took */
    uint32_t quads[G360_SCAN_LENSES], clipped[G360_SCAN_LENSES], dark[G360_SCAN_LENSES];
    uint32_t last;        /* 1: the sweep stopped after this frame */
    uint32_t still_clipped; /* ND sweeps: target blocks clipped here and never measured */
    uint8_t cells[G360_SCAN_LENSES][G360_SCAN_LENS_BYTES];
} g360_scan_frame;

/* Scans one RAW frame into FRAME's counts and cells (the other fields are
   the caller's). Returns 0, or -1 when the image is not the 5824 x 2896
   16-bit layout above (FRAME's counts and cells are then zero). */
int g360_scan_raw(const void *data, size_t size, uint32_t width, uint32_t height, uint32_t stride,
                  g360_scan_frame *frame);

/* A plain sweep, fastest first: 1 when it can stop after FRAME, its
   TAKEN-th frame: the slower frames would only add light to quads already
   measured. */
int g360_scan_plain_done(const g360_scan_frame *frame, uint32_t taken);

/* An ND sweep's targets: the blocks of the lenses in ND (G360_SCAN_ND_*)
   that clip in REFERENCE, the plain sweep's fastest frame, or, with no
   reference, every block inside the circle of those lenses. TARGET gets one
   byte per block (lens-major, row-major): 1 a target, else 0. Returns the
   number of targets. */
uint32_t g360_scan_targets(const g360_scan_frame *reference, uint32_t nd,
                           uint8_t target[G360_SCAN_LENSES * G360_SCAN_COLS * G360_SCAN_ROWS]);

/* An ND sweep, fastest first, after FRAME (its TAKEN-th): marks the
   targets FRAME measures (no clipped quad and a peak at least
   G360_SCAN_DARK) in MEASURED, sets FRAME->still_clipped to the targets
   never measured that clip in FRAME, and returns 1 when the sweep can stop:
   every target measured, or the rest clip already (slower frames clip them
   too). */
int g360_scan_nd_done(g360_scan_frame *frame, uint32_t taken,
                      const uint8_t target[G360_SCAN_LENSES * G360_SCAN_COLS * G360_SCAN_ROWS],
                      uint8_t measured[G360_SCAN_LENSES * G360_SCAN_COLS * G360_SCAN_ROWS]);

/* The page's file (cmd=scan): a header, then each frame's header and
   cells. All numbers little-endian.
     header (36 bytes): "G360SCN1", u16 version 1, u16 frames, u16 cols,
       u16 rows, u16 block, u16 cell bytes, u16 lenses, u16 0, u32 black,
       u32 white, u32 dark
     frame header (80 bytes): char stamp[16], u32 index, u32 nd, i32 tv,
       u32 exposure numerator, u32 denominator, u32 scan_us, u32 quads[2],
       u32 clipped[2], u32 dark[2], u32 last, u32 still_clipped,
       u32 reference (1: the plain sweep an ND sweep was aimed by), u32 0
     cells: lens 0 then lens 1, rows top to bottom, G360_SCAN_CELL_BYTES each */
#define G360_SCAN_FILE_HEADER 36u
#define G360_SCAN_FRAME_HEADER 80u
#define G360_SCAN_FRAME_BYTES (G360_SCAN_FRAME_HEADER + G360_SCAN_LENSES * G360_SCAN_LENS_BYTES)
size_t g360_scan_file_header(uint8_t out[G360_SCAN_FILE_HEADER], uint32_t frames);
size_t g360_scan_frame_header(uint8_t out[G360_SCAN_FRAME_HEADER], const g360_scan_frame *frame, int reference);

#ifdef __cplusplus
}
#endif
#endif
