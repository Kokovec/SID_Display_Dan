#include "pico/stdlib.h"
#include "hardware/spi.h"
#include "hardware/i2c.h"
#include "hardware/uart.h"
#include "hardware/irq.h"
#include <string.h>
#include <stdio.h>
#include "ff.h"
#include "sid_comms.h"

// --- Pin definitions ---
#define LCD_SCLK    10
#define LCD_MOSI    11
#define LCD_MISO    12
#define LCD_CS       9
#define LCD_DC      14
#define LCD_RST     13
#define LCD_BL      15
#define SD_CS        4

#define SID_UART        uart0
#define SID_UART_TX     0
#define SID_UART_RX     1

#define TP_SDA       6
#define TP_SCL       7
#define TP_RST      16
#define TP_INT      17
#define TP_I2C      i2c1
#define TP_ADDR     0x15

#define LCD_WIDTH   320
#define LCD_HEIGHT  240
#define SPI_PORT    spi1

// --- Low-level LCD helpers ---

static inline void lcd_cmd(uint8_t cmd) {
    gpio_put(LCD_DC, 0);
    gpio_put(LCD_CS, 0);
    spi_write_blocking(SPI_PORT, &cmd, 1);
    gpio_put(LCD_CS, 1);
}

static inline void lcd_data8(uint8_t data) {
    gpio_put(LCD_DC, 1);
    gpio_put(LCD_CS, 0);
    spi_write_blocking(SPI_PORT, &data, 1);
    gpio_put(LCD_CS, 1);
}

static void lcd_reset(void) {
    gpio_put(LCD_RST, 1); sleep_ms(100);
    gpio_put(LCD_RST, 0); sleep_ms(100);
    gpio_put(LCD_RST, 1); sleep_ms(200);
}

static void lcd_init(void) {
    lcd_reset();
    lcd_cmd(0x11);      // SLPOUT
    sleep_ms(120);
    lcd_cmd(0x3A);      // COLMOD: RGB565
    lcd_data8(0x55);
    lcd_cmd(0x36);      // MADCTL: 90° CW landscape (MX|MV)
    lcd_data8(0x60);
    lcd_cmd(0x21);      // INVON
    sleep_ms(10);
    lcd_cmd(0x13);      // NORON
    sleep_ms(10);
    lcd_cmd(0x29);      // DISPON
    sleep_ms(50);
}

static void lcd_set_window(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1) {
    lcd_cmd(0x2A);
    lcd_data8(x0 >> 8); lcd_data8(x0 & 0xFF);
    lcd_data8(x1 >> 8); lcd_data8(x1 & 0xFF);
    lcd_cmd(0x2B);
    lcd_data8(y0 >> 8); lcd_data8(y0 & 0xFF);
    lcd_data8(y1 >> 8); lcd_data8(y1 & 0xFF);
    lcd_cmd(0x2C);
}

static void lcd_fill(uint16_t colour) {
    uint8_t hi = colour >> 8;
    uint8_t lo = colour & 0xFF;

    lcd_cmd(0x2A);
    lcd_data8(0x00); lcd_data8(0x00);
    lcd_data8((LCD_WIDTH  - 1) >> 8); lcd_data8((LCD_WIDTH  - 1) & 0xFF);
    lcd_cmd(0x2B);
    lcd_data8(0x00); lcd_data8(0x00);
    lcd_data8((LCD_HEIGHT - 1) >> 8); lcd_data8((LCD_HEIGHT - 1) & 0xFF);
    lcd_cmd(0x2C);

    uint8_t buf[LCD_WIDTH * 2];
    for (int i = 0; i < LCD_WIDTH; i++) {
        buf[i * 2]     = hi;
        buf[i * 2 + 1] = lo;
    }
    gpio_put(LCD_DC, 1);
    gpio_put(LCD_CS, 0);
    for (int row = 0; row < LCD_HEIGHT; row++) {
        spi_write_blocking(SPI_PORT, buf, sizeof(buf));
    }
    gpio_put(LCD_CS, 1);
}

// --- BMP helpers ---

typedef struct {
    uint32_t data_off;
    uint16_t width;
    uint16_t height;
    uint16_t bpp;
    bool     top_down;
    uint32_t row_bytes;
} BmpInfo;

