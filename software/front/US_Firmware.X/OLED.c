#define _OLED_C

#include "main.h"
#include "mcu.h"
#include "font32x48numbers.h"
#include "fonts.h"
#include "font6x8.h"
#include "font8x16.h"
#include "OLED.h"

/**
 * @file
 * Framebuffer rendering and polled SPI output for the 128x64 OLED.
 *
 * Page APIs use pixel columns (0..127) and 8-pixel page rows (0..7); XY APIs use
 * pixel coordinates. Drawing is not clipped: callers must keep every touched
 * byte inside OLEDBUFF, including the extra page used by unaligned XY writes.
 * Font glyphs contain width * height bytes, where height counts 8-pixel pages.
 * Rendering modifies RAM only; OLEDUpdate() sends it. Foreground ownership is
 * required for the shared framebuffer, command buffer, and SPI control pins.
 */

#define CmdNOP 0xE3
#define CmdRot0 0xC0A0
#define CmdRot180 0xC8A1
#define CmdCol 0x21
#define CmdRow 0x22
#define CmdBri 0x81

volatile displaysetup_t DisplaySetup = {0, 0};

const unsigned char OLEDInitBuff1[] = {
    0xAE, //display control ([AE] OFF, AF ON)
    0xD5,
    0x80, //set clock & divide ratio [50]
    0xA8,
    0x3F, //set MUX ratio [3F]
    0xD3,
    0x00, //set display offset [00]
    0x40, //set display start line [40]
};

const unsigned char OLEDInitBuff2[] = {

    0xDA,
    0x12, //set COM hardware config (02 sequential, [12] alternative)
    0xD9,
    0x22, //set precharge period [22]
    0xDB,
    0x35, //set VCOM deselect level [35] //020
    0xA4, //entire display on ([A4] normal, A5 entire on)
    0xA6, //normal/reversed display ([A6] normal, A7 reversed)
    0xAF, //display ON
    //0x20,0x02,  //set page addressing mode //no need - state after reset
};

typedef union {
    struct __PACKED {
        struct __PACKED {
            UINT8 BriCmd;
            UINT8 Bri;
            UINT16 Rot;
        } PreDisplayUpdate;
        struct __PACKED {
            UINT8 Row;
            UINT8 ColHi;
            UINT8 ColLow;
        } PreRowUpdate;
    };
} preUpdate_t;

preUpdate_t PreUpdateBuff = {
    CmdBri, 225,
    CmdRot0,
    0xB0,
    0x10,
    0};

/**
 * @brief Power-sequence the panel, detect its straps, and clear/send the framebuffer.
 *
 * Detects SH1106 versus SSD1306 and internal charge-pump selection through the
 * CS/D-C pull resistors; configures the SH1106 column offset when needed.
 * @pre Early MCU GPIO setup complete; no concurrent SPI/display users.
 * @note Contains supply/reset delays and blocking SPI operations.
 */
