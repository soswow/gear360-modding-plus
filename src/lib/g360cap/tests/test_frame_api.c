/* Run by test_g360cap.py against the synthetic SDK; no camera access. */
#include "g360cap.h"

#include <assert.h>
#include <stdio.h>

static void check_frame(const g360_frame *frame, unsigned sequence)
{
    size_t i;
    assert(frame->image.size == 4096);
    assert(frame->mode == 3);
    assert((frame->image.flags & (G360_IMAGE_COPIED | G360_IMAGE_RAW_VALUES_OK)) ==
           (G360_IMAGE_COPIED | G360_IMAGE_RAW_VALUES_OK));
    for (i = 0; i < frame->image.size; ++i)
        assert(frame->image.data[i] == (unsigned char)(i * 7u + sequence * 31u + 0x5au));
}

int main(void)
{
    g360_open_options open_options = {0};
    g360_capture_options options = {0};
    g360_shot first;
    g360_still second;
    options.frame = 1;
    options.frame_layout.physical = 0xa6000000u;
    options.frame_layout.width = 64;
    options.frame_layout.height = 32;
    options.frame_layout.stride = 128;
    options.frame_layout.bits = 16;
    assert(g360_capture_shot(&options, &first) == G360_E_STATE);
    g360_shot_free(&first);
    open_options.frame_pause = 1;
    assert(g360_open(&open_options) == G360_OK);

    /* Invalid API options must not arm a job or send any still. These
       tests bypass the CLI's independent option validation. */
    options.frame_layout.width = 0x80000000u;
    assert(g360_capture_shot(&options, &first) == G360_E_ARGUMENT);
    g360_shot_free(&first);
    options.frame_layout.width = 64;
    options.frame_layout.height = 0x80000001u;
    assert(g360_capture_shot(&options, &first) == G360_E_ARGUMENT);
    g360_shot_free(&first);
    options.frame_layout.height = 32;
    options.capture_type = 0x17;
    assert(g360_capture_shot(&options, &first) == G360_E_ARGUMENT);
    g360_shot_free(&first);
    options.capture_type = 0;
    options.raw = 1;
    options.raw_layout[0].width = 0x80000000u;
    assert(g360_capture_shot(&options, &first) == G360_E_ARGUMENT);
    g360_shot_free(&first);
    options.raw = 0;

    options.frame = 0;
    options.skip_jpeg_copy = 1;
    assert(g360_capture_shot(&options, &first) == G360_E_ARGUMENT);
    g360_shot_free(&first);
    options.frame = 1;
    options.skip_jpeg_copy = 0;

    assert(g360_capture_shot(&options, &first) == G360_OK);
    check_frame(&first.frame, 1);
    assert(g360_capture(&options, &second) == G360_OK);
    check_frame(&second.frame, 2);
    /* The fake reuses the source after expansion and on the next still.
       Both public result types must keep independent caller-owned copies. */
    check_frame(&first.frame, 1);
    g360_close();
    check_frame(&first.frame, 1);
    check_frame(&second.frame, 2);
    g360_shot_free(&first);
    g360_still_free(&second);
    assert(g360_capture(&options, &second) == G360_E_STATE);
    g360_still_free(&second);
    puts("g360cap frame API tests passed");
    return 0;
}