static bool read_bmp_info(FIL *fp, BmpInfo *info) {
    uint8_t hdr[54];
    UINT br;
    f_lseek(fp, 0);
    f_read(fp, hdr, 54, &br);
    if (br < 54 || hdr[0] != 'B' || hdr[1] != 'M') return false;

    info->data_off = (uint32_t)(hdr[10] | (hdr[11]<<8) | (hdr[12]<<16) | (hdr[13]<<24));

    int32_t w = (int32_t)(hdr[18] | (hdr[19]<<8) | (hdr[20]<<16) | (hdr[21]<<24));
    int32_t h = (int32_t)(hdr[22] | (hdr[23]<<8) | (hdr[24]<<16) | (hdr[25]<<24));
    info->bpp      = (uint16_t)(hdr[28] | (hdr[29]<<8));
    info->top_down = (h < 0);
    if (h < 0) h = -h;
    info->width    = (uint16_t)w;
    info->height   = (uint16_t)h;
    info->row_bytes = ((w * (info->bpp / 8)) + 3) & ~3u;
    return true;
}

// --- Blended display ---

static uint8_t raw_bg[LCD_WIDTH * 2];
static uint8_t raw_fg[LCD_WIDTH * 2];
static uint8_t pxl   [LCD_WIDTH * 2];

static void display_blended(FIL *bg_fp, FIL *fg_fp) {
    BmpInfo bg, fg;
    if (!read_bmp_info(bg_fp, &bg) || bg.bpp != 16) { lcd_fill(0x07E0); return; }
    if (!read_bmp_info(fg_fp, &fg) || fg.bpp != 16) { lcd_fill(0x07E0); return; }

    uint16_t draw_w = bg.width  < LCD_WIDTH  ? bg.width  : LCD_WIDTH;
    uint16_t draw_h = bg.height < LCD_HEIGHT ? bg.height : LCD_HEIGHT;
    uint16_t x_off  = bg.width  < LCD_WIDTH  ? (LCD_WIDTH  - bg.width)  / 2 : 0;
    uint16_t y_off  = bg.height < LCD_HEIGHT ? (LCD_HEIGHT - bg.height) / 2 : 0;

    uint16_t fg_cols = fg.width  < draw_w ? fg.width  : draw_w;
    uint16_t fg_rows = fg.height < draw_h ? fg.height : draw_h;

    UINT br;
    for (uint16_t y = 0; y < draw_h; y++) {
        uint32_t bg_row = bg.top_down ? y : (uint32_t)(bg.height - 1 - y);
        f_lseek(bg_fp, bg.data_off + (FSIZE_t)bg_row * bg.row_bytes);
        f_read(bg_fp, raw_bg, draw_w * 2, &br);

        bool has_fg = (y < fg_rows);
        if (has_fg) {
            uint32_t fg_row = fg.top_down ? y : (uint32_t)(fg.height - 1 - y);
            f_lseek(fg_fp, fg.data_off + (FSIZE_t)fg_row * fg.row_bytes);
            f_read(fg_fp, raw_fg, fg_cols * 2, &br);
        }

        for (uint16_t x = 0; x < draw_w; x++) {
            uint16_t bg16 = (uint16_t)(raw_bg[x*2] | ((uint16_t)raw_bg[x*2+1] << 8));
            uint16_t px;

            if (has_fg && x < fg_cols) {
                uint16_t fg16 = (uint16_t)(raw_fg[x*2] | ((uint16_t)raw_fg[x*2+1] << 8));
                px = (fg16 != 0xFFFF) ? fg16 : bg16;
            } else {
                px = bg16;
            }

            pxl[x*2]     = (uint8_t)(px >> 8);
            pxl[x*2 + 1] = (uint8_t)(px & 0xFF);
        }

        lcd_set_window(x_off, y_off + y, x_off + draw_w - 1, y_off + y);
        gpio_put(LCD_DC, 1);
        gpio_put(LCD_CS, 0);
        spi_write_blocking(SPI_PORT, pxl, draw_w * 2);
        gpio_put(LCD_CS, 1);
    }
}

// --- Text rendering (8×8 bitmap font, printable ASCII 0x20–0x7E) ---

