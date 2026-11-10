#ifndef G360_APP_PLATFORM_H
#define G360_APP_PLATFORM_H
#include "capture_libc.h"
#if defined(__APPLE__)
#include <pthread.h>
#include <signal.h>
#else
/* glibc ARM32 pthread_t is unsigned long; no opaque mutex layout is used. */
typedef unsigned long pthread_t;
int pthread_create(pthread_t *thread, const void *attributes, void *(*run)(void *), void *argument);
int pthread_join(pthread_t thread, void **result);
int pthread_detach(pthread_t thread);
typedef void (*app_signal_handler)(int);
app_signal_handler signal(int number, app_signal_handler handler);
#define SIGINT 2
#define SIGTERM 15
#define SIGHUP 1
#define SIG_IGN ((app_signal_handler)1)
int fflush(capture_file *file);
void sync(void);
ptrdiff_t read(int file, void *data, size_t count);
int execv(const char *path, char *const argv[]);
int dup2(int file, int target);
int chdir(const char *path);
int unlink(const char *path);
char *getenv(const char *name);
#endif
#endif
