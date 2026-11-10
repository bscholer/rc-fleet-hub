// Mini Excavator — ESP-NOW receiver version
//
// Replaces direct Bluepad32 controller input with ESP-NOW packets
// from the RC Fleet Hub. All motor/servo logic is unchanged.
//
// Board setup (Arduino IDE):
//   1. Use STANDARD ESP32 board manager (Espressif), NOT Bluepad32
//   2. Select board: "ESP32 Dev Module"
//   3. Libraries needed: ESP32Servo, Adafruit MCP23X17
//   4. WiFi + esp_now are built-in — no install needed

#include <ESP32Servo.h>  // by Kevin Harrington
#include <WiFi.h>
#include <esp_now.h>
#include "Adafruit_MCP23X17.h"
#include "gamepad_packet.h"

// ─── Vehicle identity ────────────────────────────────────────────
// Change this to match the vehicle's index in the hub's list.
#define MY_VEHICLE_ID 0

// ─── Pin defines (unchanged from original) ───────────────────────
#define clawServoPin 5
#define auxServoPin 18
#define cabLights 32
#define auxLights 33

#define pivot0 15
#define pivot1 14
#define mainBoom0 9
#define mainBoom1 8
#define dipper0 3
#define dipper1 2
#define tiltAttach0 0
#define tiltAttach1 1
#define thumb0 11
#define thumb1 10
#define auxAttach0 12
#define auxAttach1 13

#define leftMotor0 5
#define leftMotor1 4
#define rightMotor0 6
#define rightMotor1 7

// ─── Hardware ────────────────────────────────────────────────────
Adafruit_MCP23X17 mcp;
Servo clawServo;
Servo auxServo;

// ─── State ───────────────────────────────────────────────────────
int clawServoValue = 90;
int auxServoValue = 90;
int servoDelay = 0;
int lightSwitchTime = 0;

bool cabLightsOn = false;
bool auxLightsOn = false;
bool moveClawServoUp = false;
bool moveClawServoDown = false;
bool moveAuxServoUp = false;
bool moveAuxServoDown = false;

// Latest packet from hub (written in callback, read in loop)
volatile bool newPacket = false;
GamepadPacket latestPacket;

// ─── ESP-NOW receive callback ────────────────────────────────────
void onDataRecv(const esp_now_recv_info_t *info, const uint8_t *data, int len) {
  if (len != sizeof(GamepadPacket)) return;

  GamepadPacket pkt;
  memcpy(&pkt, data, sizeof(pkt));

  // Only process packets addressed to this vehicle
  if (pkt.vehicleId != MY_VEHICLE_ID) return;

  latestPacket = pkt;
  newPacket = true;
}

// ─── Motor/servo processing (same logic as original) ─────────────
void processBoom(int axisYValue) {
  int adjustedValue = axisYValue / 2;
  if (adjustedValue > 100) {
    mcp.digitalWrite(mainBoom0, HIGH);
    mcp.digitalWrite(mainBoom1, LOW);
  } else if (adjustedValue < -100) {
    mcp.digitalWrite(mainBoom0, LOW);
    mcp.digitalWrite(mainBoom1, HIGH);
  } else {
    mcp.digitalWrite(mainBoom0, LOW);
    mcp.digitalWrite(mainBoom1, LOW);
  }
}

void processPivot(int axisYValue) {
  int adjustedValue = axisYValue / 2;
  if (adjustedValue > 100) {
    mcp.digitalWrite(pivot0, HIGH);
    mcp.digitalWrite(pivot1, LOW);
  } else if (adjustedValue < -100) {
    mcp.digitalWrite(pivot0, LOW);
    mcp.digitalWrite(pivot1, HIGH);
  } else {
    mcp.digitalWrite(pivot0, LOW);
    mcp.digitalWrite(pivot1, LOW);
  }
}

void processDipper(int axisYValue) {
  int adjustedValue = axisYValue / 2;
  if (adjustedValue > 100) {
    mcp.digitalWrite(dipper0, HIGH);
    mcp.digitalWrite(dipper1, LOW);
  } else if (adjustedValue < -100) {
    mcp.digitalWrite(dipper0, LOW);
    mcp.digitalWrite(dipper1, HIGH);
  } else {
    mcp.digitalWrite(dipper0, LOW);
    mcp.digitalWrite(dipper1, LOW);
  }
}

void processBucket(int axisYValue) {
  int adjustedValue = axisYValue / 2;
  if (adjustedValue > 100) {
    mcp.digitalWrite(tiltAttach0, HIGH);
    mcp.digitalWrite(tiltAttach1, LOW);
  } else if (adjustedValue < -100) {
    mcp.digitalWrite(tiltAttach0, LOW);
    mcp.digitalWrite(tiltAttach1, HIGH);
  } else {
    mcp.digitalWrite(tiltAttach0, LOW);
    mcp.digitalWrite(tiltAttach1, LOW);
  }
}

void processAux(int dpadValue) {
  if (dpadValue == 1) {
    mcp.digitalWrite(thumb0, HIGH);
    mcp.digitalWrite(thumb1, LOW);
  } else if (dpadValue == 2) {
    mcp.digitalWrite(thumb0, LOW);
    mcp.digitalWrite(thumb1, HIGH);
  } else {
    mcp.digitalWrite(thumb0, LOW);
    mcp.digitalWrite(thumb1, LOW);
  }
  if (dpadValue == 4) {
    mcp.digitalWrite(auxAttach0, HIGH);
    mcp.digitalWrite(auxAttach1, LOW);
  } else if (dpadValue == 8) {
    mcp.digitalWrite(auxAttach0, LOW);
    mcp.digitalWrite(auxAttach1, HIGH);
  } else {
    mcp.digitalWrite(auxAttach0, LOW);
    mcp.digitalWrite(auxAttach1, LOW);
  }
}

