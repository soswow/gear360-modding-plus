/* 72x32 monochrome renderer. Default output is PBM on stdout, no device access.
 * Linux device writer is EXPERIMENTAL until ABI/orientation/restoration tests.
 * Enable at build time with -DHDR_EXPERIMENTAL_DRIVER and pass --write.
 * No display init, power, contrast, app restart or automatic clearing. */
#define _POSIX_C_SOURCE 200809L
#include <stdint.h>
#ifdef G360_CAMERA_LIBC
/* Camera build without a cross sysroot. Uses the same camera libc
 * declarations as g360cap; the stock-process mirror is excluded there. */
#include "capture_libc.h"
typedef capture_file FILE;
#define EOF (-1)
#define O_RDWR 2
int puts(const char *text);
int putchar(int character);
int fgetc(FILE *stream);
int ferror(FILE *stream);
void perror(const char *prefix);
int ioctl(int fd, unsigned long request, ...);
#else
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#endif
#ifdef HDR_EXPERIMENTAL_DRIVER
#ifndef __linux__
#error Experimental driver requires Linux
#endif
#ifndef G360_CAMERA_LIBC
#include <fcntl.h>
#include <sys/ioctl.h>
#include <unistd.h>
#endif
#include <stddef.h>
struct rectangle { int32_t top, bottom, left, right; };
struct drawing { struct rectangle rect; uint8_t pixels[2304]; };
_Static_assert(sizeof(struct drawing) == 2320, "draw ABI size");
_Static_assert(offsetof(struct drawing, pixels) == 16, "draw ABI offset");
#ifndef HDR_NO_STOCK_MIRROR
/* Firmware-specific, read-only stock mirror. Refuse a different app layout.
 * These words are from ShowVirtualStatusLCD in the retained camera ELF.
 * No ptrace, app suspension, or memory writes. */