static const uint8_t font8x8[95][8] = {
  {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00}, // 0x20 space
  {0x18,0x3C,0x3C,0x18,0x18,0x00,0x18,0x00}, // !
  {0x36,0x36,0x00,0x00,0x00,0x00,0x00,0x00}, // "
  {0x36,0x36,0x7F,0x36,0x7F,0x36,0x36,0x00}, // #
  {0x0C,0x3E,0x03,0x1E,0x30,0x1F,0x0C,0x00}, // $
  {0x00,0x63,0x33,0x18,0x0C,0x66,0x63,0x00}, // %
  {0x1C,0x36,0x1C,0x6E,0x3B,0x33,0x6E,0x00}, // &
  {0x06,0x06,0x03,0x00,0x00,0x00,0x00,0x00}, // '
  {0x18,0x0C,0x06,0x06,0x06,0x0C,0x18,0x00}, // (
  {0x06,0x0C,0x18,0x18,0x18,0x0C,0x06,0x00}, // )
  {0x00,0x66,0x3C,0xFF,0x3C,0x66,0x00,0x00}, // *
  {0x00,0x0C,0x0C,0x3F,0x0C,0x0C,0x00,0x00}, // +
  {0x00,0x00,0x00,0x00,0x00,0x0C,0x0C,0x06}, // ,
  {0x00,0x00,0x00,0x3F,0x00,0x00,0x00,0x00}, // -
  {0x00,0x00,0x00,0x00,0x00,0x0C,0x0C,0x00}, // .
  {0x60,0x30,0x18,0x0C,0x06,0x03,0x01,0x00}, // /
  {0x3E,0x63,0x73,0x7B,0x6F,0x67,0x3E,0x00}, // 0
  {0x0C,0x0E,0x0C,0x0C,0x0C,0x0C,0x3F,0x00}, // 1
  {0x1E,0x33,0x30,0x1C,0x06,0x33,0x3F,0x00}, // 2
  {0x1E,0x33,0x30,0x1C,0x30,0x33,0x1E,0x00}, // 3
  {0x38,0x3C,0x36,0x33,0x7F,0x30,0x78,0x00}, // 4
  {0x3F,0x03,0x1F,0x30,0x30,0x33,0x1E,0x00}, // 5
  {0x1C,0x06,0x03,0x1F,0x33,0x33,0x1E,0x00}, // 6
  {0x3F,0x33,0x30,0x18,0x0C,0x0C,0x0C,0x00}, // 7
  {0x1E,0x33,0x33,0x1E,0x33,0x33,0x1E,0x00}, // 8
  {0x1E,0x33,0x33,0x3E,0x30,0x18,0x0E,0x00}, // 9
  {0x00,0x0C,0x0C,0x00,0x00,0x0C,0x0C,0x00}, // :
  {0x00,0x0C,0x0C,0x00,0x00,0x0C,0x0C,0x06}, // ;
  {0x18,0x0C,0x06,0x03,0x06,0x0C,0x18,0x00}, // <
  {0x00,0x00,0x3F,0x00,0x00,0x3F,0x00,0x00}, // =
  {0x06,0x0C,0x18,0x30,0x18,0x0C,0x06,0x00}, // >
  {0x1E,0x33,0x30,0x18,0x0C,0x00,0x0C,0x00}, // ?
  {0x3E,0x63,0x7B,0x7B,0x7B,0x03,0x1E,0x00}, // @
  {0x0C,0x1E,0x33,0x33,0x3F,0x33,0x33,0x00}, // A
  {0x3F,0x66,0x66,0x3E,0x66,0x66,0x3F,0x00}, // B
  {0x3C,0x66,0x03,0x03,0x03,0x66,0x3C,0x00}, // C
  {0x1F,0x36,0x66,0x66,0x66,0x36,0x1F,0x00}, // D
  {0x7F,0x46,0x16,0x1E,0x16,0x46,0x7F,0x00}, // E
  {0x7F,0x46,0x16,0x1E,0x16,0x06,0x0F,0x00}, // F
  {0x3C,0x66,0x03,0x03,0x73,0x66,0x7C,0x00}, // G
  {0x33,0x33,0x33,0x3F,0x33,0x33,0x33,0x00}, // H
  {0x1E,0x0C,0x0C,0x0C,0x0C,0x0C,0x1E,0x00}, // I
  {0x78,0x30,0x30,0x30,0x33,0x33,0x1E,0x00}, // J
  {0x67,0x66,0x36,0x1E,0x36,0x66,0x67,0x00}, // K
  {0x0F,0x06,0x06,0x06,0x46,0x66,0x7F,0x00}, // L
  {0x63,0x77,0x7F,0x7F,0x6B,0x63,0x63,0x00}, // M
  {0x63,0x67,0x6F,0x7B,0x73,0x63,0x63,0x00}, // N
  {0x1C,0x36,0x63,0x63,0x63,0x36,0x1C,0x00}, // O
  {0x3F,0x66,0x66,0x3E,0x06,0x06,0x0F,0x00}, // P
  {0x1E,0x33,0x33,0x33,0x3B,0x1E,0x38,0x00}, // Q
  {0x3F,0x66,0x66,0x3E,0x36,0x66,0x67,0x00}, // R
  {0x1E,0x33,0x07,0x0E,0x38,0x33,0x1E,0x00}, // S
  {0x3F,0x2D,0x0C,0x0C,0x0C,0x0C,0x1E,0x00}, // T
  {0x33,0x33,0x33,0x33,0x33,0x33,0x3F,0x00}, // U
  {0x33,0x33,0x33,0x33,0x33,0x1E,0x0C,0x00}, // V
  {0x63,0x63,0x63,0x6B,0x7F,0x77,0x63,0x00}, // W
  {0x63,0x63,0x36,0x1C,0x1C,0x36,0x63,0x00}, // X
  {0x33,0x33,0x33,0x1E,0x0C,0x0C,0x1E,0x00}, // Y
  {0x7F,0x63,0x31,0x18,0x4C,0x66,0x7F,0x00}, // Z
  {0x1E,0x06,0x06,0x06,0x06,0x06,0x1E,0x00}, // [
  {0x03,0x06,0x0C,0x18,0x30,0x60,0x40,0x00}, // backslash
  {0x1E,0x18,0x18,0x18,0x18,0x18,0x1E,0x00}, // ]
  {0x08,0x1C,0x36,0x63,0x00,0x00,0x00,0x00}, // ^
  {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0xFF}, // _
  {0x0C,0x0C,0x18,0x00,0x00,0x00,0x00,0x00}, // `
  {0x00,0x00,0x1E,0x30,0x3E,0x33,0x6E,0x00}, // a
  {0x07,0x06,0x06,0x3E,0x66,0x66,0x3B,0x00}, // b
  {0x00,0x00,0x1E,0x33,0x03,0x33,0x1E,0x00}, // c
  {0x38,0x30,0x30,0x3E,0x33,0x33,0x6E,0x00}, // d
  {0x00,0x00,0x1E,0x33,0x3F,0x03,0x1E,0x00}, // e
  {0x1C,0x36,0x06,0x0F,0x06,0x06,0x0F,0x00}, // f
  {0x00,0x00,0x6E,0x33,0x33,0x3E,0x30,0x1F}, // g
  {0x07,0x06,0x36,0x6E,0x66,0x66,0x67,0x00}, // h
  {0x0C,0x00,0x0E,0x0C,0x0C,0x0C,0x1E,0x00}, // i
  {0x30,0x00,0x30,0x30,0x30,0x33,0x33,0x1E}, // j
  {0x07,0x06,0x66,0x36,0x1E,0x36,0x67,0x00}, // k
  {0x0E,0x0C,0x0C,0x0C,0x0C,0x0C,0x1E,0x00}, // l
  {0x00,0x00,0x33,0x7F,0x7F,0x6B,0x63,0x00}, // m
  {0x00,0x00,0x1F,0x33,0x33,0x33,0x33,0x00}, // n
  {0x00,0x00,0x1E,0x33,0x33,0x33,0x1E,0x00}, // o
  {0x00,0x00,0x3B,0x66,0x66,0x3E,0x06,0x0F}, // p
  {0x00,0x00,0x6E,0x33,0x33,0x3E,0x30,0x78}, // q
  {0x00,0x00,0x3B,0x6E,0x66,0x06,0x0F,0x00}, // r
  {0x00,0x00,0x3E,0x03,0x1E,0x30,0x1F,0x00}, // s
  {0x08,0x0C,0x3E,0x0C,0x0C,0x2C,0x18,0x00}, // t
  {0x00,0x00,0x33,0x33,0x33,0x33,0x6E,0x00}, // u
  {0x00,0x00,0x33,0x33,0x33,0x1E,0x0C,0x00}, // v
  {0x00,0x00,0x63,0x6B,0x7F,0x7F,0x36,0x00}, // w
  {0x00,0x00,0x63,0x36,0x1C,0x36,0x63,0x00}, // x
  {0x00,0x00,0x33,0x33,0x33,0x3E,0x30,0x1F}, // y
  {0x00,0x00,0x3F,0x19,0x0C,0x26,0x3F,0x00}, // z
  {0x38,0x0C,0x0C,0x07,0x0C,0x0C,0x38,0x00}, // {
  {0x18,0x18,0x18,0x00,0x18,0x18,0x18,0x00}, // |
  {0x07,0x0C,0x0C,0x38,0x0C,0x0C,0x07,0x00}, // }
  {0x6E,0x3B,0x00,0x00,0x00,0x00,0x00,0x00}, // ~
};

