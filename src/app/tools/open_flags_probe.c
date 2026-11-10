/*
 * Map the camera's open() flag bits and its directory fsync behaviour.
 *
 * Why: the app's storage layer opens directories with
 * O_RDONLY|O_DIRECTORY|O_NOFOLLOW|O_CLOEXEC.  On the camera those bit
 * numbers do not mean what the modern headers say, and the kernel rejects
 * the combination with EINVAL, so the app cannot open its own photo
 * directory at all.  This probe reports, per candidate bit, what the
 * target's kernel actually does with it, so the header can carry the
 * target's numbers instead of guessed ones.
 *
 * The probe only reads.  It creates one scratch directory with one
 * regular file and one symbolic link inside the app's own area, and it
 * removes them again; nothing outside that directory is touched.
 */
#include "capture_libc.h"

int mkdir(const char *path, unsigned int mode);
int symlink(const char *target, const char *link);
int unlink(const char *path);
int rmdir(const char *path);
int fcntl(int file, int command, ...);
ptrdiff_t read(int file, void *data, size_t count);
int *__errno_location(void);
#define errno (*__errno_location())

#define O_RDONLY_VALUE 0x0000
#define O_WRONLY_VALUE 0x0001
#define O_RDWR_VALUE 0x0002
#define O_CREAT_VALUE  0x0040
#define O_TRUNC_VALUE  0x0200

static const char *scratch = "/opt/storage/sdcard/G360/probe";
static char scratch_file[256];
static char scratch_link[256];

/* The candidate bits, from the least to the most significant. */
static const unsigned long candidate_bits[] = {
    0x00000080UL, 0x00000100UL, 0x00000200UL, 0x00000400UL,
    0x00000800UL, 0x00001000UL, 0x00002000UL, 0x00004000UL,
    0x00008000UL, 0x00010000UL, 0x00020000UL, 0x00040000UL,
    0x00080000UL, 0x00100000UL, 0x00200000UL, 0x00400000UL,
    0x00800000UL, 0x01000000UL, 0x02000000UL, 0x04000000UL
};
#define CANDIDATE_COUNT (sizeof candidate_bits / sizeof candidate_bits[0])

static void report(const char *what, const char *path, unsigned long flags,
                   int fd, int open_errno, int sync_result, int sync_errno,
                   int close_result, int close_errno)
{
    if (fd < 0) {
        printf("%-8s %-34s flags=0x%08lx open=FAIL errno=%d\n",
               what, path, flags, open_errno);
        return;
    }
    printf("%-8s %-34s flags=0x%08lx open=OK fsync=%d errno=%d close=%d errno=%d\n",
           what, path, flags, sync_result, sync_errno, close_result, close_errno);
}

static void try_open(const char *what, const char *path, unsigned long flags)
{
    int fd, sync_result, sync_errno, close_result, close_errno, open_errno;
    errno = 0;
    fd = open(path, (int)flags);
    open_errno = errno;
    if (fd < 0) {
        report(what, path, flags, -1, open_errno, 0, 0, 0, 0);
        return;
    }
    errno = 0;
    sync_result = fsync(fd);
    sync_errno = errno;
    errno = 0;
    close_result = close(fd);
    close_errno = errno;
    report(what, path, flags, fd, open_errno, sync_result, sync_errno,
           close_result, close_errno);
}

static void try_all_bits(const char *what, const char *path)
{
    unsigned k;
    for (k = 0; k < CANDIDATE_COUNT; ++k)
        try_open(what, path, O_RDONLY_VALUE | candidate_bits[k]);
}

static int make_scratch(void)
{
    int fd, n;
    char path[256];
    mkdir("/opt/storage/sdcard/G360", 0755);
    mkdir(scratch, 0755);
    n = snprintf(scratch_file, sizeof scratch_file, "%s/regular", scratch);
    if (n < 0 || (size_t)n >= sizeof scratch_file) return -1;
    n = snprintf(scratch_link, sizeof scratch_link, "%s/link", scratch);
    if (n < 0 || (size_t)n >= sizeof scratch_link) return -1;
    fd = open(scratch_file, O_WRONLY_VALUE | O_CREAT_VALUE | O_TRUNC_VALUE, 0644);
    if (fd < 0) return -1;
    (void)write(fd, "x", 1);
    if (close(fd) != 0) return -1;
    unlink(scratch_link);
    if (symlink("regular", scratch_link) != 0) return -1;
    snprintf(path, sizeof path, "%s", scratch_file);
    return 0;
}

