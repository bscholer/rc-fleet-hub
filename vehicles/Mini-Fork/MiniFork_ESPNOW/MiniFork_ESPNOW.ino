// Mini Forklift — ESP-NOW receiver version
//
// Replaces direct Bluepad32 controller input with ESP-NOW packets
// from the RC Fleet Hub. All motor/servo/steering logic is unchanged.
//
// Board setup (Arduino IDE):
//   1. Use STANDARD ESP32 board manager (Espressif), NOT Bluepad32
//   2. Select board: "ESP32 Dev Module"
//   3. Libraries needed: ESP32Servo by Kevin Harrington
//   4. WiFi + esp_now are built-in — no install needed

#include <Arduino.h>
#include <ESP32Servo.h>  // by Kevin Harrington
#include <WiFi.h>
#include <esp_now.h>
#include "gamepad_packet.h"

// ─── Vehicle identity ────────────────────────────────────────────
// Must match the hub's vehicle list index.
// 0=Excavator, 1=Forklift, 2=SkidSteer, ...
#define MY_VEHICLE_ID 1

// ─── Pin defines (unchanged from original) ───────────────────────
#define steeringServoPin 23
#define mastTiltServoPin 22
#define cabLights 32
#define auxLights 33

#define mastMotor0 25
#define mastMotor1 26
#define auxAttach0 18
#define auxAttach1 17

#define leftMotor0 21
#define leftMotor1 19
#define rightMotor0 33
#define rightMotor1 32

// ─── Hardware ────────────────────────────────────────────────────
Servo steeringServo;
Servo mastTiltServo;

// ─── State ───────────────────────────────────────────────────────
int servoDelay = 0;
int lightSwitchTime = 0;
float adjustedSteeringValue = 86;
float steeringAdjustment = 1;
int steeringTrim = 0;
int mastTiltValue = 90;

bool lightsOn = false;
bool moveMastTiltServoDown = false;
bool moveMastTiltServoUp = false;
bool hardLeft = false;
bool hardRight = false;

// Latest packet from hub
volatile bool newPacket = false;
GamepadPacket latestPacket;

// ─── ESP-NOW receive callback ────────────────────────────────────
void onDataRecv(const esp_now_recv_info_t *info, const uint8_t *data, int len) {
  if (len != sizeof(GamepadPacket)) return;

  GamepadPacket pkt;
  memcpy(&pkt, data, sizeof(pkt));

  if (pkt.vehicleId != MY_VEHICLE_ID) return;

  latestPacket = pkt;
  newPacket = true;
}

// ─── Motor control (unchanged from original) ─────────────────────
void moveMotor(int motorPin0, int motorPin1, int velocity) {
  if (velocity > 1) {
    analogWrite(motorPin0, velocity);
    analogWrite(motorPin1, LOW);
  } else if (velocity < -1) {
    analogWrite(motorPin0, LOW);
    analogWrite(motorPin1, (-1 * velocity));
  } else {
    analogWrite(motorPin0, 0);
    analogWrite(motorPin1, 0);
  }
}

// ─── Processing functions (same logic as original) ───────────────
void processThrottle(int axisYValue) {
  float adjustedThrottleValue = axisYValue / 2;
  if (adjustedThrottleValue > 15 || adjustedThrottleValue < -15) {
    if (hardRight) {
      moveMotor(rightMotor0, rightMotor1, -1 * (adjustedThrottleValue * steeringAdjustment));
    } else if (hardLeft) {
      moveMotor(leftMotor0, leftMotor1, -1 * (adjustedThrottleValue * steeringAdjustment));
    } else if (adjustedSteeringValue > 100) {
      moveMotor(leftMotor0, leftMotor1, adjustedThrottleValue * steeringAdjustment);
      moveMotor(rightMotor0, rightMotor1, adjustedThrottleValue);
    } else if (adjustedSteeringValue < 80) {
      moveMotor(leftMotor0, leftMotor1, adjustedThrottleValue);
      moveMotor(rightMotor0, rightMotor1, adjustedThrottleValue * steeringAdjustment);
    } else {
      moveMotor(leftMotor0, leftMotor1, adjustedThrottleValue);
      moveMotor(rightMotor0, rightMotor1, adjustedThrottleValue);
    }
  } else {
    moveMotor(leftMotor0, leftMotor1, 0);
    moveMotor(rightMotor0, rightMotor1, 0);
  }
}

void processMast(int axisRYValue) {
  int adjustedMastValue = axisRYValue / 2;
  if (adjustedMastValue > 100 || adjustedMastValue < -100) {
    moveMotor(mastMotor0, mastMotor1, adjustedMastValue);
  } else {
    moveMotor(mastMotor0, mastMotor1, 0);
  }
}

void processTrimRight(bool pressed) {
  if (pressed && steeringTrim < 20) {
    steeringTrim += 2;
    delay(50);
  }
}

