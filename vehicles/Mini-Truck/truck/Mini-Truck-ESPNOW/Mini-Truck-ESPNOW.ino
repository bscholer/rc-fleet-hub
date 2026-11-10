// Mini Truck — ESP-NOW receiver version
//
// Replaces direct Bluepad32 controller input with ESP-NOW packets
// from the RC Fleet Hub. All motor/servo/light/trailer logic is unchanged.
//
// Board setup (Arduino IDE):
//   1. Use STANDARD ESP32 board manager (Espressif), NOT Bluepad32
//   2. Select board: "ESP32 Dev Module"
//   3. Libraries needed: ESP32Servo
//   4. WiFi + esp_now are built-in — no install needed
//
// NOTE: Add this vehicle to rc_fleet_hub's vehicleNames[] at index MY_VEHICLE_ID
// and uncomment the matching entry there before flashing the hub.

#include <Arduino.h>
#include <ESP32Servo.h>
#include <WiFi.h>
#include <esp_now.h>
#include "gamepad_packet.h"

// ─── Vehicle identity ────────────────────────────────────────────
// Must match the index in the hub's vehicleNames[] array.
#define MY_VEHICLE_ID 3

// ─── Pin defines (unchanged from original) ───────────────────────
#define LT1 15
#define LT2 27
#define LT3 14

#define frontSteeringServoPin 23
#define hitchServoPin 22

#define frontMotor0 33
#define frontMotor1 32
#define rearMotor0  2
#define rearMotor1  4
#define rearMotor2  12
#define rearMotor3  13

#define auxAttach0 18
#define auxAttach1 5
#define auxAttach2 17
#define auxAttach3 16
#define auxAttach4 25
#define auxAttach5 26

// ─── Hardware ────────────────────────────────────────────────────
Servo frontSteeringServo;
Servo hitchServo;

// ─── State ───────────────────────────────────────────────────────
int adjustedSteeringValue = 90;
int hitchServoValue = 160;
int steeringTrim = 0;
int lightMode = 0;
int lightSwitchButtonTime = 0;
int lightSwitchTime = 0;

bool lightsOn = false;
bool auxLightsOn = false;
bool blinkLT = false;
bool hazardLT = false;
bool hazardsOn = false;
bool smokeGenOn = false;
bool trailerAuxMtr1Forward = false;
bool trailerAuxMtr1Reverse = false;
bool trailerAuxMtr2Forward = false;
bool trailerAuxMtr2Reverse = false;
bool hitchUp = true;
unsigned long hitchMoveTime = 0;

// Latest packet from hub (written in callback, read in loop)
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

// ─── Motor helpers ───────────────────────────────────────────────
void moveMotor(int motorPin0, int motorPin1, int velocity) {
  if (velocity > 15) {
    analogWrite(motorPin0, velocity);
    analogWrite(motorPin1, LOW);
  } else if (velocity < -15) {
    analogWrite(motorPin0, LOW);
    analogWrite(motorPin1, (-1 * velocity));
  } else {
    analogWrite(motorPin0, 0);
    analogWrite(motorPin1, 0);
  }
}

// ─── Process functions (logic unchanged from original) ───────────
void processThrottle(int axisYValue) {
  int adjustedThrottleValue = axisYValue / 2;
  int smokeThrottle = adjustedThrottleValue / 3;
  moveMotor(rearMotor0, rearMotor1, adjustedThrottleValue);
  moveMotor(rearMotor2, rearMotor3, adjustedThrottleValue);
  moveMotor(frontMotor0, frontMotor1, adjustedThrottleValue);
  moveMotor(auxAttach2, auxAttach3, smokeThrottle);
}

void processSteering(int axisRXValue) {
  adjustedSteeringValue = (90 - (axisRXValue / 9)) - steeringTrim;
  frontSteeringServo.write(180 - adjustedSteeringValue);
}

void processTrimAndHitch(int dpadValue) {
  if (dpadValue == 4 && steeringTrim < 20) {
    steeringTrim = steeringTrim + 1;
    delay(50);
  } else if (dpadValue == 8 && steeringTrim > -20) {
    steeringTrim = steeringTrim - 1;
    delay(50);
  }
  if (dpadValue == 1) {
    if (!hitchServo.attached()) hitchServo.attach(hitchServoPin);
    hitchServo.write(hitchServoValue);
    hitchUp = true;
    hitchMoveTime = millis();
  } else if (dpadValue == 2) {
    if (!hitchServo.attached()) hitchServo.attach(hitchServoPin);
    hitchServo.write(65);
    hitchUp = false;
    hitchMoveTime = millis();
  }

  // Detach after 500ms when up — gravity holds position, prevents hunting buzz.
  // When down, stay attached so the servo holds against the load.
}