static void remove_scratch(void)
{
    unlink(scratch_link);
    unlink(scratch_file);
    rmdir(scratch);
}

/* Which bit sets FD_CLOEXEC?  F_GETFD is 1 and FD_CLOEXEC is 1 on Linux. */
static void probe_cloexec(const char *path)
{
    static const unsigned long candidate[] = {
        0x00008000UL, 0x00010000UL, 0x00020000UL, 0x00040000UL,
        0x00080000UL, 0x00100000UL, 0x00200000UL, 0x00400000UL
    };
    unsigned k;
    for (k = 0; k < sizeof candidate / sizeof candidate[0]; ++k) {
        int fd, flags, open_errno;
        errno = 0;
        fd = open(path, (int)(O_RDONLY_VALUE | candidate[k]));
        open_errno = errno;
        if (fd < 0) {
            printf("cloexec %-20s flags=0x%08lx open=FAIL errno=%d\n",
                   path, candidate[k], open_errno);
            continue;
        }
        errno = 0;
        flags = fcntl(fd, 1 /* F_GETFD */);
        printf("cloexec %-20s flags=0x%08lx fdflags=0x%x cloexec=%d errno=%d\n",
               path, candidate[k], flags, (flags & 1) ? 1 : 0, errno);
        (void)close(fd);
    }
}

/* Does the create bit create, and does the exclusive bit refuse? */
static void probe_create(const char *directory)
{
    char missing[256], fresh[256], present[256];
    int fd, n;
    n = snprintf(missing, sizeof missing, "%s/missing", directory);
    if (n < 0 || (size_t)n >= sizeof missing) return;
    n = snprintf(fresh, sizeof fresh, "%s/fresh", directory);
    if (n < 0 || (size_t)n >= sizeof fresh) return;
    n = snprintf(present, sizeof present, "%s/present", directory);
    if (n < 0 || (size_t)n >= sizeof present) return;

    errno = 0;
    fd = open(missing, O_RDONLY_VALUE);
    printf("create  missing+read               open=%d errno=%d\n", fd, errno);
    if (fd >= 0) (void)close(fd);

    errno = 0;
    fd = open(missing, O_RDONLY_VALUE | 0x00000080UL /* exclusive */);
    printf("create  missing+exclusive          open=%d errno=%d\n", fd, errno);
    if (fd >= 0) (void)close(fd);

    errno = 0;
    fd = open(fresh, O_WRONLY_VALUE | 0x00000040UL | 0x00000080UL, 0644);
    printf("create  fresh+create+exclusive     open=%d errno=%d\n", fd, errno);
    if (fd >= 0) (void)close(fd);

    errno = 0;
    fd = open(fresh, O_WRONLY_VALUE | 0x00000040UL | 0x00000080UL, 0644);
    printf("create  fresh again+create+exclusive open=%d errno=%d\n", fd, errno);
    if (fd >= 0) (void)close(fd);

    errno = 0;
    fd = open(fresh, O_RDWR_VALUE | 0x00000040UL, 0644);
    printf("create  fresh+create+readwrite     open=%d errno=%d\n", fd, errno);
    if (fd >= 0) (void)close(fd);

    unlink(missing);
    unlink(fresh);
    unlink(present);
}

int main(void)
{
    static const char *const filesystems[][2] = {
        {"exfat", "/opt/storage/sdcard/G360"},
        {"exfat2", "/opt/storage/sdcard/mods"},
        {"rootfs", "/opt/storage"},
        {"tmpfs", "/tmp"}
    };
    unsigned k;
    printf("probe: target open() flag bits and directory fsync\n");
    if (make_scratch() != 0) {
        printf("probe: cannot prepare the scratch directory\n");
        return 1;
    }
    for (k = 0; k < sizeof filesystems / sizeof filesystems[0]; ++k) {
        printf("--- %s %s ---\n", filesystems[k][0], filesystems[k][1]);
        try_open(filesystems[k][0], filesystems[k][1], O_RDONLY_VALUE);
        try_all_bits(filesystems[k][0], filesystems[k][1]);
    }
    printf("--- regular file and symbolic link ---\n");
    try_all_bits("regular", scratch_file);
    try_all_bits("link", scratch_link);
    printf("--- close on exec ---\n");
    probe_cloexec(scratch_file);
    printf("--- create and exclusive ---\n");
    probe_create(scratch);
    remove_scratch();
    printf("probe: done\n");
    return 0;
}
