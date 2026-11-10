// make sure to upload with ESP32 Dev Module selected as the board under tools>Board>ESP32 Arduino

#include <Arduino.h>

#include <ESP32Servo.h> // by Kevin Harrington
#include <ESPAsyncWebServer.h> // by mathieucarbou

#if defined(ESP32)
#include <AsyncTCP.h> // by dvarrel
#include <WiFi.h>
#elif defined(ESP8266)
#include <ESPAsyncTCP.h> // by dvarrel
#endif

// defines

#define steeringServoPin 23
#define mastTiltServoPin 22
#define cabLights 32
#define auxLights 33

#define mastMotor0 25  // Used for controlling auxiliary attachment movement
#define mastMotor1 26  // Used for controlling auxiliary attachment movement
#define auxAttach0 18  // Used for controlling auxiliary attachment movement
#define auxAttach1 17  // Used for controlling auxiliary attachment movement

#define leftMotor0 21   // Used for controlling the left motor movement
#define leftMotor1 19   // Used for controlling the left motor movement
#define rightMotor0 33  // Used for controlling the right motor movementc:\Users\JohnC\Desktop\SOLIDWORKS Connected.lnk
#define rightMotor1 32  // Used for controlling the right motor movement

// global constants

extern const char* htmlHomePage PROGMEM;
const char* ssid = "MiniFork";
 
// global variables

Servo steeringServo;
Servo mastTiltServo;

int servoDelay = 0;
float steeringServoValue = 86;
float steeringAdjustment = 1;
int throttleValue = 0;
int steeringTrim = 0;
int mastTiltServoValue = 90;
int mastTiltValue = 90;
int lightSwitchTime = 0;
bool horizontalScreen; // when screen orientation is locked vertically this rotates the D-Pad controls so that forward would now be left.
bool lightsOn = false;

AsyncWebServer server(80);
AsyncWebSocket wsCarInput("/CarInput");

void steeringControl(int steeringValue)
{
  steeringServoValue = steeringValue;
  steeringServo.write(steeringServoValue - steeringTrim);
  if (steeringServoValue > 100) {
    steeringAdjustment = ((200 - steeringServoValue) / 100);
  } else if (steeringServoValue < 80) {
    steeringAdjustment = ((200 - (90 + (90 - steeringServoValue))) / 100);
  }
  processThrottle(throttleValue);
}

void mastTiltControl(int mastTiltServoValue)
{
    mastTiltServo.write(mastTiltServoValue);
}