// Draw one 8×8 character at pixel (x, y)
static void lcd_char(uint16_t x, uint16_t y, char c, uint16_t fg, uint16_t bg) {
    if ((uint8_t)c < 0x20 || (uint8_t)c > 0x7E) c = ' ';
    const uint8_t *gl = font8x8[(uint8_t)(c - 0x20)];
    uint8_t buf[8 * 8 * 2];
    uint16_t n = 0;
    for (int row = 0; row < 8; row++) {
        uint8_t bits = gl[row];
        for (int col = 0; col < 8; col++) {
            uint16_t px = (bits & (1 << col)) ? fg : bg;
            buf[n++] = (uint8_t)(px >> 8);
            buf[n++] = (uint8_t)(px & 0xFF);
        }
    }
    lcd_set_window(x, y, x + 7, y + 7);
    gpio_put(LCD_DC, 1);
    gpio_put(LCD_CS, 0);
    spi_write_blocking(SPI_PORT, buf, sizeof(buf));
    gpio_put(LCD_CS, 1);
}

// Draw a null-terminated string; each char is 8px wide
static void lcd_text(uint16_t x, uint16_t y, const char *s, uint16_t fg, uint16_t bg) {
    while (*s && x + 8 <= LCD_WIDTH) {
        lcd_char(x, y, *s++, fg, bg);
        x += 8;
    }
}

