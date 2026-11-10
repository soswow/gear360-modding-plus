// The public header compiles as C++ and links against the C objects.
// On the host there is no camera library, so g360_open must refuse with
// G360_E_LOAD without touching anything else.
#include "g360cap.h"
#include "g360_output.h"
#include "g360_dng.h"

#include <cstdio>
#include <cstring>

int main()
{
    g360_open_options options;
    std::memset(&options, 0, sizeof options);
    options.free_bytes = 1;
    int status = g360_open(&options);
    if (status != G360_E_LOAD) {
        std::printf("unexpected g360_open status %d (%s)\n", status, g360_status_text(status));
        return 1;
    }
    g360_still still;
    std::memset(&still, 0, sizeof still);
    if (g360_capture(nullptr, &still) != G360_E_STATE) return 1;
    g360_still_free(&still);
    if (g360_save_dng(nullptr, nullptr, nullptr) != G360_E_ARGUMENT) return 1;
    if (g360_save_jpeg_exif(nullptr, nullptr, nullptr, nullptr) != G360_E_IMAGE) return 1;
    std::printf("g360cap C++ linkage test passed\n");
    return 0;
}