static int stock_pixels(const char *pid, uint8_t *pixels) {
    char *end, path[80], comm[32];
    long process = strtol(pid, &end, 10);
    if (!*pid || *end || process < 1 || process > 10000000) return -1;
    snprintf(path, sizeof(path), "/proc/%ld/comm", process);
    FILE *f = fopen(path, "r");
    if (!f) return -1;
    int matched = fgets(comm, sizeof(comm), f) && !strcmp(comm, "di-camera-app\n");
    fclose(f);
    if (!matched) return -1;
    snprintf(path, sizeof(path), "/proc/%ld/mem", process);
    int fd = open(path, O_RDONLY);
    if (fd < 0) return -1;
    uint32_t code[2], address;
    int ok = pread(fd, code, sizeof(code), 0x1650fc) == sizeof(code) &&
        code[0] == 0xe92d4038 && code[1] == 0xe1a05000 &&
        pread(fd, &address, sizeof(address), 0x165140) == sizeof(address) &&
        address == 0x35617c;
    uint8_t check[2304];
    int stable = 0;
    for (int attempt = 0; ok && attempt < 3; attempt++) {
        if (pread(fd, pixels, 2304, address + 2304) != 2304 ||
            pread(fd, check, 2304, address + 2304) != 2304) break;
        if (!memcmp(check, pixels, 2304)) { stable = 1; break; }
    }
    close(fd);
    if (!stable) return -1;
    for (int i = 0; i < 2304; i++) if (pixels[i] > 1) return -1;
    return 0;
}
#endif
#endif
/* Five columns, bit zero at top. Original compact glyph definitions. */
static const char chars[] = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ /:-.";
static const uint8_t font[][5] = {
 {62,81,73,69,62},{0,66,127,64,0},{98,81,73,73,70},{34,65,73,73,54},
 {24,20,18,127,16},{39,69,69,69,57},{60,74,73,73,48},{1,113,9,5,3},
 {54,73,73,73,54},{6,73,73,41,30},
 {126,9,9,9,126},{127,73,73,73,54},{62,65,65,65,34},{127,65,65,34,28},
 {127,73,73,73,65},{127,9,9,9,1},{62,65,73,73,122},{127,8,8,8,127},
 {0,65,127,65,0},{32,64,65,63,1},{127,8,20,34,65},{127,64,64,64,64},
 {127,2,12,2,127},{127,2,4,8,127},{62,65,65,65,62},{127,9,9,9,6},
 {62,65,81,33,94},{127,9,25,41,70},{38,73,73,73,50},{1,1,127,1,1},
 {63,64,64,64,63},{31,32,64,32,31},{63,64,56,64,63},{99,20,8,20,99},
 {3,4,120,4,3},{97,81,73,69,67},
 {0,0,0,0,0},{96,16,8,4,3},{0,54,54,0,0},{8,8,8,8,8},{0,96,96,0,0}
};
static int text(uint8_t *pixels, const char *line, int left, int top, int sx, int sy) {
    if (left + (int)strlen(line) * 6 * sx - sx > 72 || top + 7 * sy > 32) return -1;
    for (int col = 0; line[col]; col++) {
        const char *ch = strchr(chars, line[col]);
        if (!ch) return -1;
        for (int x = 0; x < 5; x++) for (int y = 0; y < 7; y++)
            if ((font[ch - chars][x] >> y) & 1)
                for (int dx = 0; dx < sx; dx++) for (int dy = 0; dy < sy; dy++)
                    pixels[(top+y*sy+dy)*72+left+col*6*sx+x*sx+dx] = 1;
    }
    return 0;
}
int main(int argc, char **argv) {
    int write_device = argc > 1 && !strcmp(argv[1], "--write");
    int first = write_device ? 2 : 1;
    if (argc <= first || argc - first > 4) {
        fprintf(stderr, "usage: gear360-display [--write] LINE [LINE ...] | --clear | --center TEXT (12 columns, 4 rows)\n"); return 2;
    }
    uint8_t pixels[2304] = {0};
    if (!strcmp(argv[first], "--clear")) {
        if (argc != first + 1) return 2;
    } else if (!strcmp(argv[first], "--center")) {
        if (argc != first + 2 || strlen(argv[first + 1]) > 12) return 2;
        int length = (int)strlen(argv[first + 1]);
        int width = length ? length * 6 - 1 : 0;
        if (text(pixels, argv[first + 1], (72 - width) / 2, (32 - 7) / 2, 1, 1)) return 2;
    } else if (!strcmp(argv[first], "--done")) {
        if (argc != first + 1) return 2;
        text(pixels, "DONE", 1, 2, 3, 4);
    } else if (!strcmp(argv[first], "--double")) {
        if (argc != first + 2 || text(pixels, argv[first+1], 0, 0, 2, 2)) return 2;
    } else if (!strcmp(argv[first], "--progress")) {
        if (argc != first + 3) return 2;
        char *end;
        unsigned long done = strtoul(argv[first+1], &end, 10);
        if (!*argv[first+1] || *end) return 2;
        unsigned long total = strtoul(argv[first+2], &end, 10);
        if (!*argv[first+2] || *end || (total != 6 && total != 8) || done > total) return 2;
        int fill = (int)(70 * done / total);
        for (int y=0; y<8; y++) for (int x=0; x<72; x++)
            pixels[y*72+x] = y==0 || y==7 || x==0 || x==71 || x<=fill;
        char count[12]; snprintf(count, sizeof(count), "%lu/%lu", done, total);
        text(pixels, count, 0, 16, 1, 1);
    } else if (!strcmp(argv[first], "--stock")) {
#if defined(HDR_EXPERIMENTAL_DRIVER) && !defined(HDR_NO_STOCK_MIRROR)
        if (argc != first + 2 || stock_pixels(argv[first + 1], pixels)) {
            fprintf(stderr, "Stock screen unavailable or firmware signature mismatch\n"); return 1;
        }
#else
        fprintf(stderr, "Stock screen unavailable in this build\n"); return 3;
#endif
    } else if (!strcmp(argv[first], "--raw")) {
        if (argc != first + 2) return 2;
        FILE *f = fopen(argv[first + 1], "rb");
        if (!f) { perror("raw bitmap"); return 1; }
        size_t count = fread(pixels, 1, sizeof(pixels), f);
        int extra = fgetc(f);
        fclose(f);
        if (count != sizeof(pixels) || extra != EOF) return 2;
        for (int i = 0; i < 2304; i++) if (pixels[i] > 1) return 2;
    } else for (int row = 0; first + row < argc; row++) {
        const char *line = argv[first + row];
        if (strlen(line) > 12) { fprintf(stderr, "Line exceeds 12 columns\n"); return 2; }
        if (text(pixels, line, 0, row*8, 1, 1)) return 2;
    }
    if (write_device) {
#ifdef HDR_EXPERIMENTAL_DRIVER
        struct drawing draw = {{0,31,0,71}, {0}};
        memcpy(draw.pixels, pixels, sizeof(pixels));
        int fd = open("/dev/ssd1309-ctrl", O_RDWR);
        if (fd < 0) { perror("display open"); return 1; }
        int rc = ioctl(fd, 0x49105302UL, &draw);
        if (rc == 0) rc = ioctl(fd, 0x40105304UL, &draw.rect);
        if (rc < 0) perror("display ioctl");
        if (rc > 0) fprintf(stderr, "Unexpected display ioctl result: %d\n", rc);
        close(fd); return rc != 0 ? 1 : 0;
#else
        fprintf(stderr, "Device writer is disabled in this build\n"); return 3;
#endif
    }
    puts("P1\n72 32");
    for (int y = 0; y < 32; y++) {
        for (int x = 0; x < 72; x++) printf("%d ", pixels[y*72+x] ? 0 : 1);
        putchar('\n');
    }
    return ferror(stdout) ? 1 : 0;
}