// Render song metadata as three lines at the top of the screen.
// Lines are padded to 40 chars so any previous text is fully overwritten.
static void display_metadata(const SidMeta *m) {
    char line[41];
    snprintf(line, sizeof(line), "%-40.40s", m->title);
    lcd_text(0,  2, line, 0xFFFF, 0x0000);          // white  — title

    snprintf(line, sizeof(line), "%-40.40s", m->author);
    lcd_text(0, 12, line, 0xFD20, 0x0000);          // orange — author

    snprintf(line, sizeof(line), "Song %d/%d  %-26.26s",
             m->song_num, m->song_count, m->released);
    lcd_text(0, 22, line, 0x07FF, 0x0000);          // cyan   — song# / year
}

// --- Touch (CST816S, I2C) ---

static void touch_init(void) {
    // Hardware reset
    gpio_init(TP_RST); gpio_set_dir(TP_RST, GPIO_OUT);
    gpio_put(TP_RST, 0); sleep_ms(10);
    gpio_put(TP_RST, 1); sleep_ms(50);

    // INT: active-low, pulled up externally on the module
    gpio_init(TP_INT); gpio_set_dir(TP_INT, GPIO_IN);
    gpio_pull_up(TP_INT);

    i2c_init(TP_I2C, 400000);
    gpio_set_function(TP_SDA, GPIO_FUNC_I2C);
    gpio_set_function(TP_SCL, GPIO_FUNC_I2C);
    gpio_pull_up(TP_SDA);
    gpio_pull_up(TP_SCL);
}

// Returns true if at least one finger is currently on the panel.
static bool finger_down(void) {
    uint8_t reg = 0x02;
    uint8_t count = 0;
    i2c_write_blocking(TP_I2C, TP_ADDR, &reg, 1, true);
    i2c_read_blocking (TP_I2C, TP_ADDR, &count, 1, false);
    return count > 0;
}

