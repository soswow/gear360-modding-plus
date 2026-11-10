/* The target's standard libc ABI additions; host uses its own headers. */
#ifndef G360_STORAGE_PLATFORM_H
#define G360_STORAGE_PLATFORM_H
#include "capture_libc.h"
#include <stdint.h>
#if defined(__APPLE__)
#include <dirent.h>
#include <errno.h>
#include <sys/file.h>
#include <sys/stat.h>
#define ST_RDONLY O_RDONLY
#define ST_WRONLY O_WRONLY
#define ST_RDWR O_RDWR
#define ST_TRUNC O_TRUNC
#define ST_APPEND O_APPEND
#define ST_CREAT O_CREAT
#define ST_EXCL O_EXCL
#define ST_NOFOLLOW O_NOFOLLOW
#define ST_DIRECTORY O_DIRECTORY
#define ST_CLOEXEC O_CLOEXEC
#define st_dir DIR
#define st_dirent struct dirent
#define st_readdir readdir
#else
/*
 * The camera's own open() flag numbers, measured on the target
 * (Linux 3.5.0, ARM32) with src/app/tools/open_flags_probe.c.
 *
 * They are not the generic Linux numbers.  The generic headers put
 * O_DIRECTORY at 0200000 and O_NOFOLLOW at 0400000; on the camera those
 * two bits are one octal digit lower, at 040000 and 0100000, and the bit
 * the generic headers use for O_DIRECTORY is the camera's O_DIRECT
 * instead.  The camera's card (FAT32, served by the kernel's exfat
 * driver) rejects that bit, so
 * opening a directory with that bit fails with EINVAL.  That is what
 * stopped the app at storage initialization on a fresh boot.
 *
 * The measured table: O_CREAT 0100, O_EXCL 0200, O_TRUNC 01000,
 * O_APPEND 02000, O_NONBLOCK 04000, O_DSYNC 010000,
 * O_DIRECTORY 040000, O_NOFOLLOW 0100000, O_DIRECT 0200000,
 * O_LARGEFILE 01000000, O_CLOEXEC 02000000, O_PATH 0100000000.
 * O_DIRECTORY is 040000 because a regular file opened with it gives
 * ENOTDIR; O_NOFOLLOW is 0100000 because a symbolic link opened with it
 * gives ELOOP; O_CLOEXEC is 02000000 because it is the only bit that
 * sets FD_CLOEXEC.  Directory fsync works on the card, so the durability
 * of the counter slots needs no change.
 */
#define ST_RDONLY 0
#define ST_WRONLY 1
#define ST_RDWR 2
#define ST_CREAT 0100
#define ST_EXCL 0200
#define ST_TRUNC 01000
#define ST_APPEND 02000
#define ST_NOFOLLOW 0100000
#define ST_DIRECTORY 040000
#define ST_CLOEXEC 02000000
#define LOCK_EX 2
#define LOCK_NB 4
#define EINTR 4
#define ENOENT 2
#define EEXIST 17
int mkdir(const char *path, unsigned int mode);
int unlink(const char *path);
int flock(int fd, int operation);
ptrdiff_t read(int fd, void *data, size_t count);
long lseek(int fd, long offset, int whence);
int ftruncate(int fd, long size);
int *__errno_location(void);
#define errno (*__errno_location())
typedef struct storage_dir st_dir;
/* glibc readdir64 ABI, independent of the process's default off_t width:
 * ino64_t, off64_t, u16 reclen, u8 type, then name at byte19. */
typedef struct storage_dirent64 {
    uint64_t ino;
    int64_t offset;
    unsigned short reclen;
    unsigned char type;
    char d_name[256];
} st_dirent;
_Static_assert(offsetof(st_dirent, reclen) == 16 && offsetof(st_dirent, type) == 18 &&
               offsetof(st_dirent, d_name) == 19, "not glibc's struct dirent64 layout");
st_dir *opendir(const char *path);
st_dirent *readdir64(st_dir *dir);
int closedir(st_dir *dir);
#define st_readdir readdir64
/* ARM32 glibc2.13 time_t/long are32 bits. POSIX tm has nine int fields,
 * followed by glibc's long gmtoff and zone pointer. These members are read
 * after localtime_r; no strftime or caller-visible target struct needed. */
typedef struct storage_tm {
    int sec, min, hour, mday, mon, year, wday, yday, isdst;
    long gmtoff;
    const char *zone;
} storage_tm;
/* localtime_r writes the whole struct: ours must be exactly glibc's 44 bytes. */
_Static_assert(offsetof(storage_tm, gmtoff) == 36 && offsetof(storage_tm, zone) == 40 &&
               sizeof(storage_tm) == 44, "not glibc's ARM32 struct tm layout");
storage_tm *localtime_r(const long *clock, storage_tm *result);
#endif
#endif
