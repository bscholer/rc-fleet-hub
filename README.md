# RC Fleet Hub

One PS4/PS5/Xbox controller → one ESP32 hub → switches between multiple RC vehicles via ESP-NOW.

Built for ProfBoots RC construction fleet (excavator, skid steer, dump truck, semi, log truck, wheel loader, crane).

## How it works

```
PS4 Controller ──bluetooth──▶ Hub ESP32 ──ESP-NOW broadcast──▶ Vehicle ESP32s
                                │                                   │
                          SHARE button                        filters by
                          cycles target                       vehicleId
```

- Hub runs Bluepad32 (Bluetooth) + ESP-NOW (WiFi) simultaneously
- Packs all gamepad state into a 12-byte `GamepadPacket`
- Broadcasts to all vehicles; each vehicle checks `vehicleId` and ignores if not addressed
- 500ms safety timeout on vehicles: motors stop if hub signal is lost
- Press **SHARE** to cycle through vehicles

## Arduino IDE Setup

### Hub ESP32

1. **Board manager**: Add this URL in Preferences → Additional Board Manager URLs:
   ```
   https://raw.githubusercontent.com/ricardoquesada/esp32-arduino-lib-builder/master/bluepad32_files/package_esp32_bluepad32_index.json
   ```
2. Install **"ESP32 + Bluepad32"** from Board Manager (NOT the standard Espressif ESP32 package)
3. Select board: **ESP32 Dev Module** (under the Bluepad32 section)
4. Open `rc_fleet_hub/rc_fleet_hub.ino`
5. Upload

No extra libraries needed — WiFi and esp_now are built into the ESP32 core.

### Vehicle ESP32s

1. Use the **standard Espressif ESP32** board package (not Bluepad32)
2. Install libraries via Library Manager:
   - **ESP32Servo** by Kevin Harrington
   - **Adafruit MCP23X17** (+ Adafruit BusIO dependency)
3. Copy `gamepad_packet.h` into the vehicle's sketch folder (already done for excavator)
4. Set `MY_VEHICLE_ID` in the sketch to match the hub's vehicle list
5. Upload

## Vehicle IDs

| ID | Vehicle |
|----|---------|
| 0 | Excavator |
| 1 | Skid Steer |
| 2 | Dump Truck |
| 3 | Semi Truck |
| 4 | Log Truck |
| 5 | Wheel Loader |
| 6 | Crane |

## Adapting other vehicles

To convert any ProfBoots vehicle from direct Bluepad32 to ESP-NOW:

1. Remove `#include <Bluepad32.h>` and all BP32 setup/callback code
2. Add `#include <WiFi.h>`, `#include <esp_now.h>`, `#include "gamepad_packet.h"`
3. Add the ESP-NOW init + receive callback (see excavator example)
4. Replace `processGamepad(ctl)` with `processPacket(pkt)`, reading from the packet struct instead of `ctl->axisX()` etc.
5. The motor/servo control functions stay exactly the same

## Packet format

```c
typedef struct __attribute__((packed)) {
  uint8_t vehicleId;   // 0-7
  int16_t axisX;       // left stick X
  int16_t axisY;       // left stick Y
  int16_t axisRX;      // right stick X
  int16_t axisRY;      // right stick Y
  uint8_t dpad;        // 1=up, 2=down, 4=right, 8=left
  uint8_t buttons;     // a,b,x,y,l1,l2,r1,r2 bitfield
  uint8_t misc;        // thumbL, thumbR bitfield
} GamepadPacket;       // 12 bytes
```