// Returns true and fills landscape coordinates if a finger is down.
// CST816S reports portrait panel coords (x: 0-239, y: 0-319).
// MADCTL 0x60 (MX|MV): landscape lx = portrait_y, ly = 239 - portrait_x.
static bool touch_get(uint16_t *lx, uint16_t *ly) {
    if (gpio_get(TP_INT)) return false;     // INT high = idle

    uint8_t buf[6];
    uint8_t reg = 0x01;
    i2c_write_blocking(TP_I2C, TP_ADDR, &reg, 1, true);
    i2c_read_blocking (TP_I2C, TP_ADDR, buf, 6, false);

    if (buf[1] == 0) return false;          // finger count = 0

    uint16_t px = (uint16_t)(((buf[2] & 0x0F) << 8) | buf[3]);   // portrait x
    uint16_t py = (uint16_t)(((buf[4] & 0x0F) << 8) | buf[5]);   // portrait y

    *lx = py;
    *ly = (uint16_t)(239 - px);
    return true;
}

// --- Button detection ---
// Button boxes are 48×48 px; coordinates below are the hit regions
// derived from the given centres ± 24 px.

typedef enum { BTN_NONE = 0, BTN_LAST = 1, BTN_PLAY = 2, BTN_STOP = 3, BTN_NEXT = 4 } Button;

static Button hit_button(uint16_t lx, uint16_t ly) {
    // Shared vertical band (centres 118-119, half-size 24)
    if (ly < 94 || ly > 142) return BTN_NONE;

    // Left-to-right priority
    if (lx >=   8 && lx <=  55) return BTN_LAST;   // centre x=32
    if (lx >=  92 && lx <= 139) return BTN_PLAY;   // centre x=116
    if (lx >= 180 && lx <= 227) return BTN_STOP;   // centre x=204
    if (lx >= 262 && lx <= 309) return BTN_NEXT;   // centre x=286

    return BTN_NONE;
}

// --- UART comms ---
// The hardware UART FIFO is only 32 bytes; our packet is 88 bytes.
// An interrupt-driven ring buffer captures every byte regardless of
// what the main loop is doing at the time.

#define RX_BUF_SIZE 256     // power of 2; must exceed max packet size
static volatile uint8_t  rx_buf[RX_BUF_SIZE];
static volatile uint16_t rx_head = 0;
static volatile uint16_t rx_tail = 0;

static void uart_rx_irq(void) {
    while (uart_is_readable(SID_UART))
        rx_buf[rx_head = (rx_head + 1) & (RX_BUF_SIZE - 1)] =
            uart_getc(SID_UART);
}

static inline bool rx_empty(void)    { return rx_head == rx_tail; }
static inline uint8_t rx_pop(void)  {
    rx_tail = (rx_tail + 1) & (RX_BUF_SIZE - 1);
    return rx_buf[rx_tail];
}

static void comms_init(void) {
    uart_init(SID_UART, SID_BAUD_RATE);
    gpio_set_function(SID_UART_TX, GPIO_FUNC_UART);
    gpio_set_function(SID_UART_RX, GPIO_FUNC_UART);
    irq_set_exclusive_handler(UART0_IRQ, uart_rx_irq);
    irq_set_enabled(UART0_IRQ, true);
    uart_set_irq_enables(SID_UART, true, false);
}

static void comms_send_cmd(uint8_t cmd) {
    uint8_t chk = PKT_TYPE_CMD ^ 1 ^ cmd;
    uint8_t pkt[] = { PKT_HDR0, PKT_HDR1, PKT_TYPE_CMD, 1, cmd, chk };
    uart_write_blocking(SID_UART, pkt, sizeof(pkt));
}

// Non-blocking metadata receiver.  Call every loop tick.
// Returns true and fills *meta when a valid metadata packet arrives.
static bool comms_poll(SidMeta *meta) {
    typedef enum { H0, H1, TP, LN, PL, CK } State;
    static State    state = H0;
    static uint8_t  type, len, chk, idx;
    static uint8_t  buf[sizeof(SidMeta)];

    while (!rx_empty()) {
        uint8_t b = rx_pop();

        switch (state) {
            case H0: if (b == PKT_HDR0) state = H1; break;
            case H1: state = (b == PKT_HDR1) ? TP : H0; break;
            case TP: type = b; chk = b; state = LN; break;
            case LN: len = b; chk ^= b; idx = 0;
                     state = (len > 0 && len <= sizeof(buf)) ? PL : H0; break;
            case PL: buf[idx++] = b; chk ^= b;
                     if (idx >= len) state = CK; break;
            case CK:
                if (b == chk && type == PKT_TYPE_META && len == sizeof(SidMeta)) {
                    memcpy(meta, buf, sizeof(SidMeta));
                    state = H0;
                    return true;
                }
                state = H0;
                break;
        }
    }
    return false;
}

