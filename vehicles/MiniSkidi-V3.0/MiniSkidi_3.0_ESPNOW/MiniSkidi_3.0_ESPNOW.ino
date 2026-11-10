// MiniSkidi 3.0 — ESP-NOW receiver version
//
// Replaces the WiFi AP + WebSocket interface with ESP-NOW packets
// from the RC Fleet Hub. All motor/servo/light logic is unchanged.
//
// Control mapping (tank-drive — each stick drives one track):
//   Left  stick Y  (axisY)  → left  motor forward/backward
//   Right stick Y  (axisRY) → right motor forward/backward
//   D-pad up/down           → arm motor up/down
//   R1 / R2                 → bucket servo +2° / -2° per packet
//   L1 / L2                 → aux servo   +2° / -2° per packet
//   Thumb R (edge)          → light toggle
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
#define MY_VEHICLE_ID 2  // "Skid Steer"

// ─── Pin defines (unchanged from original) ───────────────────────
#define bucketServoPin 23
#define auxServoPin    22
#define lightPin1      18
#define lightPin2      5

#define RIGHT_MOTOR 1
#define LEFT_MOTOR  0
#define ARM_MOTOR   2

#define FORWARD   1
#define BACKWARD -1
#define STOP      0

// Stick dead zone — ignore values closer to center than this
#define STICK_DEADZONE 100

// Servo travel limits and initial positions
#define BUCKET_MIN  60
#define BUCKET_MAX 180
#define BUCKET_INIT 140
#define AUX_MIN     60
#define AUX_MAX    180
#define AUX_INIT   150

// ─── Hardware ────────────────────────────────────────────────────
struct MOTOR_PINS { int pinIN1; int pinIN2; };

MOTOR_PINS motorPins[] = {
  {25, 26},  // RIGHT_MOTOR (IN1, IN2)
  {33, 32},  // LEFT_MOTOR
  {21, 19},  // ARM_MOTOR
};

Servo bucketServo;
Servo auxServo;

// ─── State ───────────────────────────────────────────────────────
int bucketPos = BUCKET_INIT;
int auxPos    = AUX_INIT;

bool light             = false;
bool removeArmMomentum = false;
bool thumbRWasPressed  = false;

// Latest packet from hub (written in callback, read in loop)
volatile bool newPacket = false;
GamepadPacket latestPacket;

// ─── Motor control (unchanged from original) ─────────────────────
void rotateMotor(int motorNumber, int motorDirection) {
  if (motorDirection == FORWARD) {
    digitalWrite(motorPins[motorNumber].pinIN1, HIGH);
    digitalWrite(motorPins[motorNumber].pinIN2, LOW);
  } else if (motorDirection == BACKWARD) {
    digitalWrite(motorPins[motorNumber].pinIN1, LOW);
    digitalWrite(motorPins[motorNumber].pinIN2, HIGH);
  } else {
    if (motorNumber == ARM_MOTOR && removeArmMomentum) {
      // Brief counter-pulse to kill arm momentum on direction change
      digitalWrite(motorPins[ARM_MOTOR].pinIN1, HIGH);
      digitalWrite(motorPins[ARM_MOTOR].pinIN2, LOW);
      delay(10);
      digitalWrite(motorPins[ARM_MOTOR].pinIN1, LOW);
      digitalWrite(motorPins[ARM_MOTOR].pinIN2, LOW);
      delay(5);
      digitalWrite(motorPins[ARM_MOTOR].pinIN1, HIGH);
      digitalWrite(motorPins[ARM_MOTOR].pinIN2, LOW);
      delay(10);
      removeArmMomentum = false;
    }
    digitalWrite(motorPins[motorNumber].pinIN1, LOW);
    digitalWrite(motorPins[motorNumber].pinIN2, LOW);
  }
}

// ─── Light control (unchanged from original) ─────────────────────
void lightControl() {
  if (!light) {
    digitalWrite(lightPin1, HIGH);
    digitalWrite(lightPin2, LOW);
    light = true;
  } else {
    digitalWrite(lightPin1, LOW);
    digitalWrite(lightPin2, LOW);
    light = false;
  }
}

