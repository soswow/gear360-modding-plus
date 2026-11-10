/*
 * The little of the C library this component uses.
 *
 * The camera is a 32-bit machine and its own C library is installed
 * without its headers, so the program is built against the declarations
 * below rather than against a set of headers.  The declarations are the
 * standard ones, and the sizes are the compiler's own: `size_t` comes
 * from the compiler's <stddef.h>, which is 32 bits on the camera and 64
 * bits on the host, and getting that wrong would corrupt every call that
 * passes a size.  The compiler's headers are freestanding headers: they
 * need no C library to be installed, which is what makes them usable
 * here.
 *
 * The host has real headers, so the host build uses the host's own and
 * this header only has to cover the camera.  That split is deliberate.
 * The two C libraries do not agree about everything - the standard
 * streams are the obvious example: on the camera's, `stdout` and
 * `stderr` are ordinary variables, the target's libc.so.6 defines them
 * as data symbols, and a reference to them is resolved by a copy
 * relocation; on Darwin's they are the system's own, reached through
 * `__stdoutp` and `__stderrp`.  Declaring the host's streams by hand
 * once got that wrong, and the program then died in fprintf on its own
 * error path.  The host's headers are the authority for the host, and
 * the camera has none, so each side uses what it actually has.
 *
 * So: one code path, and on each side the declarations that side really
 * has.
 */
#ifndef CAPTURE_LIBC_H
#define CAPTURE_LIBC_H

#include <stddef.h>              /* size_t: the compiler's own header */

#if defined(__APPLE__)

/* The host: the system's own headers, and the system's own names. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dlfcn.h>
#include <fcntl.h>
#include <unistd.h>
#include <time.h>

typedef FILE capture_file;
typedef struct timespec capture_timespec;

#define CAPTURE_RTLD_NOW RTLD_NOW
#define CAPTURE_RTLD_GLOBAL RTLD_GLOBAL
#define CAPTURE_CLOCK_MONOTONIC CLOCK_MONOTONIC

/*
 * The flags the run's own log file is opened with.  On the host they are
 * the system's own flags, so the host does not carry the target's numbers
 * around.
 */
#define CAPTURE_O_WRONLY O_WRONLY
#define CAPTURE_O_CREAT  O_CREAT
#define CAPTURE_O_TRUNC  O_TRUNC

#else

/*
 * The camera: no headers, so the little that is used is declared here.
 * The stream type stays opaque, because the camera's structure for it
 * is not known here.
 */
typedef struct capture_file capture_file;

extern capture_file *stdout;
extern capture_file *stderr;

void *memcpy(void *destination, const void *source, size_t count);
void *memset(void *destination, int value, size_t count);
int memcmp(const void *left, const void *right, size_t count);
size_t strlen(const char *text);
int strcmp(const char *left, const char *right);
char *strchr(const char *text, int character);

void *malloc(size_t size);
void *calloc(size_t count, size_t size);
void free(void *pointer);

capture_file *fopen(const char *path, const char *mode);
size_t fwrite(const void *data, size_t size, size_t count,
              capture_file *stream);
size_t fread(void *data, size_t size, size_t count, capture_file *stream);
int fclose(capture_file *stream);

int printf(const char *format, ...);
int fprintf(capture_file *stream, const char *format, ...);
int snprintf(char *buffer, size_t size, const char *format, ...);

unsigned long strtoul(const char *text, char **end, int base);
unsigned long long strtoull(const char *text, char **end, int base);

/*
 * The dynamic loader, for the library host.  glibc has RTLD_NOW 2 and
 * RTLD_GLOBAL 0x100.
 */
#define CAPTURE_RTLD_NOW 2
#define CAPTURE_RTLD_GLOBAL 0x100
void *dlopen(const char *file, int mode);
void *dlsym(void *handle, const char *name);
char *dlerror(void);
int dlclose(void *handle);

/*
 * The monotonic clock, for the blocking capture's timeout.  The
 * identifier is one on Linux.
 *
 * On the camera the clock call is not in the C library itself: the
 * target's libc.so.6 does not export clock_gettime, and its librt.so.1
 * does, so the program must be linked against librt.  The camera's glibc
 * is 2.13, where the clock calls were still in librt.
 */
#define CAPTURE_CLOCK_MONOTONIC 1
typedef struct capture_timespec {
    long tv_sec;
    long tv_nsec;
} capture_timespec;
int clock_gettime(int clock_id, capture_timespec *time);
int nanosleep(const capture_timespec *requested, capture_timespec *remaining);

/*
 * The run's own log: a file of its own, written and flushed as the program
 * goes, so that a run which takes the camera down still leaves a record of
 * how far it got.
 *
 * The flags are the target's own: O_WRONLY is 1, O_CREAT is 0100 and
 * O_TRUNC is 01000 on Linux.  The host does not use these numbers - it
 * uses its system's flags - so they live here, in the target's branch.
 *
 * open is variadic, because the mode is only read when O_CREAT is set,
 * and write returns a signed count: the target's ssize_t is a signed
 * 32-bit type there, and ptrdiff_t is the compiler's own signed type of
 * that width.
 */
#define CAPTURE_O_WRONLY 0x0001
#define CAPTURE_O_CREAT  0x0040
#define CAPTURE_O_TRUNC  0x0200

int open(const char *path, int flags, ...);
ptrdiff_t write(int file, const void *data, size_t count);
int fsync(int file);
int close(int file);

#endif /* __APPLE__ */

#endif /* CAPTURE_LIBC_H */
