#ifndef SID_COMMS_H
#define SID_COMMS_H

#include <stdint.h>

// -----------------------------------------------------------------------
// Inter-Pico PIO UART protocol  (Player Pico side)
//   Player  GP21 (PIO TX) ──► Display GPIO 1 (RX)
//   Player  GP22 (PIO RX) ◄── Display GPIO 0 (TX)
//   Common GND
// -----------------------------------------------------------------------

#define SID_BAUD_RATE   115200

// Packet framing
#define PKT_HDR0        0xAA
#define PKT_HDR1        0x55

// Packet types
#define PKT_TYPE_META   0x01    // Player → Display : song metadata
#define PKT_TYPE_CMD    0x02    // Display → Player : command
#define PKT_TYPE_STATE  0x03    // Player → Display : stereo state (2SID)

// Commands (single-byte payload in a CMD packet)
#define CMD_PLAY        0x01    // play current file
#define CMD_NEXT        0x02    // advance to next file, send metadata, wait
#define CMD_PREV        0x04    // go to previous file, send metadata, wait
#define CMD_STOP        0x05    // stop playback, stay on current file, wait
#define CMD_NEXT_TUNE     0x06    // next subtune within the current SID file
#define CMD_PREV_TUNE     0x07    // previous subtune within the current SID file
#define CMD_REQUEST_META  0x08    // request Player to re-send current metadata
#define CMD_STEREO_CENTER 0x09    // 2SID: play L/R center balanced  (-> "Mono")
#define CMD_STEREO_PANNED 0x0A    // 2SID: play L left, R right       (-> "Stereo")

// -----------------------------------------------------------------------
// Stereo state payload  (Player → Display, single byte in a STATE packet)
// The Player reports this when a 2SID file is played, and sends
// STEREO_NONE to clear it whenever the current file is not a 2SID.
// -----------------------------------------------------------------------
#define STEREO_NONE     0x00    // not a 2SID file: hide state, disable buttons
#define STEREO_CENTER   0x01    // L/R center balanced -> show "Mono"
#define STEREO_PANNED   0x02    // L/R panned          -> show "Stereo"

// -----------------------------------------------------------------------
// Metadata payload  (Player → Display)
// Sent on boot and after every song change.
// All strings are null-terminated, fixed-width.
// -----------------------------------------------------------------------
#define SID_FILENAME_LEN    32
#define SID_TITLE_LEN       32
#define SID_AUTHOR_LEN      32
#define SID_RELEASED_LEN    32

typedef struct __attribute__((packed)) {
    uint8_t song_num;                   // current song (1-based)
    uint8_t song_count;                 // total songs in the SID file
    char    filename [SID_FILENAME_LEN];
    char    title    [SID_TITLE_LEN];
    char    author   [SID_AUTHOR_LEN];
    char    released [SID_RELEASED_LEN];
} SidMeta;

// -----------------------------------------------------------------------
// Packet wire format:
//
//  [HDR0][HDR1][type][len][ ...payload (len bytes)... ][checksum]
//
//  checksum = XOR of type, len, and every payload byte
// -----------------------------------------------------------------------

#endif // SID_COMMS_H