// ─── Safety: stop everything when hub goes silent ────────────────
void allStop() {
  rotateMotor(LEFT_MOTOR,  STOP);
  rotateMotor(RIGHT_MOTOR, STOP);
  rotateMotor(ARM_MOTOR,   STOP);
}

// ─── ESP-NOW receive callback ────────────────────────────────────
void onDataRecv(const esp_now_recv_info_t *info, const uint8_t *data, int len) {
  if (len != sizeof(GamepadPacket)) return;

  GamepadPacket pkt;
  memcpy(&pkt, data, sizeof(pkt));

  if (pkt.vehicleId != MY_VEHICLE_ID) return;

  latestPacket = pkt;
  newPacket = true;
}

// ─── Process a received packet ───────────────────────────────────
void processPacket(GamepadPacket &pkt) {
  // Tank drive: left stick Y → left motor, right stick Y → right motor
  int leftDir  = (pkt.axisY   >  STICK_DEADZONE) ? FORWARD
               : (pkt.axisY   < -STICK_DEADZONE) ? BACKWARD : STOP;
  int rightDir = (pkt.axisRY  >  STICK_DEADZONE) ? FORWARD
               : (pkt.axisRY  < -STICK_DEADZONE) ? BACKWARD : STOP;
  rotateMotor(LEFT_MOTOR,  leftDir);
  rotateMotor(RIGHT_MOTOR, rightDir);

  // Arm: d-pad up = raise, down = lower
  if (pkt.dpad == 1) {
    rotateMotor(ARM_MOTOR, FORWARD);
    removeArmMomentum = false;
  } else if (pkt.dpad == 2) {
    rotateMotor(ARM_MOTOR, BACKWARD);
    removeArmMomentum = true;
  } else {
    rotateMotor(ARM_MOTOR, STOP);
  }

  // Bucket servo: R1 raises, R2 lowers
   if (pkt.buttons & BTN_R1) bucketPos = min(BUCKET_MAX, bucketPos + 5);
   if (pkt.buttons & BTN_R2) bucketPos = max(BUCKET_MIN, bucketPos - 5);
  bucketServo.write(bucketPos);

  // Aux servo: L1 raises, L2 lowers
  if (pkt.buttons & BTN_L1) auxPos = min(AUX_MAX, auxPos + 2);
  if (pkt.buttons & BTN_L2) auxPos = max(AUX_MIN, auxPos - 2);
  auxServo.write(auxPos);

  // Lights: thumb R toggles, edge-triggered
  bool thumbR = (pkt.misc & MISC_THUMB_R) != 0;
  if (thumbR && !thumbRWasPressed) lightControl();
  thumbRWasPressed = thumbR;
}

// ─── Setup ───────────────────────────────────────────────────────
void setup() {
  Serial.begin(115200);

  for (int i = 0; i < 3; i++) {
    pinMode(motorPins[i].pinIN1, OUTPUT);
    pinMode(motorPins[i].pinIN2, OUTPUT);
  }
  allStop();

  bucketServo.attach(bucketServoPin);
  auxServo.attach(auxServoPin);
  bucketServo.write(bucketPos);
  auxServo.write(auxPos);

  pinMode(lightPin1, OUTPUT);
  pinMode(lightPin2, OUTPUT);
  digitalWrite(lightPin1, LOW);
  digitalWrite(lightPin2, LOW);

  WiFi.mode(WIFI_STA);
  WiFi.disconnect();

  if (esp_now_init() != ESP_OK) {
    Serial.println("ERROR: ESP-NOW init failed!");
    return;
  }
  esp_now_register_recv_cb(onDataRecv);

  Serial.printf("\n=== MiniSkidi 3.0 (ESP-NOW) === Vehicle ID: %d\n", MY_VEHICLE_ID);
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

  // Safety timeout: stop everything if hub goes silent for 500ms
  if (lastPacketTime > 0 && millis() - lastPacketTime > 500) {
    allStop();
    lastPacketTime = 0;
    Serial.println("No signal — motors stopped");
  }

  vTaskDelay(1);
}