// --- LED blink ---

static void blink(int n) {
    for (int i = 0; i < n; i++) {
        gpio_put(PICO_DEFAULT_LED_PIN, 1); sleep_ms(150);
        gpio_put(PICO_DEFAULT_LED_PIN, 0);
        if (i < n - 1) sleep_ms(150);
    }
}

// --- Main ---
int main(void) {
    stdio_init_all();

    // Init UART first so the ring buffer captures packets
    // that arrive while we are still loading the BMP from SD.
    comms_init();

    spi_init(SPI_PORT, 12500000);
    gpio_set_function(LCD_SCLK, GPIO_FUNC_SPI);
    gpio_set_function(LCD_MOSI, GPIO_FUNC_SPI);
    gpio_set_function(LCD_MISO, GPIO_FUNC_SPI);

    gpio_init(LCD_CS);  gpio_set_dir(LCD_CS,  GPIO_OUT); gpio_put(LCD_CS,  1);
    gpio_init(LCD_DC);  gpio_set_dir(LCD_DC,  GPIO_OUT); gpio_put(LCD_DC,  1);
    gpio_init(LCD_RST); gpio_set_dir(LCD_RST, GPIO_OUT); gpio_put(LCD_RST, 1);
    gpio_init(LCD_BL);  gpio_set_dir(LCD_BL,  GPIO_OUT); gpio_put(LCD_BL,  1);
    gpio_init(SD_CS);   gpio_set_dir(SD_CS,   GPIO_OUT); gpio_put(SD_CS,   1);

    gpio_init(PICO_DEFAULT_LED_PIN);
    gpio_set_dir(PICO_DEFAULT_LED_PIN, GPIO_OUT);

    lcd_init();
    lcd_fill(0x0000);

    FATFS fs;
    if (f_mount(&fs, "0:", 1) != FR_OK) {
        lcd_fill(0xF800); while (true) tight_loop_contents();
    }

    FIL bg_fil, fg_fil;
    if (f_open(&bg_fil, "0:/commando.bmp", FA_READ) != FR_OK) {
        lcd_fill(0x001F); while (true) tight_loop_contents();
    }
    if (f_open(&fg_fil, "0:/controls.bmp", FA_READ) != FR_OK) {
        lcd_fill(0xF81F); while (true) tight_loop_contents();
    }

    display_blended(&bg_fil, &fg_fil);
    f_close(&fg_fil);
    f_close(&bg_fil);

    // Draw placeholder so we know text rendering works before UART arrives
    {
        SidMeta placeholder = {0};
        strncpy(placeholder.title,    "Waiting for player...", SID_TITLE_LEN - 1);
        strncpy(placeholder.author,   "---", SID_AUTHOR_LEN - 1);
        strncpy(placeholder.released, "----", SID_RELEASED_LEN - 1);
        placeholder.song_num   = 0;
        placeholder.song_count = 0;
        display_metadata(&placeholder);
    }

    touch_init();

    // Player sent metadata at boot while we were still loading the BMP.
    // The 32-byte UART FIFO overflowed and the packet was lost.
    // Ask the Player to re-send now that we are ready.
    comms_send_cmd(CMD_REQUEST_META);

    SidMeta meta = {0};

    while (true) {
        // Receive metadata from Player Pico (non-blocking)
        if (comms_poll(&meta)) display_metadata(&meta);

        // Handle touch
        uint16_t lx, ly;
        if (touch_get(&lx, &ly)) {
            Button btn = hit_button(lx, ly);
            switch (btn) {
                case BTN_PLAY: comms_send_cmd(CMD_PLAY); break;
                case BTN_NEXT: comms_send_cmd(CMD_NEXT); break;
                default: break;
            }
            if (btn != BTN_NONE) {
                while (finger_down()) {
                    if (comms_poll(&meta)) display_metadata(&meta);
                    sleep_ms(20);
                }
                sleep_ms(50);
            }
        }
        sleep_ms(10);
    }
}
