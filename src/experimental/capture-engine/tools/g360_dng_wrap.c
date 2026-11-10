/*
 * g360-dng-wrap: wrap a 33,732,608-byte SM-R210 still RAW raster in an
 * uncompressed DNG without changing the samples.
 *
 *   g360-dng-wrap --input dump.raw --output dump.dng [--black 4105]
 *                 [--neutral-red 0.544 --neutral-blue 0.578]
 */
#define _POSIX_C_SOURCE 200809L
#include "raw_format.h"

#include <getopt.h>
#include <stdio.h>
#include <stdlib.h>

static void usage(FILE *stream) {
    fputs("usage: g360-dng-wrap --input RAW --output DNG [--black N] "
          "[--neutral-red R] [--neutral-blue B]\n",
          stream);
}

int main(int argc, char **argv) {
    const char *input = NULL;
    const char *output = NULL;
    g360_dng_parameters parameters = {0, 1.0, 1.0, NULL};
    static const struct option options[] = {
        {"input", required_argument, NULL, 'i'},
        {"output", required_argument, NULL, 'o'},
        {"black", required_argument, NULL, 'b'},
        {"neutral-red", required_argument, NULL, 'r'},
        {"neutral-blue", required_argument, NULL, 'u'},
        {"help", no_argument, NULL, 'h'},
        {NULL, 0, NULL, 0},
    };
    int option;
    while ((option = getopt_long(argc, argv, "i:o:b:r:u:h", options, NULL)) != -1) {
        switch (option) {
        case 'i': input = optarg; break;
        case 'o': output = optarg; break;
        case 'b': parameters.black_level = (uint32_t)strtoul(optarg, NULL, 0); break;
        case 'r': parameters.neutral_red = strtod(optarg, NULL); break;
        case 'u': parameters.neutral_blue = strtod(optarg, NULL); break;
        case 'h': usage(stdout); return 0;
        default: usage(stderr); return 2;
        }
    }
    if (!input || !output) {
        usage(stderr);
        return 2;
    }

    FILE *source = fopen(input, "rb");
    if (!source) {
        perror(input);
        return 1;
    }
    uint8_t *raster = malloc(G360_RAW_BYTES + 1);
    size_t length = raster ? fread(raster, 1, G360_RAW_BYTES + 1, source) : 0;
    fclose(source);
    g360_raw_statistics statistics;
    if (!raster || g360_raw_scan(raster, length, &statistics) != 0) {
        fprintf(stderr, "%s: expected exactly %zu bytes\n", input, G360_RAW_BYTES);
        free(raster);
        return 1;
    }
    if (statistics.misaligned_samples) {
        fprintf(stderr, "warning: %zu samples have low bits set; layout may differ\n",
                statistics.misaligned_samples);
    }

    uint8_t header[G360_DNG_HEADER_CAPACITY];
    size_t header_length = g360_dng_write_header(header, sizeof(header), &parameters);
    FILE *target = header_length ? fopen(output, "wb") : NULL;
    int ok = target && fwrite(header, 1, header_length, target) == header_length &&
             fwrite(raster, 1, G360_RAW_BYTES, target) == G360_RAW_BYTES;
    if (target && fclose(target) != 0) {
        ok = 0;
    }
    free(raster);
    if (!ok) {
        perror(output);
        return 1;
    }
    printf("wrote %s (header %zu bytes, samples min %u max %u)\n", output, header_length,
           statistics.minimum, statistics.maximum);
    return 0;
}