void OLEDInit() {
    mcuSPIClose();
    OLED_VCC = 1; //Turn off display VCC
    OLED_VDD = 1; //Turn off display VDD
    OLED_RES = 0; //Reset display
    OLED_DC = 0;
    OLED_CS = 0;
    OLED_SCK = 0;
    OLED_SDI = 0;
    OLED_SDO = 0;
    OLED_RES_3S = 0;
    OLED_DC_3S = 0;
    OLED_CS_3S = 0;
    SDI_3S = 0;
    SDO_3S = 0;
    SCK_3S = 0;
    _delay_ms(100);

    OLED_CS_3S = 1; //Everything but reset - 3state
    OLED_DC_3S = 1;
    SDI_3S = 1;
    SDO_3S = 1;
    SCK_3S = 1;
    OLED_VDD = 0;   //Turn on display VDD
    OLED_CS_PU = 1; //Enable internal pull-up on CS in order to check if resistor between CS and GND is installed
    OLED_DC_PU = 1; //Enable internal pull-up on D/C in order to check if resistor between OLED_RES and OLED_DC is installed
    _delay_ms(100);

    DisplaySetup.SH1106 = !OLED_CS_IN;             //If CS pull-down resistor is installed, OLED_CS will be low => display controller is SH1106
    DisplaySetup.InternalChargePump = !OLED_DC_IN; //If resistor between OLED_DC and OLED-RES is installed, it will dominate and OLED_DC will be low => display uses internal charge pump.

    OLED_CS_PU = 0; //Bring all IOs to operational state (outputs, no internal pull-ups)
    OLED_DC_PU = 0;
    OLED_CS_3S = 0;
    OLED_DC_3S = 0;
    SCK_3S = 0;
    SDI_3S = 0;
    SDO_3S = 0;
    OLED_CS = 1;
    OLED_DC = 1;
    _delay_ms(10);

    SDI_OUT = 0;
    SDO_OUT = 0;
    OLED_RES = 1;
    mcuSPIOpen();
    _delay_ms(1);
    OLED_VCC = 0; ///turn on display VCC
    _delay_ms(100);

    OLED_DC = 0;
    OLED_CS = 0;
    _delay_ms(10);

    mcuSPISendBytes((unsigned int *)OLEDInitBuff1, sizeof(OLEDInitBuff1));
    mcuSPIWait();
    if (DisplaySetup.SH1106) {
        PreUpdateBuff.PreRowUpdate.ColLow = 2; //SSH1106 is 131x64 - shift right by 2
        mcuSPISendByte(0xAD);                  //charge pump control
        mcuSPISendByte(DisplaySetup.InternalChargePump ? 0x8B : 0x8A);
    } else {
        PreUpdateBuff.PreRowUpdate.ColLow = 0; //SSD1306 is 128x64 - no need to shift
        mcuSPISendByte(0x8D);                  //charge pump control
        mcuSPISendByte(DisplaySetup.InternalChargePump ? 0x14 : 0x10);
    }
    mcuSPIWait();
    mcuSPISendBytes((unsigned int *)OLEDInitBuff2, sizeof(OLEDInitBuff2));

    OLEDFill(0, 128, 0, 8, 0);
    OLEDUpdate();
}

//void OLEDUpdateDMA(){
//    mcuSPIWait();
//    mcuSPIStop();
//    PreUpdateBuff.PreDisplayUpdate.Bri = (pars.Bri << 4) - 15;
//    PreUpdateBuff.PreDisplayUpdate.Rot = pars.DispRot ? CmdRot180 : CmdRot0;
//    OLED_DC = 0;
//    OLED_CS = 0;
//    mcuSPISendBytes((int*)&PreUpdateBuff, sizeof(PreUpdateBuff));
//    mcuSPIWait();
//    OLED_DC = 1;
//    mcuSPISendAuto(OLEDBUFF.B[0],128*8);
//}

/**
 * @brief Send brightness, rotation, and all eight framebuffer pages over SPI.
 * @pre pars.Bri/DispRot initialized to valid settings; display/SPI initialized.
 * @note Serializes command/data transitions by waiting for SPI, but does not
 *       explicitly wait for the final page's last byte before returning. Keep
 *       framebuffer data stable until its transfer has been consumed.
 */
void OLEDUpdate() {
    UINT8 r;
    mcuSPIWait();
    OLED_DC = 0;
    OLED_CS = 0;
    PreUpdateBuff.PreDisplayUpdate.Bri = (pars.Bri << 4) - 15;
    PreUpdateBuff.PreDisplayUpdate.Rot = pars.DispRot ? CmdRot180 : CmdRot0;
    mcuSPISendBytes((unsigned int *)&PreUpdateBuff.PreDisplayUpdate, sizeof(PreUpdateBuff.PreDisplayUpdate));
    for (r = 0; r <= 7; r++) {
        PreUpdateBuff.PreRowUpdate.Row = 0xB0 + r;
        mcuSPIWait();
        OLED_DC = 0;
        OLED_CS = 0;
        mcuSPISendBytes((unsigned int *)&PreUpdateBuff.PreRowUpdate, sizeof(PreUpdateBuff.PreRowUpdate));
        mcuSPIWait();
        OLED_DC = 1;
        mcuSPISendBytes((unsigned int *)OLEDBUFF.B[r], 128);
    }
}

