// Mini Dozer — ESP-NOW receiver version
//
// Replaces direct Bluepad32 controller input with ESP-NOW packets
// from the RC Fleet Hub. All motor/servo/light logic is unchanged.
//
// Board setup (Arduino IDE):
//   1. Use STANDARD ESP32 board manager (Espressif), NOT Bluepad32
//   2. Select board: "ESP32 Dev Module"
//   3. Libraries needed: ESP32Servo
//   4. WiFi + esp_now are built-in — no install needed
//
// NOTE: Add this vehicle to rc_fleet_hub's vehicleNames[] at index MY_VEHICLE_ID
// and uncomment the matching entry there before flashing the hub.
//
// Controls:
//   Left stick Y     → left track
//   Right stick RY   → right track
//   D-pad up/down    → main blade tilt up/down
//   D-pad left/right → ripper motor forward/reverse
//   L1 / L2          → left blade tilt forward/reverse
//   R1 / R2          → right blade tilt forward/reverse
//   B                → ripper servo up (incremental per packet)
//   X                → ripper servo down (incremental per packet)
//   Right thumb      → toggle lights

#include <Arduino.h>
#include <ESP32Servo.h>
#include <WiFi.h>
#include <esp_now.h>
#include "gamepad_packet.h"

// ─── Vehicle identity ────────────────────────────────────────────
// Must match the index in the hub's vehicleNames[] array.
#define MY_VEHICLE_ID 4

// ─── Pin defines ─────────────────────────────────────────────────
#define LT1 15
#define LT2 27
#define LT3 14

#define ripperServoPin  23

#define leftMotor0       4
#define leftMotor1       2
#define rightMotor0     12
#define rightMotor1     13
#define leftBladeTilt0  16
#define leftBladeTilt1  17
#define rightBladeTilt0 19
#define rightBladeTilt1 18
#define bladeTilt0      25
#define bladeTilt1      26
#define ripperMotor0    33
#define ripperMotor1    32

// ─── Hardware ────────────────────────────────────────────────────
Servo ripperServo;

// ─── State ───────────────────────────────────────────────────────
int  lightSwitchButtonTime = 0;
int  ripperServoValue      = 90;
bool lightMode             = false;

// Latest packet from hub (written in ISR context, read in loop)
volatile bool newPacket = false;
GamepadPacket latestPacket;

// ─── ESP-NOW receive callback ────────────────────────────────────
void onDataRecv(const esp_now_recv_info_t *info, const uint8_t *data, int len) {
  if (len != sizeof(GamepadPacket)) return;
  GamepadPacket pkt;
  memcpy(&pkt, data, sizeof(pkt));
  if (pkt.vehicleId != MY_VEHICLE_ID) return;
  latestPacket = pkt;
  newPacket    = true;
}

// ─── Motor helpers ───────────────────────────────────────────────
void moveMotor(int motorPin0, int motorPin1, int velocity) {
  if (velocity > 15) {
    analogWrite(motorPin0, velocity);
    analogWrite(motorPin1, LOW);
  } else if (velocity < -15) {
    analogWrite(motorPin0, LOW);
    analogWrite(motorPin1, -velocity);
  } else {
    analogWrite(motorPin0, 0);
    analogWrite(motorPin1, 0);
  }
}

// ─── Process functions ───────────────────────────────────────────
void processLeftThrottle(int axisYValue) {
  moveMotor(leftMotor0, leftMotor1, axisYValue / 2);
}

void processRightThrottle(int axisRYValue) {
  moveMotor(rightMotor0, rightMotor1, axisRYValue / 2);
}

void processBladeTiltAndRipper(int dpadValue) {
  if (dpadValue == 1) {
    moveMotor(bladeTilt0, bladeTilt1, 255);
  } else if (dpadValue == 2) {
    moveMotor(bladeTilt0, bladeTilt1, -255);
  } else {
    moveMotor(bladeTilt0, bladeTilt1, 0);
  }

  if (dpadValue == 8) {
    moveMotor(ripperMotor0, ripperMotor1, 255);
  } else if (dpadValue == 4) {
    moveMotor(ripperMotor0, ripperMotor1, -255);
  } else {
    moveMotor(ripperMotor0, ripperMotor1, 0);
  }
}

void processLights(bool buttonValue) {
  if (buttonValue && (millis() - lightSwitchButtonTime) > 300) {
    lightMode = !lightMode;
    digitalWrite(LT1, lightMode ? HIGH : LOW);
    lightSwitchButtonTime = millis();
  }
}

