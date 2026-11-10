#include "pool_monitor.h"
#include "raw_format.h"

int main() {
    g360_raw_tracker tracker;
    g360_raw_tracker_reset(&tracker);
    uint8_t header[G360_DNG_HEADER_CAPACITY];
    g360_dng_parameters parameters = {0, 1.0, 1.0, "linkage"};
    return tracker.phase == G360_RAW_IDLE &&
                   g360_dng_write_header(header, sizeof(header), &parameters) > 0
               ? 0
               : 1;
}
