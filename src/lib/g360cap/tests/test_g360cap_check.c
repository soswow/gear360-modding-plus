#include "g360cap_check.h"

#include <assert.h>
#include <stdio.h>

int main(void)
{
    const char *maps =
        "00010000-00020000 r-xp 00000000 00:01 1 /bin/x\n"
        "00020000-00030000 rw-p 00000000 00:00 0\n"
        "00030000-00031000 ---p 00000000 00:00 0\n"
        "00031000-00040000 rw-p 00000000 00:00 0\n"
        "a0000000-a1000000 rw-s 85400000 00:05 1 /dev/mem\n"
        "a1000000-a2000000 rw-s 86400000 00:05 1 /dev/mem\n"
        "garbage line\n"
        "b0000000-b0001000 r--p 00000000 00:00 0";
    unsigned char jpeg[] = {0xff, 0xd8, 0x00, 0x11, 0xff, 0xd9};
    unsigned char bad[] = {0xff, 0xd8, 0x00, 0x11, 0xd9, 0xff};

    assert(g360_maps_covers(maps, 0x10000, 0x10));
    assert(g360_maps_covers(maps, 0x1fff0, 0x20));            /* spans two adjacent mappings */
    assert(!g360_maps_covers(maps, 0x2fff0, 0x20));           /* runs into a no-access mapping */
    assert(!g360_maps_covers(maps, 0x30000, 1));
    assert(g360_maps_covers(maps, 0xa0fff000, 0x2000));       /* two /dev/mem windows */
    assert(!g360_maps_covers(maps, 0xa1fff000, 0x2000));      /* past the end */
    assert(!g360_maps_covers(maps, 0x0, 4));
    assert(!g360_maps_covers(maps, 0x10000, 0));
    assert(!g360_maps_covers(maps, (uintptr_t)-8, 16));      /* overflow */
    assert(g360_maps_covers(maps, 0xb0000ffc, 4));            /* last line without newline */
    assert(!g360_maps_covers(0, 0x10000, 4));
    {
        uintptr_t v = 0;
        const char *devmem =
            "00010000-00020000 r-xp 85400000 00:01 1 /bin/notmem\n"
            "a0000000-a1000000 rw-s 85400000 00:05 1 /dev/mem\n"
            "a1000000-a2000000 rw-s 86400000 00:05 1 /dev/mem\n"
            "b0000000-b1000000 ---s a6000000 00:05 1 /dev/mem\n"
            "c0000000-c1000000 r--s a6000000 00:05 1 /dev/memory\n"
            "d0000000-d3000000 r--s a6000000 00:05 1 /dev/mem";
        assert(g360_maps_physical(devmem, 0x85400010, 0x10, &v) && v == 0xa0000010);
        assert(g360_maps_physical(devmem, 0x873fff00, 0x100, &v) && v == 0xa1ffff00);
        assert(!g360_maps_physical(devmem, 0x863ffff0, 0x20, &v));  /* spans two lines */
        assert(!g360_maps_physical(devmem, 0x873fff00, 0x101, &v)); /* past the end */
        assert(!g360_maps_physical(devmem, 0x85400000, 0, &v));
        assert(g360_maps_physical(devmem, 0xa6000000, 0x202b800, &v) && v == 0xd0000000);
        /* not readable (b...), wrong path (c...): only the last line counts */
        assert(!g360_maps_physical(devmem, 0x10000, 4, &v));        /* not /dev/mem */
        assert(!g360_maps_physical(devmem, (uintptr_t)-8, 16, &v)); /* overflow */
        assert(!g360_maps_physical(0, 0x85400000, 4, &v));
    }
    /* Out of order input still resolves. */
    assert(g360_maps_covers("2000-3000 r--p 0 0 0\n1000-2000 r--p 0 0 0\n", 0x1800, 0x1000));

    {
        size_t bytes = 123;
        const size_t limit = 96u * 1024u * 1024u;
        assert(g360_raw_extent(5824, 2896, 11648, 16, limit, &bytes));
        assert(bytes == 33732608u);
        assert(g360_raw_extent(64, 32, 132, 12, limit, &bytes) && bytes == 4224u);
        /* width*2 and stride*height wrapped in the old ARM checks. */
        bytes = 123;
        assert(!g360_raw_extent(0x80000000u, 1, 128, 12, limit, &bytes));
        assert(!g360_raw_extent(1, 0x80000001u, 2, 12, limit, &bytes));
        assert(!g360_raw_extent(1, UINT32_MAX, UINT32_MAX, 12, limit, &bytes));
        assert(!g360_raw_extent(65, 32, 128, 16, limit, &bytes));
        assert(!g360_raw_extent(64, 0, 128, 16, limit, &bytes));
        assert(!g360_raw_extent(0, 32, 128, 16, limit, &bytes));
        assert(!g360_raw_extent(64, 32, 128, 0, limit, &bytes));
        assert(!g360_raw_extent(64, 32, 128, 17, limit, &bytes));
        assert(!g360_raw_extent(64, 32, 128, 16, 4095, &bytes));
        assert(!g360_raw_extent(64, 32, 128, 16, limit, 0));
        assert(bytes == 123); /* refusal never publishes a partial extent */
        assert(g360_raw_extent(64, 32, 128, 16, 4096, &bytes) && bytes == 4096);
    }

    assert(g360_jpeg_markers_ok(jpeg, sizeof jpeg));
    assert(!g360_jpeg_markers_ok(bad, sizeof bad));
    assert(!g360_jpeg_markers_ok(jpeg, 3));
    assert(!g360_jpeg_markers_ok(0, 6));
    puts("g360cap check tests passed");
    return 0;
}
