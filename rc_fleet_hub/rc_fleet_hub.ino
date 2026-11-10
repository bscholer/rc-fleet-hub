// RC Fleet Hub — Bluepad32 → ESP-NOW relay
//
// Pairs to a PS4/PS5/Xbox/8BitDo controller via Bluetooth (bluepad32),
// packs gamepad state into a 12-byte packet, and broadcasts it
// over ESP-NOW to whichever vehicle is currently selected.
//
// Vehicle switching:
//   - SHARE button: cycle forward
//   - Back paddles (8BitDo Pro 2): next / previous vehicle
//
// Controller player LEDs show active vehicle ID in binary (4 LEDs = 0-15).
//
// Board setup (Arduino IDE):
//   1. Add Bluepad32 ESP32 board manager URL (see README)
//   2. Select board: "ESP32 Dev Module" (under Bluepad32 section)
//   3. No extra libraries needed — WiFi + esp_now are built-in
//
// NOTE: This board runs BOTH Bluetooth (bluepad32) and WiFi (ESP-NOW)
// simultaneously. The ESP32 hardware supports this natively.
//
// ─── PADDLE DISCOVERY ────────────────────────────────────────────
// Set DEBUG_BUTTONS to true, upload, open serial monitor at 115200,
// press each back paddle and note the miscButtons hex value printed.
// Then update PADDLE_NEXT_MASK / PADDLE_PREV_MASK below and set
// DEBUG_BUTTONS back to false.

#include <Bluepad32.h>
#include <WiFi.h>
#include <esp_now.h>
#include <esp_bt.h>
#include "gamepad_packet.h"

// ─── Configuration ───────────────────────────────────────────────

// Set true to print all button/misc values to serial for discovering
// which bits your back paddles map to. Disable for normal operation.
#define DEBUG_BUTTONS false

// Print BT/ESP-NOW stats every N milliseconds. Set 0 to disable.
#define DIAG_INTERVAL_MS 5000

// Warn when a connected controller produces no BT data for this long.
#define BT_STALL_MS 200

// miscButtons bitmasks for vehicle switching.
// On 8BitDo Pro 2 (Switch mode): Select/- = 0x02, Start/+ = 0x04
// Use DEBUG_BUTTONS to verify if your controller differs.
#define SELECT_MASK 0x02  // Select / Share / Minus
#define START_MASK  0x04  // Start / Options / Plus

const char* vehicleNames[] = {
  "Excavator",     // 0
  "Forklift",      // 1
  "MiniSkidi",     // 2
  "Mini Truck",    // 3
  "Mini Dozer",    // 4
  "MiniLift",      // 5
  // "Log Truck",    // 6
  // "Wheel Loader", // 7
};
#define NUM_VEHICLES (sizeof(vehicleNames) / sizeof(vehicleNames[0]))

// ESP-NOW broadcast address — all vehicles receive, filter by vehicleId
uint8_t broadcastAddr[] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

// ─── State ───────────────────────────────────────────────────────
ControllerPtr myControllers[BP32_MAX_GAMEPADS];
int activeVehicle = 0;

// Edge detection for vehicle switch buttons
bool selectWasPressed = false;
bool startWasPressed = false;

// ─── Diagnostics state ───────────────────────────────────────────
static struct {
  uint32_t espNowOk;
  uint32_t espNowFail;
  uint32_t pktsSent;
  uint32_t btFrames;    // hasData frames received
  uint32_t maxGapMs;    // largest gap between consecutive BT frames
} diag;

static uint32_t lastDataMs   = 0;  // millis() of last hasData frame
static uint32_t lastStatsMs  = 0;
static bool     stallPrinted = false;

// ─── ESP-NOW send callback ────────────────────────────────────────
void onDataSent(const uint8_t *mac, esp_now_send_status_t status) {
  if (status == ESP_NOW_SEND_SUCCESS) {
    diag.espNowOk++;
  } else {
    diag.espNowFail++;
    Serial.printf("[ESP-NOW] FAIL (total fails: %u)\n", diag.espNowFail);
  }
}

// ─── Bluepad32 callbacks ─────────────────────────────────────────
void onConnectedController(ControllerPtr ctl) {
  for (int i = 0; i < BP32_MAX_GAMEPADS; i++) {
    if (myControllers[i] == nullptr) {
      Serial.printf("Controller connected to slot %d\n", i);
      ControllerProperties props = ctl->getProperties();
      Serial.printf("  Model: %s  VID=0x%04x PID=0x%04x\n",
                    ctl->getModelName().c_str(), props.vendor_id, props.product_id);
      myControllers[i] = ctl;

      // Show current vehicle on LEDs immediately
      updatePlayerLEDs(ctl);
      return;
    }
  }
  Serial.println("Controller connected but no empty slot!");
}

