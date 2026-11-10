// C++ linkage smoke check; no library load or camera access.
#include "capture_session_library.h"

int main()
{
    capture_library_host_close(nullptr);
    return capture_library_host_supports_sensor_retention() != 0 ||
           capture_library_host_last_error() == nullptr;
}
