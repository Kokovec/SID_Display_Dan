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


Inter-Pico Link

Display GP0 (UART0 TX) ──────► Player GP22 (PIO RX)
Display GP1 (UART0 RX) ◄────── Player GP21 (PIO TX)
GND ──────────────────────────── GND
115200 baud, packet-framed protocol defined in sid_comms.h.