void mastControl(int mastValue){
if (mastValue == 5) {
    digitalWrite(mastMotor0, HIGH);
    digitalWrite(mastMotor1, LOW);
  } else if (mastValue == 6) {
    digitalWrite(mastMotor0, LOW);
    digitalWrite(mastMotor1, HIGH);
  } else {
    digitalWrite(mastMotor0, LOW);
    digitalWrite(mastMotor1, LOW);
  }
}
void processThrottle(int throttle) {
  throttleValue = -throttle;
  if (throttleValue > 15 || throttleValue < -15) {
    if(steeringServoValue > 100) {
      moveMotor(leftMotor0, leftMotor1, throttleValue * steeringAdjustment);
      moveMotor(rightMotor0, rightMotor1, -throttleValue);
    } else if (steeringServoValue < 80) {
      moveMotor(leftMotor0, leftMotor1, throttleValue);
      moveMotor(rightMotor0, rightMotor1, -throttleValue * steeringAdjustment);
    } else {
      moveMotor(leftMotor0, leftMotor1, throttleValue);
      moveMotor(rightMotor0, rightMotor1, -throttleValue);
    }
  } else {
    moveMotor(leftMotor0, leftMotor1, 0);
    moveMotor(rightMotor0, rightMotor1, 0);
  }
}
void moveMotor(int motorPin1, int motorPin0, int velocity) {
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
void lightControl()
{
  if ((millis() - lightSwitchTime) > 200) {
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
void mastTilt(int mastTilt)
{
   if (mastTilt == 1) {
    if (servoDelay == 2) {
      if (mastTiltValue >= 10 && mastTiltValue < 165) {
        mastTiltValue = mastTiltValue + 2;
        mastTiltServo.write(mastTiltValue);
      }
      servoDelay = 0;
    }
    servoDelay++;
  } else {
    if (servoDelay == 2) {
      if (mastTiltValue <= 170 && mastTiltValue > 15) {
        mastTiltValue = mastTiltValue - 2;
        mastTiltServo.write(mastTiltValue);
      }
      servoDelay = 0;
    }
    servoDelay++;
  }
}

void handleRoot(AsyncWebServerRequest *request)
{
  request->send_P(200, "text/html", htmlHomePage);
}

void handleNotFound(AsyncWebServerRequest *request)
{
  request->send(404, "text/plain", "File Not Found");
}

void onCarInputWebSocketEvent(AsyncWebSocket *server,
                              AsyncWebSocketClient *client,
                              AwsEventType type,
                              void *arg,
                              uint8_t *data,
                              size_t len)
{
  switch (type)
  {
    case WS_EVT_CONNECT:
      //Serial.printf("WebSocket client #%u connected from %s\n", client->id(), client->remoteIP().toString().c_str());
      break;
    case WS_EVT_DISCONNECT:
      //Serial.printf("WebSocket client #%u disconnected\n", client->id());
      break;
    case WS_EVT_DATA:
      AwsFrameInfo *info;
      info = (AwsFrameInfo*)arg;
      if (info->final && info->index == 0 && info->len == len && info->opcode == WS_TEXT)
      {
        char buf[64];
        size_t copyLen = len < sizeof(buf) - 1 ? len : sizeof(buf) - 1;
        memcpy(buf, data, copyLen);
        buf[copyLen] = '\0';
        char* comma = strchr(buf, ',');
        if (!comma) break;
        *comma = '\0';
        const char* key = buf;
        int valueInt = atoi(comma + 1);
        Serial.printf("Key [%s] Value[%d]\n", key, valueInt);
        if (strcmp(key, "steering") == 0)
        {
          steeringControl(valueInt);
        }
        else if (strcmp(key, "throttle") == 0)
        {
          processThrottle(valueInt);
        }
        else if (strcmp(key, "mast") == 0)
        {
          mastControl(valueInt);
        }
        else if (strcmp(key, "light") == 0)
        {
          lightControl();
        }
        else if (strcmp(key, "mTilt") == 0)
        {
          mastTilt(valueInt);
        }
      }
      break;
    case WS_EVT_PONG:
    case WS_EVT_ERROR:
      break;
    default:
      break;
  }
}

void setUpPinModes()
{
  pinMode(mastMotor0, OUTPUT);
  pinMode(mastMotor1, OUTPUT);
  pinMode(auxAttach0, OUTPUT);
  pinMode(auxAttach1, OUTPUT);
  pinMode(leftMotor0, OUTPUT);
  pinMode(leftMotor1, OUTPUT);
  pinMode(rightMotor0, OUTPUT);
  pinMode(rightMotor1, OUTPUT);

  digitalWrite(mastMotor0, LOW);
  digitalWrite(mastMotor1, LOW);
  digitalWrite(auxAttach0, LOW);
  digitalWrite(auxAttach1, LOW);

  steeringServo.attach(steeringServoPin);
  mastTiltServo.attach(mastTiltServoPin);
  steeringControl(steeringServoValue);
  mastTiltControl(mastTiltServoValue);
}


void setup(void)
{
  setUpPinModes();
  Serial.begin(115200);

  WiFi.softAP(ssid );
  IPAddress IP = WiFi.softAPIP();
  Serial.print("AP IP address: ");
  Serial.println(IP);

  server.on("/", HTTP_GET, handleRoot);
  server.onNotFound(handleNotFound);

  wsCarInput.onEvent(onCarInputWebSocketEvent);
  server.addHandler(&wsCarInput);

  server.begin();
  //Serial.println("HTTP server started");
}

void loop()
{
  wsCarInput.cleanupClients();
}
