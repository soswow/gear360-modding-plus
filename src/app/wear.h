/* The built-in flash's own wear estimate, read from its eMMC register EXT_CSD.
 *
 * The kernel shows EXT_CSD through debugfs as 512 bytes in hex:
 * DIR/mmc0:RCA/ext_csd (DIR is /sys/kernel/debug/mmc0 on the camera; mmc0 is
 * the eMMC, mmc1 the card, which has no EXT_CSD). Reading it sends the chip
 * one read command (SEND_EXT_CSD); nothing is written.
 *
 * Bytes (JEDEC eMMC 5.0):
 *   267 PRE_EOL_INFO   1 normal, 2 warning (80% of spare blocks used), 3 urgent
 *   268 DEVICE_LIFE_TIME_EST_TYP_A   life used of the SLC-type memory
 *   269 DEVICE_LIFE_TIME_EST_TYP_B   life used of the MLC-type memory
 *       (the main storage, where /opt/usr is)
 * Life values come in 10% steps: 1 is 0-10% used, 2 is 10-20% ... 10 is
 * 90-100%, 11 is past the estimated life; 0 means not reported. */
#ifndef G360_APP_WEAR_H
#define G360_APP_WEAR_H
#ifdef __cplusplus
extern "C" {
#endif
#define G360_WEAR_DIR "/sys/kernel/debug/mmc0"
/* The app warns from this life value on: 4 is 30-40% used. */
#define G360_WEAR_WARNING 4

typedef struct g360_wear {
    int pre_eol;
    int life_a;
    int life_b;
} g360_wear;

/* Reads the three values. Returns 0, or -1 (all three set to -1) if the
 * file is missing, short, not hex or out of range. */
int g360_wear_read(const char *dir, g360_wear *wear);
/* The larger of the two life values; 0 when neither is reported. */
int g360_wear_level(const g360_wear *wear);
#ifdef __cplusplus
}
#endif
#endif