/**
 * @brief Fill a rectangular range of page bytes with the repeated pattern b.
 * @param[in] col First pixel column; colnum is its nonnegative width in bytes.
 * @param[in] row First 8-pixel page; rownum is the nonnegative number of pages.
 * @pre The entire rectangle fits OLEDBUFF; no clipping or argument checks occur.
 */
void OLEDFill(int col, int colnum, int row, int rownum, UINT8 b) {
    int cc;
    while (rownum--) {
        cc = colnum;
        while (cc--)
            OLEDBUFF.B[row][col++] = b;
        col -= colnum;
        row++;
    }
}

/**
 * @brief Set/clear a pixel rectangle in the framebuffer, with no clipping.
 * @param[in] x,y Pixel origin; dx/dy are nonnegative pixel extents.
 * @param[in] b Nonzero sets selected bits; zero selects the clearing path.
 * @pre All touched bytes fit OLEDBUFF.
 * @warning The unaligned clearing path currently uses logical !mask and clears
 *          whole touched bytes. It does not preserve pixels outside the rectangle;
 *          use page-aligned clearing when those neighboring pixels must survive.
 */
void OLEDFillXY(int x, int dx, int y, int dy, int b) {
    if (!(y & 7) && !(dy & 7))
        return OLEDFill(x, dx, y >> 3, dy >> 3, b ? 255 : 0);
    while (dy > 0) {
        int row7 = y & 7;
        int rowL = 8 - row7;
        UINT8 mask = 0xFF << row7;
        if (dy < rowL)
            mask &= 0xFF >> (rowL - dy);
        int cx = x;
        int cc = dx;
        if (b) {
            while (cc--)
                OLEDBUFF.B[y >> 3][cx++] |= mask;
        } else {
            mask = !mask;
            while (cc--)
                OLEDBUFF.B[y >> 3][cx++] &= mask;
        }
        dy -= rowL;
        y += rowL;
    }
}

/**
 * @brief XOR a page-aligned rectangle with 0xFF in the framebuffer.
 * @pre Nonnegative colnum/rownum; column/page rectangle fits OLEDBUFF.
 */
void OLEDInvert(int col, int colnum, int row, int rownum) {
    int cc;
    while (rownum--) {
        for (cc = colnum; cc--;)
            OLEDBUFF.B[row][col++] ^= 255;
        col -= colnum;
        row++;
    }
}

/**
 * @brief XOR a pixel rectangle, preserving other bits in partially covered pages.
 * @pre Nonnegative dx/dy; pixel rectangle fits the 128x64 framebuffer.
 */
void OLEDInvertXY(int x, int dx, int y, int dy) {
    if (!(y & 7) && !(dy & 7))
        return OLEDInvert(x, dx, y >> 3, dy >> 3);
    while (dy > 0) {
        int row7 = y & 7;
        int rowL = 8 - row7;
        UINT8 mask = 0xFF << row7;
        if (dy < rowL)
            mask &= 0xFF >> (rowL - dy);
        int cx = x;
        int cc = dx;
        while (cc--)
            OLEDBUFF.B[y >> 3][cx++] ^= mask;
        dy -= rowL;
        y += rowL;
    }
}

/**
 * @brief Copy page-organized bitmap bytes, wrapping every colnum columns.
 * @param[in] col,row Destination pixel column and 8-pixel page index.
 * @param[in] colnum Positive bitmap row width in bytes.
 * @param[in] buf Readable source of at least num bytes; not modified.
 * @param[in] num Nonnegative byte count; zero is a no-op.
 * @pre All destination pages fit OLEDBUFF; no clipping or overlap handling.
 */
void OLEDWrite(int col, int colnum, int row, void *buf, int num) {
    while (num) {
        int cc = colnum;
        while (num && cc--) {
            OLEDBUFF.B[row][col++] = *(UINT8 *)buf;
            buf = &((char *)buf)[1];
            num--;
        }
        col -= colnum;
        row++;
    }
}

/**
 * @brief Copy page-organized bitmap data at a pixel origin, splitting page edges.
 * @param[in] x,y Pixel coordinates; colnum is the positive source row width.
 * @param[in] buf Readable source of num bytes; num must be nonnegative.
 * @pre Destination fits OLEDBUFF, including the extra page touched for y % 8 != 0.
 * @note Preserves uncovered boundary bits; aligned y delegates to OLEDWrite().
 */
