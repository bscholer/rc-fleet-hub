// MiniLift truck — ESP-NOW receiver for the RC Fleet Hub.
//
// Drives the truck directly. In crane mode (Y toggles) it forwards crane commands
// over Serial to the crane daughter-board (MiniLift_Crane). Do NOT print debug
// text to Serial here: the crane board parses every line.
//
// Board setup: standard Espressif ESP32 package, "ESP32 Dev Module".
// Library: ESP32Servo by Kevin Harrington.

#include <Arduino.h>
#include <ESP32Servo.h>
#include <WiFi.h>
#include <esp_now.h>
#include "gamepad_packet.h"

// Must match the hub's vehicleNames[] index.
#define MY_VEHICLE_ID 5

#define frontSteeringServoPin 23
#define rearSteeringServoPin 22
#define frontMotor0 32
#define frontMotor1 33
#define rearMotor0 4
#define rearMotor1 2

#define frontOutriggerServoPin 21
#define rearOutriggerServoPin 19
#define frontOutriggerMotor0 25
#define frontOutriggerMotor1 26
#define rearOutriggerMotor0 12
#define rearOutriggerMotor1 13

#define auxAttach0 17
#define auxAttach1 16
#define auxAttach2 18
#define auxAttach3 5

Servo rearSteeringServo;
Servo frontSteeringServo;
Servo frontOutriggerServo;
Servo rearOutriggerServo;

unsigned long lightSwitchTime = 0;
unsigned long outriggerServosTimeout = 0;
int adjustedSteeringValue = 86;
int steeringTrim = 0;
bool lightsOn = false;
bool lightsOn2 = false;
bool craneMode = false;
bool oRDirection = false;
bool crabWalkOn = false;

// Previous-packet button states for toggle edge detection
bool prevY = false, prevB = false, prevX = false;
unsigned long lastTrimTime = 0;

volatile bool newPacket = false;
GamepadPacket latestPacket;
unsigned long lastPacketTime = 0;

void onDataRecv(const esp_now_recv_info_t *info, const uint8_t *data, int len) {
  if (len != sizeof(GamepadPacket)) return;
  GamepadPacket pkt;
  memcpy(&pkt, data, sizeof(pkt));
  if (pkt.vehicleId != MY_VEHICLE_ID) return;
  latestPacket = pkt;
  newPacket = true;
}

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

// Crane daughter-board protocol: "<id>,<speed>" to move, "<id>" to stop.
void craneAxis(int axis, int posId, int negId, int stopId) {
  int speed = axis / 2;
  if (axis > 60) Serial.printf("%d,%d\n", posId, speed);
  else if (axis < -60) Serial.printf("%d,%d\n", negId, speed);
  else Serial.println(stopId);
}

void craneStopAll() {
  Serial.println(3);
  Serial.println(6);
  Serial.println(9);
  Serial.println(12);
  Serial.println(15);
}

void toggleOnPress(bool pressed, bool &prev, bool &state) {
  if (pressed && !prev) state = !state;
  prev = pressed;
}

void processMode(bool y) {
  bool was = craneMode;
  toggleOnPress(y, prevY, craneMode);
  if (was != craneMode) {
    moveMotor(rearMotor0, rearMotor1, 0);
    moveMotor(frontMotor0, frontMotor1, 0);
    craneStopAll();
  }
}

void processThrottleAndBoom(int axisY) {
  if (!craneMode) {
    int v = axisY / 2;
    moveMotor(rearMotor0, rearMotor1, v);
    moveMotor(frontMotor0, frontMotor1, v);
  } else {
    craneAxis(axisY, 1, 2, 3);
  }
}

void processPivot(int axisX) {
  if (craneMode) craneAxis(axisX, 4, 5, 6);
}

void processExtension(int axisRY) {
  if (craneMode) craneAxis(axisRY, 7, 8, 9);
}

void processTrimAndCables(int dpad) {
  if (!craneMode) {
    if (millis() - lastTrimTime < 50) return;
    if (dpad == 1 && steeringTrim < 20) {
      steeringTrim += 2;
      lastTrimTime = millis();
    } else if (dpad == 2 && steeringTrim > -20) {
      steeringTrim -= 2;
      lastTrimTime = millis();
    }
  } else {
    // Winch: up=out(10), down=in(11). Ballast: right=up(13), left=down(14).
    Serial.println(dpad == 1 ? 10 : dpad == 2 ? 11 : 12);
    Serial.println(dpad == 4 ? 13 : dpad == 8 ? 14 : 15);
  }
}

void processSteering(int axisRX) {
  adjustedSteeringValue = (90 - (axisRX / 17)) - steeringTrim;
  frontSteeringServo.write(adjustedSteeringValue);
  rearSteeringServo.write(crabWalkOn ? adjustedSteeringValue : 180 - adjustedSteeringValue);
}

void driveOutrigger(Servo &servo, int servoPin, int angle, int pin0, int pin1) {
  outriggerServosTimeout = millis();
  servo.attach(servoPin);
  servo.write(angle);
  digitalWrite(pin0, oRDirection ? HIGH : LOW);
  digitalWrite(pin1, oRDirection ? LOW : HIGH);
}

