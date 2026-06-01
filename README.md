## Display Pico (`SID_Display_Dan`)

| GPIO | Function     | Connected to                          |
|------|---------------|---------------------------------------|
| GP0  | UART0 TX      | → Player Pico GP22 (PIO RX)           |
| GP1  | UART0 RX      | → Player Pico GP21 (PIO TX)           |
| GP4  | SPI1 CS       | SD card /CS                           |
| GP6  | I2C1 SDA      | CST816S touch SDA                     |
| GP7  | I2C1 SCL      | CST816S touch SCL                     |
| GP9  | SPI1 CS       | LCD /CS                               |
| GP10 | SPI1 SCK      | LCD SCLK + SD CLK                     |
| GP11 | SPI1 TX       | LCD MOSI + SD MOSI                    |
| GP12 | SPI1 RX       | SD MISO (LCD write-only)              |
| GP13 | GPIO OUT      | LCD RST                               |
| GP14 | GPIO OUT      | LCD DC (Data/Command)                 |
| GP15 | GPIO OUT      | LCD Backlight                         |
| GP16 | GPIO OUT      | CST816S RST                           |
| GP17 | GPIO IN       | CST816S INT                           |
| GP25 | LED           | Onboard LED                           |

> **Note:** LCD and SD card share SPI1 — they are chip-select separated on GP9 and GP4.

## Player Pico (`SID_Player_Dan`)

| GPIO | Function     | Connected to                              |
|------|---------------|-------------------------------------------|
| GP0–GP7  | GPIO OUT     | SID D0–D7 (data bus)                    |
| GP8–GP12 | GPIO OUT     | SID A0–A4 (address bus)                 |
| GP13     | GPIO OUT     | SID /CS                                |
| GP14     | —            | (unused)                               |
| GP15     | GPIO OUT     | SID /RES                               |
| GP16     | PWM          | SID phi2 clock (~985 kHz PAL)          |
| GP17     | SPI0 CS      | SD card /CS                            |
| GP18     | SPI0 SCK     | SD CLK                                 |
| GP19     | SPI0 TX      | SD MOSI                                |
| GP20     | SPI0 RX      | SD MISO                                |
| GP21     | PIO UART TX  | → Display Pico GP1 (UART0 RX)          |
| GP22     | PIO UART RX  | ← Display Pico GP0 (UART0 TX)          |

> **Note:** SID R/W pin is tied directly to GND (write‑only operation).  
> SID audio out is analog — comes straight from the SID chip’s **AUDIO OUT** pin to your output circuit, not through the Pico.


## Inter-Pico Link

Display GP0 (UART0 TX) ──────► Player GP22 (PIO RX) <br>
Display GP1 (UART0 RX) ◄────── Player GP21 (PIO TX) <br>
GND ──────────────────────────── GND <br>
115200 baud, packet-framed protocol defined in sid_comms.h.
