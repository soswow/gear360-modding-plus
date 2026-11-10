/*
 * The session header must be usable from C++ unchanged.
 */
#include "capture_session.h"

#include <cstddef>
#include <cstdint>

static void on_image(capture_session *, const capture_session_image *, void *)
{
}

int main()
{
    capture_session session{};
    capture_session_init(&session);
    capture_session_config config{};
    capture_session_backend backend{};
    config.backend = &backend;
    config.on_image = &on_image;
    config.remaining_shots = 2448u;
    config.free_space_bytes = 7477198848ull;
    (void)session;
    (void)config;
    return 0;
}