void onDisconnectedController(ControllerPtr ctl) {
  for (int i = 0; i < BP32_MAX_GAMEPADS; i++) {
    if (myControllers[i] == ctl) {
      Serial.printf("Controller disconnected from slot %d\n", i);
      myControllers[i] = nullptr;
      return;
    }
  }
}

// ─── Update controller player LEDs to show vehicle ID in binary ──
void updatePlayerLEDs(ControllerPtr ctl) {
  // 4 LEDs, each bit = one LED. Vehicle 0 = all off, 1 = 0001, etc.
  // For 8 vehicles (0-7) this uses 3 of 4 LEDs.
  // Offset by 1 so vehicle 0 shows LED 1 lit (0001) instead of all off.
  uint8_t ledPattern = (activeVehicle + 1) & 0x0F;
  ctl->setPlayerLEDs(ledPattern);
}

// ─── Pack gamepad state into a packet ────────────────────────────
GamepadPacket packGamepad(ControllerPtr ctl) {
  GamepadPacket pkt;
  pkt.vehicleId = (uint8_t)activeVehicle;

  pkt.axisX  = ctl->axisX();
  pkt.axisY  = ctl->axisY();
  pkt.axisRX = ctl->axisRX();
  pkt.axisRY = ctl->axisRY();

  pkt.dpad = (uint8_t)ctl->dpad();

  pkt.buttons = 0;
  if (ctl->a())  pkt.buttons |= BTN_A;
  if (ctl->b())  pkt.buttons |= BTN_B;
  if (ctl->x())  pkt.buttons |= BTN_X;
  if (ctl->y())  pkt.buttons |= BTN_Y;
  if (ctl->l1()) pkt.buttons |= BTN_L1;
  if (ctl->l2()) pkt.buttons |= BTN_L2;
  if (ctl->r1()) pkt.buttons |= BTN_R1;
  if (ctl->r2()) pkt.buttons |= BTN_R2;

  pkt.misc = 0;
  if (ctl->thumbL()) pkt.misc |= MISC_THUMB_L;
  if (ctl->thumbR()) pkt.misc |= MISC_THUMB_R;

  return pkt;
}

// ─── Switch active vehicle and update LEDs ───────────────────────
void switchVehicle(int newVehicle, ControllerPtr ctl) {
  activeVehicle = newVehicle;
  Serial.printf(">>> Vehicle %d: %s  [LEDs: %04b]\n",
                activeVehicle, vehicleNames[activeVehicle],
                (activeVehicle + 1) & 0x0F);
  updatePlayerLEDs(ctl);
}

// ─── Handle vehicle switching (Select/Start, edge-triggered) ─────
void handleVehicleSwitch(ControllerPtr ctl) {
  int misc = ctl->miscButtons();

  // Debug mode: print everything when any button is pressed
  #if DEBUG_BUTTONS
  static int lastMisc = 0;
  static int lastButtons = 0;
  int curButtons = (ctl->a()) | (ctl->b() << 1) | (ctl->x() << 2) | (ctl->y() << 3)
                 | (ctl->l1() << 4) | (ctl->l2() << 5) | (ctl->r1() << 6) | (ctl->r2() << 7);
  if (misc != lastMisc || curButtons != lastButtons) {
    Serial.printf("[DEBUG] miscButtons=0x%02X  buttons=0x%02X  thumbL=%d  thumbR=%d  dpad=%d\n",
                  misc, curButtons, ctl->thumbL(), ctl->thumbR(), ctl->dpad());
    lastMisc = misc;
    lastButtons = curButtons;
  }
  #endif

  // Select/Share/Minus → previous vehicle
  bool selectPressed = (misc & SELECT_MASK) != 0;
  if (selectPressed && !selectWasPressed) {
    switchVehicle((activeVehicle - 1 + NUM_VEHICLES) % NUM_VEHICLES, ctl);
  }
  selectWasPressed = selectPressed;

  // Start/Options/Plus → next vehicle
  bool startPressed = (misc & START_MASK) != 0;
  if (startPressed && !startWasPressed) {
    switchVehicle((activeVehicle + 1) % NUM_VEHICLES, ctl);
  }
  startWasPressed = startPressed;
}