// ─── Process a received packet ───────────────────────────────────
void processPacket(GamepadPacket &pkt) {
  processLeftThrottle(pkt.axisY);
  processRightThrottle(pkt.axisRY);
  processBladeTiltAndRipper(pkt.dpad);
  processLights((pkt.misc & MISC_THUMB_R) != 0);

  // Right blade tilt
  if (pkt.buttons & BTN_R1) {
    digitalWrite(rightBladeTilt0, HIGH);
    digitalWrite(rightBladeTilt1, LOW);
  } else if (pkt.buttons & BTN_R2) {
    digitalWrite(rightBladeTilt0, LOW);
    digitalWrite(rightBladeTilt1, HIGH);
  } else {
    digitalWrite(rightBladeTilt0, LOW);
    digitalWrite(rightBladeTilt1, LOW);
  }

  // Left blade tilt
  if (pkt.buttons & BTN_L1) {
    digitalWrite(leftBladeTilt0, HIGH);
    digitalWrite(leftBladeTilt1, LOW);
  } else if (pkt.buttons & BTN_L2) {
    digitalWrite(leftBladeTilt0, LOW);
    digitalWrite(leftBladeTilt1, HIGH);
  } else {
    digitalWrite(leftBladeTilt0, LOW);
    digitalWrite(leftBladeTilt1, LOW);
  }

  // Ripper servo: 2 degrees per packet while button held — rate is tied to hub update rate
  if (pkt.buttons & BTN_B) {
    if (ripperServoValue < 170) {
      ripperServoValue += 2;
      ripperServo.write(ripperServoValue);
    }
  } else if (pkt.buttons & BTN_X) {
    if (ripperServoValue > 10) {
      ripperServoValue -= 2;
      ripperServo.write(ripperServoValue);
    }
  }
}

// ─── Safety: stop everything when hub goes silent ────────────────
void allStop() {
  moveMotor(leftMotor0,   leftMotor1,   0);
  moveMotor(rightMotor0,  rightMotor1,  0);
  moveMotor(bladeTilt0,   bladeTilt1,   0);
  moveMotor(ripperMotor0, ripperMotor1, 0);
  digitalWrite(rightBladeTilt0, LOW); digitalWrite(rightBladeTilt1, LOW);
  digitalWrite(leftBladeTilt0,  LOW); digitalWrite(leftBladeTilt1,  LOW);
}

// ─── Setup ───────────────────────────────────────────────────────
void setup() {
  Serial.begin(115200);

  pinMode(leftMotor0,      OUTPUT); pinMode(leftMotor1,      OUTPUT);
  pinMode(rightMotor0,     OUTPUT); pinMode(rightMotor1,     OUTPUT);
  pinMode(ripperMotor0,    OUTPUT); pinMode(ripperMotor1,    OUTPUT);
  pinMode(leftBladeTilt0,  OUTPUT); pinMode(leftBladeTilt1,  OUTPUT);
  pinMode(rightBladeTilt0, OUTPUT); pinMode(rightBladeTilt1, OUTPUT);
  pinMode(bladeTilt0,      OUTPUT); pinMode(bladeTilt1,      OUTPUT);
  pinMode(LT1, OUTPUT); pinMode(LT2, OUTPUT); pinMode(LT3, OUTPUT);

  digitalWrite(leftMotor0,      LOW); digitalWrite(leftMotor1,      LOW);
  digitalWrite(rightMotor0,     LOW); digitalWrite(rightMotor1,     LOW);
  digitalWrite(ripperMotor0,    LOW); digitalWrite(ripperMotor1,    LOW);
  digitalWrite(leftBladeTilt0,  LOW); digitalWrite(leftBladeTilt1,  LOW);
  digitalWrite(rightBladeTilt0, LOW); digitalWrite(rightBladeTilt1, LOW);
  digitalWrite(bladeTilt0,      LOW); digitalWrite(bladeTilt1,      LOW);
  digitalWrite(LT1, LOW); digitalWrite(LT2, LOW); digitalWrite(LT3, LOW);

  ripperServo.attach(ripperServoPin);
  ripperServo.write(ripperServoValue);

  WiFi.mode(WIFI_STA);
  WiFi.disconnect();

  if (esp_now_init() != ESP_OK) {
    Serial.println("ERROR: ESP-NOW init failed!");
    return;
  }
  esp_now_register_recv_cb(onDataRecv);

  Serial.printf("\n=== Mini Dozer (ESP-NOW) === Vehicle ID: %d\n", MY_VEHICLE_ID);
  Serial.println("Listening for hub commands...");
}

// ─── Loop ────────────────────────────────────────────────────────
unsigned long lastPacketTime = 0;

void loop() {
  if (newPacket) {
    newPacket      = false;
    lastPacketTime = millis();
    processPacket(latestPacket);
  }

  // Safety timeout: stop everything if hub goes silent for 500ms
  if (lastPacketTime > 0 && millis() - lastPacketTime > 500) {
    allStop();
    lastPacketTime = 0;
    Serial.println("No signal — motors stopped");
  }

  vTaskDelay(1);
}