void processTrimLeft(bool pressed) {
  if (pressed && steeringTrim > -20) {
    steeringTrim -= 2;
    delay(50);
  }
}

void processSteering(int axisRXValue) {
  adjustedSteeringValue = (90 - (axisRXValue / 9));
  steeringServo.write(adjustedSteeringValue - steeringTrim);

  if (adjustedSteeringValue > 100) {
    steeringAdjustment = ((200 - adjustedSteeringValue) / 100);
  } else if (adjustedSteeringValue < 80) {
    steeringAdjustment = ((200 - (90 + (90 - adjustedSteeringValue))) / 100);
  }
}

void processMastTilt(int dpadValue) {
  if (dpadValue == 1) {
    if (servoDelay == 4) {
      if (mastTiltValue >= 10 && mastTiltValue < 170) {
        mastTiltValue += 1;
        mastTiltServo.write(mastTiltValue);
      }
      servoDelay = 0;
    }
    servoDelay++;
  } else if (dpadValue == 2) {
    if (servoDelay == 4) {
      if (mastTiltValue <= 170 && mastTiltValue > 10) {
        mastTiltValue -= 1;
        mastTiltServo.write(mastTiltValue);
      }
      servoDelay = 0;
    }
    servoDelay++;
  }
}

void processAux(bool buttonValue) {
  if (buttonValue && (millis() - lightSwitchTime) > 200) {
    if (lightsOn) {
      digitalWrite(auxAttach0, LOW);
      digitalWrite(auxAttach1, LOW);
      lightsOn = false;
    } else {
      digitalWrite(auxAttach0, HIGH);
      digitalWrite(auxAttach1, LOW);
      lightsOn = true;
    }
    lightSwitchTime = millis();
  }
}

// ─── Process a received packet (replaces processGamepad) ─────────
void processPacket(GamepadPacket &pkt) {
  // Hard turn flags
  hardLeft  = (pkt.buttons & BTN_L2) != 0;
  hardRight = (pkt.buttons & BTN_R2) != 0;

  // Throttle (left stick Y)
  processThrottle(pkt.axisY);
  // Steering (right stick X)
  processSteering(pkt.axisRX);
  // Mast raise/lower (right stick Y)
  processMast(pkt.axisRY);
  // Mast tilt (d-pad)
  processMastTilt(pkt.dpad);
  // Aux / lights (thumbR)
  processAux((pkt.misc & MISC_THUMB_R) != 0);
  // Steering trim
  processTrimRight((pkt.buttons & BTN_R1) != 0);
  processTrimLeft((pkt.buttons & BTN_L1) != 0);
}

// ─── Safety: stop all motors when hub stops sending ──────────────
void allStop() {
  moveMotor(leftMotor0, leftMotor1, 0);
  moveMotor(rightMotor0, rightMotor1, 0);
  moveMotor(mastMotor0, mastMotor1, 0);
  hardLeft = false;
  hardRight = false;
}

// ─── Setup ───────────────────────────────────────────────────────
void setup() {
  Serial.begin(115200);
  Serial.printf("\n=== Mini Forklift (ESP-NOW) === Vehicle ID: %d\n", MY_VEHICLE_ID);

  // Motor pins
  pinMode(mastMotor0, OUTPUT);
  pinMode(mastMotor1, OUTPUT);
  pinMode(auxAttach0, OUTPUT);
  pinMode(auxAttach1, OUTPUT);
  digitalWrite(auxAttach0, LOW);
  digitalWrite(auxAttach1, LOW);
  pinMode(leftMotor0, OUTPUT);
  pinMode(leftMotor1, OUTPUT);
  pinMode(rightMotor0, OUTPUT);
  pinMode(rightMotor1, OUTPUT);

  // Servos
  steeringServo.attach(steeringServoPin);
  steeringServo.write(adjustedSteeringValue);
  mastTiltServo.attach(mastTiltServoPin);
  mastTiltServo.write(mastTiltValue);

  // Init WiFi in station mode for ESP-NOW
  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  Serial.printf("Vehicle MAC: %s\n", WiFi.macAddress().c_str());

  // Init ESP-NOW
  if (esp_now_init() != ESP_OK) {
    Serial.println("ERROR: ESP-NOW init failed!");
    return;
  }
  esp_now_register_recv_cb(onDataRecv);

  Serial.println("Listening for hub commands...");
}

// ─── Loop ────────────────────────────────────────────────────────
unsigned long lastPacketTime = 0;

void loop() {
  if (newPacket) {
    newPacket = false;
    lastPacketTime = millis();
    processPacket(latestPacket);
  }

  // Safety timeout: stop if no packet for 500ms
  if (millis() - lastPacketTime > 500 && lastPacketTime > 0) {
    allStop();
    lastPacketTime = 0;
    Serial.println("No signal — motors stopped");
  }

  vTaskDelay(1);
}