void processLights(bool buttonValue) {
  if (buttonValue && (millis() - lightSwitchButtonTime) > 300) {
    lightMode++;
    if (lightMode == 1) {
      digitalWrite(LT1, HIGH);
      digitalWrite(LT2, HIGH);
      Serial.println(12);
      delay(10);
      Serial.println(14);
    } else if (lightMode == 2) {
      digitalWrite(LT1, LOW);
      digitalWrite(LT2, LOW);
      delay(100);
      digitalWrite(LT1, HIGH);
      digitalWrite(LT2, HIGH);
      blinkLT = true;
    } else if (lightMode == 3) {
      blinkLT = false;
      hazardLT = true;
    } else if (lightMode == 4) {
      hazardLT = false;
      digitalWrite(LT1, LOW);
      digitalWrite(LT2, LOW);
      Serial.println(11);
      delay(10);
      Serial.println(13);
      lightMode = 0;
      if (!auxLightsOn) {
        digitalWrite(LT3, HIGH);
        Serial.println(16);
        auxLightsOn = true;
      } else {
        digitalWrite(LT3, LOW);
        Serial.println(15);
        auxLightsOn = false;
      }
    }
    lightSwitchButtonTime = millis();
  }
}

void processSmokeGen(bool buttonValue) {
  if (buttonValue) {
    if (!smokeGenOn) {
      digitalWrite(auxAttach0, LOW);
      digitalWrite(auxAttach1, HIGH);
      smokeGenOn = true;
    } else {
      digitalWrite(auxAttach0, LOW);
      digitalWrite(auxAttach1, LOW);
      smokeGenOn = false;
    }
  }
}

void processTrailerLegsUp(int value) {
  if (value == 1) { Serial.println(1); delay(10); }
}
void processTrailerLegsDown(int value) {
  if (value == 1) { Serial.println(2); delay(10); }
}
void processTrailerRampUp(int value) {
  if (value == 1) { Serial.println(3); delay(10); }
}
void processTrailerRampDown(int value) {
  if (value == 1) { Serial.println(4); delay(10); }
}

void processTrailerAuxMtr1Forward(bool value) {
  if (value) {
    Serial.println(5); delay(10);
    trailerAuxMtr1Forward = true;
  } else if (trailerAuxMtr1Forward) {
    Serial.println(7); delay(10);
    trailerAuxMtr1Forward = false;
  }
}
void processTrailerAuxMtr1Reverse(bool value) {
  if (value) {
    Serial.println(6); delay(10);
    trailerAuxMtr1Reverse = true;
  } else if (trailerAuxMtr1Reverse) {
    Serial.println(7); delay(10);
    trailerAuxMtr1Reverse = false;
  }
}
void processTrailerAuxMtr2Forward(bool value) {
  if (value) {
    Serial.println(8); delay(10);
    trailerAuxMtr2Forward = true;
  } else if (trailerAuxMtr2Forward) {
    Serial.println(10); delay(10);
    trailerAuxMtr2Forward = false;
  }
}
void processTrailerAuxMtr2Reverse(bool value) {
  if (value) {
    Serial.println(9); delay(10);
    trailerAuxMtr2Reverse = true;
  } else if (trailerAuxMtr2Reverse) {
    Serial.println(10); delay(10);
    trailerAuxMtr2Reverse = false;
  }
}

// ─── Process a received packet ───────────────────────────────────
void processPacket(GamepadPacket &pkt) {
  processThrottle(pkt.axisY);
  processSteering(pkt.axisRX);
  processTrimAndHitch(pkt.dpad);
  processLights((pkt.misc & MISC_THUMB_R) != 0);
  processSmokeGen((pkt.misc & MISC_THUMB_L) != 0);

  processTrailerLegsUp(   (pkt.buttons & BTN_Y) ? 1 : 0);
  processTrailerLegsDown( (pkt.buttons & BTN_A) ? 1 : 0);
  processTrailerRampUp(   (pkt.buttons & BTN_B) ? 1 : 0);
  processTrailerRampDown( (pkt.buttons & BTN_X) ? 1 : 0);

  processTrailerAuxMtr1Forward((pkt.buttons & BTN_R1) != 0);
  processTrailerAuxMtr1Reverse((pkt.buttons & BTN_R2) != 0);
  processTrailerAuxMtr2Forward((pkt.buttons & BTN_L1) != 0);
  processTrailerAuxMtr2Reverse((pkt.buttons & BTN_L2) != 0);

  // Turn signals / hazards (run every packet, same as original)
  if (blinkLT && (millis() - lightSwitchTime) > 300) {
    if (!lightsOn) {
      if (adjustedSteeringValue <= 85) {
        digitalWrite(LT1, HIGH); Serial.println(12);
      } else if (adjustedSteeringValue >= 95) {
        digitalWrite(LT2, HIGH); Serial.println(14);
      }
      lightsOn = true;
    } else {
      if (adjustedSteeringValue <= 85) {
        digitalWrite(LT2, HIGH); digitalWrite(LT1, LOW);
        Serial.println(11); delay(10); Serial.println(14);
      } else if (adjustedSteeringValue >= 95) {
        digitalWrite(LT1, HIGH); digitalWrite(LT2, LOW);
        Serial.println(13); delay(10); Serial.println(12);
      }
      lightsOn = false;
    }
    lightSwitchTime = millis();
  }
  if (blinkLT && adjustedSteeringValue > 85 && adjustedSteeringValue < 95) {
    digitalWrite(LT1, HIGH); digitalWrite(LT2, HIGH);
    Serial.println(12); delay(10); Serial.println(14);
  }
  if (hazardLT && (millis() - lightSwitchTime) > 300) {
    if (!hazardsOn) {
      digitalWrite(LT1, HIGH); digitalWrite(LT2, HIGH);
      Serial.println(12); delay(10); Serial.println(14);
      hazardsOn = true;
    } else {
      digitalWrite(LT1, LOW); digitalWrite(LT2, LOW);
      Serial.println(11); delay(10); Serial.println(13);
      hazardsOn = false;
    }
    lightSwitchTime = millis();
  }
}

