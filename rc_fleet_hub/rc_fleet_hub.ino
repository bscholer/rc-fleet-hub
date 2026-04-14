// RC Fleet Hub — Bluepad32 → ESP-NOW relay
//
// Pairs to a PS4/PS5/Xbox controller via Bluetooth (bluepad32),
// packs gamepad state into a 12-byte packet, and broadcasts it
// over ESP-NOW to whichever vehicle is currently selected.
//
// SHARE button cycles the active vehicle.
// Serial monitor shows the current target.
//
// Board setup (Arduino IDE):
//   1. Add Bluepad32 ESP32 board manager URL (see README)
//   2. Select board: "ESP32 Dev Module" (under Bluepad32 section)
//   3. No extra libraries needed — WiFi + esp_now are built-in
//
// NOTE: This board runs BOTH Bluetooth (bluepad32) and WiFi (ESP-NOW)
// simultaneously. The ESP32 hardware supports this natively.

#include <Bluepad32.h>
#include <WiFi.h>
#include <esp_now.h>
#include "gamepad_packet.h"

// ─── Configuration ───────────────────────────────────────────────
#define NUM_VEHICLES 7
const char* vehicleNames[NUM_VEHICLES] = {
  "Excavator",   // 0
  "Skid Steer",  // 1
  "Dump Truck",  // 2
  "Semi Truck",  // 3
  "Log Truck",   // 4
  "Wheel Loader", // 5
  "Crane",       // 6
};

// ESP-NOW broadcast address — all vehicles receive, filter by vehicleId
uint8_t broadcastAddr[] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

// ─── State ───────────────────────────────────────────────────────
ControllerPtr myControllers[BP32_MAX_GAMEPADS];
int activeVehicle = 0;
bool shareWasPressed = false;  // edge detection for SHARE button
unsigned long lastSendTime = 0;

// ─── ESP-NOW send callback (optional, for debugging) ─────────────
void onDataSent(const uint8_t *mac, esp_now_send_status_t status) {
  // Uncomment for debugging:
  // Serial.printf("Send status: %s\n", status == ESP_NOW_SEND_SUCCESS ? "OK" : "FAIL");
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

// ─── Handle vehicle switching (SHARE button, edge-triggered) ─────
void handleVehicleSwitch(ControllerPtr ctl) {
  // miscButtons bit 1 = SHARE/Back/Select depending on controller
  bool sharePressed = (ctl->miscButtons() & 0x02) != 0;

  if (sharePressed && !shareWasPressed) {
    activeVehicle = (activeVehicle + 1) % NUM_VEHICLES;
    Serial.printf(">>> Switched to vehicle %d: %s\n", activeVehicle, vehicleNames[activeVehicle]);
  }
  shareWasPressed = sharePressed;
}

// ─── Process all connected controllers ───────────────────────────
void processControllers() {
  for (auto ctl : myControllers) {
    if (ctl && ctl->isConnected() && ctl->hasData() && ctl->isGamepad()) {
      handleVehicleSwitch(ctl);

      GamepadPacket pkt = packGamepad(ctl);
      esp_now_send(broadcastAddr, (uint8_t *)&pkt, sizeof(pkt));
    }
  }
}

// ─── Setup ───────────────────────────────────────────────────────
void setup() {
  Serial.begin(115200);
  Serial.println("\n=== RC Fleet Hub ===");

  // Init WiFi in station mode for ESP-NOW (no AP connection needed)
  WiFi.mode(WIFI_STA);
  WiFi.disconnect();  // make sure we're not trying to connect to anything
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
  peerInfo.channel = 0;  // use current channel
  peerInfo.encrypt = false;
  if (esp_now_add_peer(&peerInfo) != ESP_OK) {
    Serial.println("ERROR: Failed to add broadcast peer!");
    return;
  }

  // Init Bluepad32
  Serial.printf("Bluepad32 firmware: %s\n", BP32.firmwareVersion());
  BP32.setup(&onConnectedController, &onDisconnectedController);
  BP32.forgetBluetoothKeys();
  BP32.enableVirtualDevice(false);

  Serial.printf("Active vehicle: %d (%s)\n", activeVehicle, vehicleNames[activeVehicle]);
  Serial.println("Press SHARE to cycle vehicles. Waiting for controller...");
}

// ─── Loop ────────────────────────────────────────────────────────
void loop() {
  bool dataUpdated = BP32.update();
  if (dataUpdated) {
    processControllers();
  } else {
    vTaskDelay(1);
  }
}