void OLEDWriteXY(int x, int colnum, int y, void *buf, int num) {
    if (!(y & 7))
        return OLEDWrite(x, colnum, y >> 3, buf, num);
    int row70 = y & 7;
    int row71 = 8 - row70;
    UINT8 mask0 = 0xFF >> row71;
    UINT8 mask1 = 0xFF << row70;
    while (num) {
        int cc = colnum;
        int buffRow = y >> 3;
        while (num && cc--) {
            UINT8 b = (*(UINT8 *)buf);
            if (row70) {
                OLEDBUFF.B[buffRow][x] &= mask0;
                OLEDBUFF.B[buffRow][x] |= b << row70;
                OLEDBUFF.B[buffRow + 1][x] &= mask1;
                OLEDBUFF.B[buffRow + 1][x++] |= b >> row71;
            } else {
                OLEDBUFF.B[buffRow][x] = b;
            }
            buf = &((char *)buf)[1];
            num--;
        }
        x -= colnum;
        y += 8;
    }
}

/**
 * @brief Render the last three decimal digits using 32x48 glyphs, blanking leading zeros.
 * @pre num is nonnegative; destination is a 96-column, six-page field that fits.
 * @note col is the left pixel column, row the first page; zero still draws one digit.
 */
void OLEDPrintNum3248(int col, int row, int num) {
    int i, cd;
    col += 64;
    i = 3;
    while (i--) {
        cd = num % 10;
        num /= 10;
        if (num || cd || (i > 1)) {
            OLEDWrite(col, 32, row, (void *)numbers32x48[cd], 192);
        } else {
            OLEDFill(col, 32, row, 6, 0);
        }
        col -= 32;
    }
}
/**
 * @brief Draw the large C/F unit glyph selected by CF (0 for C, 1 for F).
 * @pre The 16-column, five-page destination and glyph index must be valid.
 * @note Despite the legacy name, this implementation copies five 8-pixel pages.
 */
void OLEDPrintCF1648(int col, int row, int CF) {
    CF += 10;
    OLEDWrite(col, 16, row++, (void *)&numbers32x48[CF][0], 16);
    OLEDWrite(col, 16, row++, (void *)&numbers32x48[CF][32], 16);
    OLEDWrite(col, 16, row++, (void *)&numbers32x48[CF][64], 16);
    OLEDWrite(col, 16, row++, (void *)&numbers32x48[CF][96], 16);
    OLEDWrite(col, 16, row++, (void *)&numbers32x48[CF][128], 16);
}

/**
 * @brief Draw a right-aligned decimal integer in a fixed-width page-based field.
 * @param[in] dec Positive number of character positions, not decimal places.
 * @param[in] num Value whose magnitude is representable as a positive int.
 * @param[in] font Glyph table containing digits and '-' as needed; startChar is
 *                 the table's first character code, width/height its byte/page size.
 * @param[in] blank Nonnegative spacing in pixels after each glyph.
 * @pre Column/page destination fits; reserve a spare position for a negative sign.
 * @note Excess high digits are discarded; leading positions are cleared.
 */
void OLEDPrintNum(int col, int row, int dec, int num, void *font, int startChar, int width, int height, int blank) {
    int cb = width * height;
    int cw = width + blank;
    int i, cd;
    int minus = 0;
    if (num < 0) {
        num = -num;
        minus = 1;
    }
    i = dec;
    dec--;
    col += cw * dec;
    if (num < 0)
        num = -num;
    while (i--) {
        cd = num % 10;
        num /= 10;
        if (num || cd || (i == dec)) {
            OLEDWrite(col, width, row, &((char *)font)[cb * (cd + 0x30 - startChar)], cb);
            OLEDFill(col + width, blank, row, height, 0);
        } else {
            if (minus) {
                OLEDWrite(col, width, row, &((char *)font)[cb * ('-' - startChar)], cb);
                OLEDFill(col + width, blank, row, height, 0);
                minus = 0;
            } else {
                OLEDFill(col, cw, row, height, 0);
            }
        }
        col -= cw;
    }
}

