#ifndef GAMEPAD_PACKET_H
#define GAMEPAD_PACKET_H

#include <stdint.h>

// Shared packet format between hub and all vehicles.
// 12 bytes — well within ESP-NOW's 250-byte limit.
typedef struct __attribute__((packed)) {
  uint8_t vehicleId;   // target vehicle (0-7), 0xFF = broadcast to all
  int16_t axisX;       // left stick X  (-512 to 512)
  int16_t axisY;       // left stick Y  (-512 to 512)
  int16_t axisRX;      // right stick X (-512 to 512)
  int16_t axisRY;      // right stick Y (-512 to 512)
  uint8_t dpad;        // d-pad encoded (1=up, 2=down, 4=right, 8=left)
  uint8_t buttons;     // bitfield: [a, b, x, y, l1, l2, r1, r2]
  uint8_t misc;        // bitfield: [thumbL, thumbR]
} GamepadPacket;

// Button bitfield positions
#define BTN_A   (1 << 0)
#define BTN_B   (1 << 1)
#define BTN_X   (1 << 2)
#define BTN_Y   (1 << 3)
#define BTN_L1  (1 << 4)
#define BTN_L2  (1 << 5)
#define BTN_R1  (1 << 6)
#define BTN_R2  (1 << 7)

// Misc bitfield positions
#define MISC_THUMB_L (1 << 0)
#define MISC_THUMB_R (1 << 1)

#endif
