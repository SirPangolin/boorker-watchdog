/**
 * @file display_logo.c
 * @brief Millie (Boorker mascot) logo as XBM bitmap
 *
 * 32x32 pixel monochrome bitmap of a dog face:
 * - Pointed ears (triangles)
 * - Hexagonal head shape
 * - Heterochromia: left eye has partial fill (9-12 o'clock wedge),
 *   right eye is solid filled
 * - Dark pupils, nose triangle, lighter snout area
 *
 * XBM format: row-major, LSB first, each row padded to byte boundary.
 * 32x32 / 8 = 128 bytes.
 */

#include "sdkconfig.h"

#if CONFIG_STATUS_DISPLAY_ENABLED

#include <stdint.h>

const int millie_logo_width = 32;
const int millie_logo_height = 32;

/*
 * Millie 32x32 — designed for 1-bit monochrome OLED
 *
 * Visual guide (approximate):
 *   Row 0-3:   Ear tips
 *   Row 4-7:   Ears widening
 *   Row 8-11:  Head top, ears merge into head
 *   Row 12-15: Eyes region (left=heterochromia outline+wedge, right=solid)
 *   Row 16-19: Below eyes, snout starts
 *   Row 20-23: Snout, nose
 *   Row 24-27: Lower jaw / chin
 *   Row 28-31: Neck
 */
const uint8_t millie_logo_xbm[] = {
    // Row 0-3: ear tips
    0x00, 0x02, 0x00, 0x40,  // .......x.................x......
    0x00, 0x07, 0x00, 0xE0,  // .....xxx...............xxx......
    0x00, 0x0F, 0x00, 0xF0,  // ....xxxx..............xxxx......
    0x80, 0x0F, 0x00, 0xF8,  // ...xxxxx.............xxxxx......
    // Row 4-7: ears widening into head
    0xC0, 0x1F, 0x00, 0xFC,  // ..xxxxxxx...........xxxxxxx.....
    0xE0, 0x1F, 0x80, 0xFC,  // .xxxxxxxx..........xxxxxxxx.....
    0xF0, 0x3F, 0xC0, 0xFD,  // xxxxxxxxxx........xxxxxxxxxx....
    0xF0, 0xFF, 0xE0, 0xFF,  // xxxxxxxxxxxx.....xxxxxxxxxxxxx..
    // Row 8-11: head fills in
    0xF8, 0xFF, 0xFF, 0xFF,  // xxxxxxxxxxxxxxxxxxxxxxxxxxxxxx..
    0xF8, 0xFF, 0xFF, 0xFF,  // xxxxxxxxxxxxxxxxxxxxxxxxxxxxxx..
    0xFC, 0xFF, 0xFF, 0xFF,  // xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx.
    0xFC, 0xFF, 0xFF, 0xFF,  // xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx.
    // Row 12-15: eye region
    0xFC, 0xC7, 0xF8, 0xFF,  // xxxxxxx...xxxx..xxxxxxxxxxxxx...
    0xFC, 0x83, 0xE0, 0xFF,  // xxxxxxx....xxx...xxxxxxxxxxxx...
    0xFC, 0x93, 0xE0, 0xFF,  // xxxxxxx.x..xxx...xxxxxxxxxxxx...
    0xFC, 0xC7, 0xF8, 0xFF,  // xxxxxxx...xxxx..xxxxxxxxxxxxx...
    // Row 16-19: below eyes, snout area
    0xFC, 0xFF, 0xFF, 0xFF,  // xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx.
    0xFC, 0xFF, 0xFF, 0xFF,  // xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx.
    0xF8, 0xFF, 0xFF, 0xFF,  // xxxxxxxxxxxxxxxxxxxxxxxxxxxxxx..
    0xF8, 0xFF, 0xFF, 0x7F,  // xxxxxxxxxxxxxxxxxxxxxxxxxxxxxx..
    // Row 20-23: snout narrows, nose
    0xF0, 0xFF, 0xFF, 0x3F,  // xxxxxxxxxxxxxxxxxxxxxxxxxxxx....
    0xE0, 0xFF, 0xFF, 0x1F,  // xxxxxxxxxxxxxxxxxxxxxxxxxxx.....
    0xC0, 0xFF, 0x9F, 0x0F,  // xxxxxxxxx..xxxxxxxxxxxxxxxxx....
    0x80, 0xFF, 0x07, 0x07,  // xxxxxxxx....xxxxxxxxxxxxxx......
    // Row 24-27: chin / lower jaw
    0x00, 0xFF, 0x83, 0x03,  // .xxxxxxx......xxxxxxxxxx........
    0x00, 0xFE, 0xC1, 0x01,  // ..xxxxxxx......xxxxxxxxx........
    0x00, 0xFC, 0xE0, 0x00,  // ...xxxxxx.......xxxxxxx.........
    0x00, 0x78, 0x70, 0x00,  // ....xxxx.........xxxx...........
    // Row 28-31: neck
    0x00, 0x30, 0x38, 0x00,  // .....xx...........xxx...........
    0x00, 0x00, 0x1C, 0x00,  // .......................xx........
    0x00, 0x00, 0x00, 0x00,  // ................................
    0x00, 0x00, 0x00, 0x00,  // ................................
};

#endif /* CONFIG_STATUS_DISPLAY_ENABLED */