/**
 * @brief Draw decimal digits at pixel coordinates with OLEDPrintNum()'s font layout.
 * @pre dec > 0; magnitude fits int; all glyph and spacing bytes fit OLEDBUFF,
 *      including split-page writes.
 * @warning The minus/leading-padding branches still call page-based helpers
 *          with pixel y. Those branches are not correct for arbitrary pixel y;
 *          the digit and spacing path also inherits OLEDFillXY()'s clearing limit.
 */
void OLEDPrintNumXY(int x, int y, int dec, int num, void *font, int startChar, int width, int height, int blank) {
    int cb = width * height;
    int cw = width + blank;
    int i, cd;
    int minus = 0;
    if (num < 0) {
        num = -num;
        minus = 1;
    }
    i = dec;
    dec--;
    x += cw * dec;
    if (num < 0)
        num = -num;
    while (i--) {
        cd = num % 10;
        num /= 10;
        if (num || cd || (i == dec)) {
            OLEDWriteXY(x, width, y, &((char *)font)[cb * (cd + 0x30 - startChar)], cb);
            OLEDFillXY(x + width, blank, y, height << 3, 0);
        } else {
            if (minus) {
                OLEDWrite(x, width, y, &((char *)font)[cb * ('-' - startChar)], cb);
                OLEDFill(x + width, blank, y, height << 3, 0);
                minus = 0;
            } else {
                OLEDFill(x, cw, y, height << 3, 0);
            }
        }
        x -= cw;
    }
}
/**
 * @brief Draw exactly dec uppercase hexadecimal digits, including leading zeros.
 * @pre dec > 0; column/page destination fits; glyph table includes 0..9 and A..F.
 * @note Uses OLEDPrintNum()'s font geometry; excess high-order nibbles are discarded.
 */
void OLEDPrintHex(int col, int row, int dec, unsigned int num, void *font, int startChar, int width, int height, int blank) {
    int cb = width * height;
    int cw = width + blank;
    unsigned int i, cd;
    i = dec;
    dec--;
    col += cw * dec;
    if (num < 0)
        num = -num;
    while (i--) {
        cd = num & 15;
        num >>= 4;
        OLEDWrite(col, width, row, &((char *)font)[cb * (cd + (cd <= 9 ? 0x30 : 0x37) - startChar)], cb);
        OLEDFill(col + width, blank, row, height, 0);
        col -= cw;
    }
}

/**
 * @brief Draw text at a pixel column/page row until NUL or the character limit.
 * @param[in] s Readable characters present in the supplied font table.
 * @param[in] num Nonnegative limit; zero means at most 128 characters, not unlimited.
 * @pre Source is readable until NUL/limit, font geometry follows OLEDPrintNum(),
 *      and the complete destination fits OLEDBUFF. No glyph-index bounds checks.
 */
void OLEDPrint(int col, int row, const char *s, int num, void *font, int startChar, int width, int height, int blank) {
    int cb = width * height;
    if (num == 0)
        num = 128;
    while (num--) {
        if (s[0] == 0)
            break;
        OLEDWrite(col, width, row, &((char *)font)[cb * (s[0] - startChar)], cb);
        col += width;
        OLEDFill(col, blank, row, height, 0);
        col += blank;
        s++;
    }
}

/**
 * @brief Draw bounded text at pixel coordinates using OLEDPrint()'s font contract.
 * @pre All glyphs, spacing, and split-page writes fit OLEDBUFF; num >= 0.
 * @note Zero num means a 128-character cap. Unaligned spacing clears inherit
 *       OLEDFillXY()'s whole-byte clearing behavior.
 */
void OLEDPrintXY(int x, int y, const char *s, int num, void *font, int startChar, int width, int height, int blank) {
    int cb = width * height;
    if (num == 0)
        num = 128;
    while (num--) {
        if (s[0] == 0)
            break;
        OLEDWriteXY(x, width, y, &((char *)font)[cb * (s[0] - startChar)], cb);
        x += width;
        OLEDFillXY(x, blank, y, height << 3, 0);
        x += blank;
        s++;
    }
}
#undef _OLED_C