// ─── Safety: stop everything when hub goes silent ────────────────
void allStop() {
  analogWrite(frontMotor0, 0); analogWrite(frontMotor1, 0);
  analogWrite(rearMotor0,  0); analogWrite(rearMotor1,  0);
  analogWrite(rearMotor2,  0); analogWrite(rearMotor3,  0);
  analogWrite(auxAttach2,  0); analogWrite(auxAttach3,  0);  // smoke fan
  digitalWrite(auxAttach0, LOW); digitalWrite(auxAttach1, LOW);  // smoke heater
  Serial.println(7);   // trailer auxMotor1 STOP
  delay(10);
  Serial.println(10);  // trailer auxMotor2 STOP
  delay(10);
  trailerAuxMtr1Forward = false;
  trailerAuxMtr1Reverse = false;
  trailerAuxMtr2Forward = false;
  trailerAuxMtr2Reverse = false;
  smokeGenOn = false;
}

// ─── Setup ───────────────────────────────────────────────────────
void setup() {
  Serial.begin(115200);

  pinMode(frontMotor0, OUTPUT); pinMode(frontMotor1, OUTPUT);
  pinMode(rearMotor0,  OUTPUT); pinMode(rearMotor1,  OUTPUT);
  pinMode(rearMotor2,  OUTPUT); pinMode(rearMotor3,  OUTPUT);
  pinMode(auxAttach0,  OUTPUT); pinMode(auxAttach1,  OUTPUT);
  pinMode(auxAttach2,  OUTPUT); pinMode(auxAttach3,  OUTPUT);
  pinMode(auxAttach4,  OUTPUT); pinMode(auxAttach5,  OUTPUT);
  pinMode(LT1, OUTPUT); pinMode(LT2, OUTPUT); pinMode(LT3, OUTPUT);

  digitalWrite(frontMotor0, LOW); digitalWrite(frontMotor1, LOW);
  digitalWrite(rearMotor0,  LOW); digitalWrite(rearMotor1,  LOW);
  digitalWrite(rearMotor2,  LOW); digitalWrite(rearMotor3,  LOW);
  digitalWrite(auxAttach0,  LOW); digitalWrite(auxAttach1,  LOW);
  digitalWrite(auxAttach2,  LOW); digitalWrite(auxAttach3,  LOW);
  digitalWrite(auxAttach4,  LOW); digitalWrite(auxAttach5,  LOW);
  digitalWrite(LT1, LOW); digitalWrite(LT2, LOW); digitalWrite(LT3, LOW);

  frontSteeringServo.attach(frontSteeringServoPin);
  frontSteeringServo.write(adjustedSteeringValue);
  hitchServo.attach(hitchServoPin);
  hitchServo.write(hitchServoValue);

  WiFi.mode(WIFI_STA);
  WiFi.disconnect();

  if (esp_now_init() != ESP_OK) {
    Serial.println("ERROR: ESP-NOW init failed!");
    return;
  }
  esp_now_register_recv_cb(onDataRecv);

  Serial.printf("\n=== Mini Truck (ESP-NOW) === Vehicle ID: %d\n", MY_VEHICLE_ID);
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

  // Hitch servo detach: only when up, after 500ms
  if (hitchUp && hitchServo.attached() && hitchMoveTime > 0 && (millis() - hitchMoveTime) > 500) {
    hitchServo.detach();
  }

  // Safety timeout: stop everything if hub goes silent for 500ms
  if (millis() - lastPacketTime > 500 && lastPacketTime > 0) {
    allStop();
    lastPacketTime = 0;
    Serial.println("No signal — motors stopped");
  }

  vTaskDelay(1);
}