void stopOutrigger(int pin0, int pin1) {
  digitalWrite(pin0, LOW);
  digitalWrite(pin1, LOW);
}

// Rear: L2/R2. Front: L1/R1. Each pair shares one motor, servo angle selects the leg.
void processOutriggers(bool rl, bool rr, bool fl, bool fr) {
  if (rl) driveOutrigger(rearOutriggerServo, rearOutriggerServoPin, 80, rearOutriggerMotor0, rearOutriggerMotor1);
  else if (rr) driveOutrigger(rearOutriggerServo, rearOutriggerServoPin, 125, rearOutriggerMotor0, rearOutriggerMotor1);
  else stopOutrigger(rearOutriggerMotor0, rearOutriggerMotor1);

  if (fl) driveOutrigger(frontOutriggerServo, frontOutriggerServoPin, 125, frontOutriggerMotor0, frontOutriggerMotor1);
  else if (fr) driveOutrigger(frontOutriggerServo, frontOutriggerServoPin, 80, frontOutriggerMotor0, frontOutriggerMotor1);
  else stopOutrigger(frontOutriggerMotor0, frontOutriggerMotor1);
}

void toggleAux(bool pressed, bool &state, int pin0, int pin1) {
  if (pressed && (millis() - lightSwitchTime) > 200) {
    state = !state;
    digitalWrite(pin0, state ? HIGH : LOW);
    digitalWrite(pin1, LOW);
    lightSwitchTime = millis();
  }
}

void processPacket(GamepadPacket &pkt) {
  processMode(pkt.buttons & BTN_Y);
  toggleOnPress(pkt.buttons & BTN_B, prevB, oRDirection);
  toggleOnPress(pkt.buttons & BTN_X, prevX, crabWalkOn);

  processThrottleAndBoom(pkt.axisY);
  processPivot(pkt.axisX);
  processExtension(pkt.axisRY);
  processSteering(pkt.axisRX);
  processTrimAndCables(pkt.dpad);

  toggleAux(pkt.misc & MISC_THUMB_R, lightsOn, auxAttach0, auxAttach1);
  toggleAux(pkt.misc & MISC_THUMB_L, lightsOn2, auxAttach2, auxAttach3);

  processOutriggers(pkt.buttons & BTN_L2, pkt.buttons & BTN_R2, pkt.buttons & BTN_L1, pkt.buttons & BTN_R1);
}

void allStop() {
  moveMotor(rearMotor0, rearMotor1, 0);
  moveMotor(frontMotor0, frontMotor1, 0);
  stopOutrigger(frontOutriggerMotor0, frontOutriggerMotor1);
  stopOutrigger(rearOutriggerMotor0, rearOutriggerMotor1);
  craneStopAll();
}

void setup() {
  Serial.begin(115200);

  // Servos first: analogWrite on the motor pins would claim the LEDC timers they need.
  ESP32PWM::allocateTimer(0);
  ESP32PWM::allocateTimer(1);
  ESP32PWM::allocateTimer(2);
  ESP32PWM::allocateTimer(3);
  rearSteeringServo.setPeriodHertz(50);
  frontSteeringServo.setPeriodHertz(50);
  frontOutriggerServo.setPeriodHertz(50);
  rearOutriggerServo.setPeriodHertz(50);
  rearSteeringServo.attach(rearSteeringServoPin);
  rearSteeringServo.write(adjustedSteeringValue);
  frontSteeringServo.attach(frontSteeringServoPin);
  frontSteeringServo.write(adjustedSteeringValue);
  frontOutriggerServo.attach(frontOutriggerServoPin);
  frontOutriggerServo.write(90);
  rearOutriggerServo.attach(rearOutriggerServoPin);
  rearOutriggerServo.write(90);
  outriggerServosTimeout = millis();

  const int outputPins[] = {
    frontOutriggerMotor0, frontOutriggerMotor1, rearOutriggerMotor0, rearOutriggerMotor1,
    auxAttach0, auxAttach1, auxAttach2, auxAttach3,
    rearMotor0, rearMotor1, frontMotor0, frontMotor1
  };
  for (int pin : outputPins) {
    pinMode(pin, OUTPUT);
    digitalWrite(pin, LOW);
  }

  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  if (esp_now_init() != ESP_OK) return;
  esp_now_register_recv_cb(onDataRecv);
}

void loop() {
  if (newPacket) {
    newPacket = false;
    lastPacketTime = millis();
    processPacket(latestPacket);
  }

  // Failsafe: stop everything if the hub goes silent
  if (lastPacketTime > 0 && millis() - lastPacketTime > 500) {
    allStop();
    lastPacketTime = 0;
  }

  // Detach outrigger servos when idle to stop jitter
  if (millis() - outriggerServosTimeout > 5000) {
    rearOutriggerServo.detach();
    frontOutriggerServo.detach();
  }

  vTaskDelay(1);
}
