#ifndef SID_COMMS_H
#define SID_COMMS_H

#include <stdint.h>

// -----------------------------------------------------------------------
// Inter-Pico UART protocol
//   Display Pico  UART0 TX (GPIO 0) ──► Player Pico  UART0 RX (GPIO 1)
//   Display Pico  UART0 RX (GPIO 1) ◄── Player Pico  UART0 TX (GPIO 0)
//   Common GND
// -----------------------------------------------------------------------

#define SID_BAUD_RATE   115200

// Packet framing
#define PKT_HDR0        0xAA
#define PKT_HDR1        0x55

// Packet types
#define PKT_TYPE_META   0x01    // Player → Display : song metadata
#define PKT_TYPE_CMD    0x02    // Display → Player : command

// Commands (single-byte payload in a CMD packet)
#define CMD_PLAY         0x01   // play current song
#define CMD_NEXT         0x02   // advance to next SID file, play it
#define CMD_REQUEST_META 0x03   // request re-send of current metadata
#define CMD_PREV         0x04   // go to previous SID file, send metadata, wait
#define CMD_STOP         0x05   // stop playback, reset to start, do not play

// -----------------------------------------------------------------------
// Metadata payload  (Player → Display)
// Sent on boot and after every song change.
// All strings are null-terminated, fixed-width.
// -----------------------------------------------------------------------
#define SID_FILENAME_LEN    32  // SID base name, no path, no extension
#define SID_TITLE_LEN       32
#define SID_AUTHOR_LEN      32
#define SID_RELEASED_LEN    32

typedef struct __attribute__((packed)) {
    uint8_t song_num;                   // current song (1-based)
    uint8_t song_count;                 // total songs in the SID file
    char    filename[SID_FILENAME_LEN]; // e.g. "commando" → looks for commando.bmp
    char    title   [SID_TITLE_LEN];
    char    author  [SID_AUTHOR_LEN];
    char    released[SID_RELEASED_LEN];
} SidMeta;

// -----------------------------------------------------------------------
// Packet wire format:
//
//  [HDR0][HDR1][type][len][ ...payload (len bytes)... ][checksum]
//
//  checksum = XOR of type, len, and every payload byte
// -----------------------------------------------------------------------

#endif // SID_COMMS_H