// ─── Process a received packet (replaces processGamepad) ─────────
void processPacket(GamepadPacket &pkt) {
  // Axes → motors
  processBoom(pkt.axisY);
  processPivot(pkt.axisX);
  processDipper(pkt.axisRY);
  processBucket(pkt.axisRX);

  // D-pad → aux motors
  processAux(pkt.dpad);

  // Lights (thumbL / thumbR)
  if ((millis() - lightSwitchTime) > 200) {
    if (pkt.misc & MISC_THUMB_R) {
      if (!cabLightsOn) {
        digitalWrite(cabLights, HIGH);
        cabLightsOn = true;
      } else {
        digitalWrite(cabLights, LOW);
        cabLightsOn = false;
      }
      delay(80);
    }
    if (pkt.misc & MISC_THUMB_L) {
      if (!auxLightsOn) {
        digitalWrite(auxLights, HIGH);
        auxLightsOn = true;
      } else {
        digitalWrite(auxLights, LOW);
        auxLightsOn = false;
      }
      delay(80);
    }
    lightSwitchTime = millis();
  }

  // Drive motors (shoulder buttons)
  if (pkt.buttons & BTN_R1) {
    mcp.digitalWrite(rightMotor0, HIGH);
    mcp.digitalWrite(rightMotor1, LOW);
  } else if (pkt.buttons & BTN_R2) {
    mcp.digitalWrite(rightMotor0, LOW);
    mcp.digitalWrite(rightMotor1, HIGH);
  } else {
    mcp.digitalWrite(rightMotor0, LOW);
    mcp.digitalWrite(rightMotor1, LOW);
  }

  if (pkt.buttons & BTN_L1) {
    mcp.digitalWrite(leftMotor0, HIGH);
    mcp.digitalWrite(leftMotor1, LOW);
  } else if (pkt.buttons & BTN_L2) {
    mcp.digitalWrite(leftMotor0, LOW);
    mcp.digitalWrite(leftMotor1, HIGH);
  } else {
    mcp.digitalWrite(leftMotor0, LOW);
    mcp.digitalWrite(leftMotor1, LOW);
  }

  // Servos (face buttons)
  if (pkt.buttons & BTN_A) {
    moveClawServoDown = true;
  } else if (pkt.buttons & BTN_Y) {
    moveClawServoUp = true;
  } else {
    moveClawServoDown = false;
    moveClawServoUp = false;
  }

  if (pkt.buttons & BTN_X) {
    moveAuxServoDown = true;
  } else if (pkt.buttons & BTN_B) {
    moveAuxServoUp = true;
  } else {
    moveAuxServoDown = false;
    moveAuxServoUp = false;
  }

  // Servo smooth movement
  if (moveClawServoUp) {
    if (servoDelay == 2) {
      if (clawServoValue >= 10 && clawServoValue < 170) {
        clawServoValue += 2;
        clawServo.write(clawServoValue);
      }
      servoDelay = 0;
    }
    servoDelay++;
  }
  if (moveClawServoDown) {
    if (servoDelay == 2) {
      if (clawServoValue <= 170 && clawServoValue > 10) {
        clawServoValue -= 2;
        clawServo.write(clawServoValue);
      }
      servoDelay = 0;
    }
    servoDelay++;
  }
  if (moveAuxServoUp) {
    if (servoDelay == 2) {
      if (auxServoValue >= 10 && auxServoValue < 170) {
        auxServoValue += 2;
        auxServo.write(auxServoValue);
      }
      servoDelay = 0;
    }
    servoDelay++;
  }
  if (moveAuxServoDown) {
    if (servoDelay == 2) {
      if (auxServoValue <= 170 && auxServoValue > 10) {
        auxServoValue -= 2;
        auxServo.write(auxServoValue);
      }
      servoDelay = 0;
    }
    servoDelay++;
  }
}

// ─── Safety: stop all motors when hub stops sending ──────────────
void allStop() {
  for (int i = 0; i <= 15; i++) {
    mcp.digitalWrite(i, LOW);
  }
  moveClawServoUp = false;
  moveClawServoDown = false;
  moveAuxServoUp = false;
  moveAuxServoDown = false;
}

// ─── Setup ───────────────────────────────────────────────────────
void setup() {
  Serial.begin(115200);
  Serial.printf("\n=== Mini Excavator (ESP-NOW) === Vehicle ID: %d\n", MY_VEHICLE_ID);

  // MCP23X17 port expander
  mcp.begin_I2C();
  for (int i = 0; i <= 15; i++) {
    mcp.pinMode(i, OUTPUT);
  }

  // Servos
  pinMode(clawServoPin, OUTPUT);
  pinMode(auxServoPin, OUTPUT);
  clawServo.attach(clawServoPin);
  auxServo.attach(auxServoPin);
  clawServo.write(clawServoValue);
  auxServo.write(auxServoValue);

  // Lights
  pinMode(cabLights, OUTPUT);
  pinMode(auxLights, OUTPUT);

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

  // Safety timeout: if no packet for 500ms, stop everything.
  // This prevents a vehicle from running away if the hub disconnects.
  if (millis() - lastPacketTime > 500 && lastPacketTime > 0) {
    allStop();
    lastPacketTime = 0;  // only fire once
    Serial.println("No signal — motors stopped");
  }

  vTaskDelay(1);
}