// ─── Process all connected controllers ───────────────────────────
void processControllers() {
  for (auto ctl : myControllers) {
    if (ctl && ctl->isConnected() && ctl->hasData() && ctl->isGamepad()) {
      uint32_t now = millis();
      if (lastDataMs > 0) {
        uint32_t gap = now - lastDataMs;
        if (gap > diag.maxGapMs) diag.maxGapMs = gap;
      }
      lastDataMs   = now;
      stallPrinted = false;
      diag.btFrames++;

      handleVehicleSwitch(ctl);

      GamepadPacket pkt = packGamepad(ctl);
      esp_now_send(broadcastAddr, (uint8_t *)&pkt, sizeof(pkt));
      diag.pktsSent++;
    }
  }
}

// ─── Setup ───────────────────────────────────────────────────────
void setup() {
  Serial.begin(115200);
  // Full CPU speed — reduces scheduling latency in BT and WiFi stacks
  setCpuFrequencyMhz(240);

  Serial.println("\n=== RC Fleet Hub ===");
  Serial.printf("Vehicles configured: %d\n", NUM_VEHICLES);

  // Init WiFi in station mode for ESP-NOW (no AP connection needed)
  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  Serial.printf("Hub MAC: %s\n", WiFi.macAddress().c_str());

  // Init ESP-NOW
  if (esp_now_init() != ESP_OK) {
    Serial.println("ERROR: ESP-NOW init failed!");
    return;
  }
  esp_now_register_send_cb(onDataSent);

  // Register broadcast peer
  esp_now_peer_info_t peerInfo = {};
  memcpy(peerInfo.peer_addr, broadcastAddr, 6);
  peerInfo.channel = 0;
  peerInfo.encrypt = false;
  if (esp_now_add_peer(&peerInfo) != ESP_OK) {
    Serial.println("ERROR: Failed to add broadcast peer!");
    return;
  }

  // Max BT TX power — improves link margin through coexistence interference bursts.
  // esp_bredr covers classic BT (PS4/Xbox/8BitDo classic mode);
  // esp_ble covers BLE-based controllers (8BitDo in Android/BLE mode).
  esp_ble_tx_power_set(ESP_BLE_PWR_TYPE_DEFAULT, ESP_PWR_LVL_P9);
  esp_bredr_tx_power_set(ESP_PWR_LVL_N0, ESP_PWR_LVL_P9);

  // Init Bluepad32
  Serial.printf("Bluepad32 firmware: %s\n", BP32.firmwareVersion());
  BP32.setup(&onConnectedController, &onDisconnectedController);
  BP32.forgetBluetoothKeys();
  BP32.enableVirtualDevice(false);

  Serial.printf("Active vehicle: %d (%s)\n", activeVehicle, vehicleNames[activeVehicle]);

  #if DEBUG_BUTTONS
  Serial.println("\n*** DEBUG MODE: press buttons and watch serial output ***");
  Serial.println("*** Look for miscButtons hex values when pressing paddles ***\n");
  #else
  Serial.println("Start/+ = next vehicle | Select/- = prev vehicle");
  #endif

  Serial.println("Waiting for controller...");
}

// ─── Loop ────────────────────────────────────────────────────────
void loop() {
  bool dataUpdated = BP32.update();
  if (dataUpdated) {
    processControllers();
  } else {
    // Check for BT stall: controller connected but no data arriving
    #if BT_STALL_MS > 0
    if (!stallPrinted && lastDataMs > 0) {
      bool anyConnected = false;
      for (auto ctl : myControllers) {
        if (ctl && ctl->isConnected()) { anyConnected = true; break; }
      }
      if (anyConnected) {
        uint32_t gap = millis() - lastDataMs;
        if (gap >= BT_STALL_MS) {
          Serial.printf("[BT STALL] No data for %ums (controller shows connected)\n", gap);
          stallPrinted = true;
        }
      }
    }
    #endif
    vTaskDelay(1);
  }

  // Periodic diagnostics summary
  #if DIAG_INTERVAL_MS > 0
  uint32_t now = millis();
  if (now - lastStatsMs >= DIAG_INTERVAL_MS) {
    uint32_t elapsedS = DIAG_INTERVAL_MS / 1000;
    float btHz   = (float)diag.btFrames   / elapsedS;
    float pktHz  = (float)diag.pktsSent   / elapsedS;
    uint32_t total = diag.espNowOk + diag.espNowFail;
    float failPct = total > 0 ? 100.0f * diag.espNowFail / total : 0.0f;
    Serial.printf("[DIAG] bt=%.1fHz pkt=%.1fHz maxGap=%ums | espnow ok=%u fail=%u (%.1f%% fail)\n",
                  btHz, pktHz, diag.maxGapMs, diag.espNowOk, diag.espNowFail, failPct);
    memset(&diag, 0, sizeof(diag));
    lastStatsMs = now;
  }
  #endif
}
