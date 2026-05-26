#include <Arduino.h>

#include "config.h"

#ifndef DCC_PIN
  #error DCC_PIN not defined
#endif

#ifndef DCC_PULLUP
  #define DCC_PULLUP 0
#endif

#include <NmraDcc.h>
NmraDcc dcc;

#if defined(ARDUINO_ARCH_ESP32) && !defined(USE_ETHERNET)
  #include <WiFi.h>
  #include "wificredentials.h"

  #if !defined(WIFI_SSID) || !defined(WIFI_PASS)
    #error WiFi credentials not defined
  #endif

WiFiClient netClient;
#else
  #include <Ethernet.h>
EthernetClient netClient;
#endif

#include <PubSubClient.h>

#ifndef MQTT_SERVER
  #error MQTT_SERVER not defined
#endif

#ifndef MQTT_PORT
  #define MQTT_PORT 1883
#endif

#if !defined(MQTT_USER) || !defined(MQTT_PASS)
  #warning MQTT credentials not provided
#endif

PubSubClient client(MQTT_SERVER, MQTT_PORT, netClient);

#ifndef DCC_TOPIC
  #define DCC_TOPIC "dcc"
#endif

#ifdef ARDUINO_ARCH_ESP32
  #define Sprintf Serial.printf
#else
  #define Sprintf Serialprintf
template <typename T>
void Serialprintf(T cur) {
  Serial.print(" ");
  Serial.print(cur);
  Serial.println();
}
template <typename T, typename... Args>
void Serialprintf(T next, Args... args) {
  Serial.print(" ");
  Serial.print(next);
  Serialprintf(args...);
}

#endif

void setup() {
  Serial.begin(115200);
  Serial.println("DCC MQTT Gateway");

#if defined(ARDUINO_ARCH_ESP32) && !defined(USE_ETHERNET)
  Serial.printf("Connecting to WiFi network %s\n", WIFI_SSID);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  if (WiFi.waitForConnectResult() != WL_CONNECTED)
    ESP.restart();
  Serial.println("WiFi connected");
  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());
#else
  static byte mac[] = {0xDE, 0xAD, 0xBE, 0xEF, 0xFE, 0xED};
  #ifdef ETHERNET_CS_PIN
  Ethernet.init(ETHERNET_CS_PIN);
  #endif
  Ethernet.begin(mac);
#endif

  Sprintf("Connecting to MQTT server \"%s\"\n", MQTT_SERVER);
  while (!client.connected())
#if defined(MQTT_USER) && defined(MQTT_PASS)
    client.connect("DCC-MQTT-Relay", MQTT_USER, MQTT_PASS);
#else
    client.connect("DCC-MQTT-Relay");
#endif
  Serial.println("Connected to MQTT server");

  dcc.pin(DCC_PIN, DCC_PULLUP);
  dcc.init(MAN_ID_DIY, 1, FLAGS_DCC_ACCESSORY_DECODER, 0);
}

void loop() {
  client.loop();
  dcc.process();
}

String getTopicAddressString(uint16_t address, DCC_ADDR_TYPE addrType) {
  String topic = String(address);
  switch (addrType) {
    case DCC_ADDR_SHORT:
      topic += 's';
      break;
    case DCC_ADDR_LONG:
      topic += 'l';
      break;
    default:
      break;
  }
  return topic;
}

void notifyDccReset(uint8_t hardReset) {
  client.publish(DCC_TOPIC "/reset", hardReset ? "1" : "0");
}

#ifdef NOTIFY_DCC_IDLE
void notifyDccIdle() {
  client.publish(DCC_TOPIC "/idle", "");
}
#endif

void notifyDccSpeed(uint16_t Addr,
                    DCC_ADDR_TYPE AddrType,
                    uint8_t Speed,
                    DCC_DIRECTION Dir,
                    DCC_SPEED_STEPS SpeedSteps) {
  Speed = max(0, Speed - 1);
  float nspeed = (float)Speed / (SpeedSteps - 1);
  if (Dir == DCC_DIR_REV)
    nspeed = -nspeed;
  String topic = DCC_TOPIC "/speed/";
  topic += Addr;
  client.publish(topic.c_str(), String(nspeed).c_str());
}

uint8_t getFunctionGroupOffset(FN_GROUP functionGroup) {
  switch (functionGroup) {
    default:
#ifdef NMRA_DCC_ENABLE_14_SPEED_STEP_MODE
    case FN_0:
#endif
    case FN_0_4:
      return 0;
    case FN_5_8:
      return 5;
    case FN_9_12:
      return 9;
    case FN_13_20:
      return 13;
    case FN_21_28:
      return 21;
  }
}

template <typename Callback>
void forEachFunctionBit(
    FN_GROUP functionGroup,
    uint8_t functionState,
    // void (*callback)(uint8_t functionNumber,bool functionState)
    Callback callback) {
  uint8_t bitCount = 0;
  switch (functionGroup) {
#ifdef NMRA_DCC_ENABLE_14_SPEED_STEP_MODE
    case FN_0:
      bitCount = 1;
      break;
#endif
    default:
    case FN_0_4:
      bitCount = 5;
      break;
    case FN_5_8:
    case FN_9_12:
      bitCount = 4;
      break;
    case FN_13_20:
    case FN_21_28:
      bitCount = 8;
      break;
  }
  const uint8_t functionGroupOffset = getFunctionGroupOffset(functionGroup);
  for (uint8_t i = 0; i < bitCount; i++) {
    if (functionGroupOffset == 0 && i == 0) {
      callback(0, functionState & FN_BIT_00);
      continue;
    }
    uint8_t bitshift = i;
    if (functionGroupOffset == 0)
      bitshift -= 1;
    callback(functionGroupOffset + i, functionState & (1 << bitshift));
  }
}

void notifyDccFunc(uint16_t Addr,
                   DCC_ADDR_TYPE AddrType,
                   FN_GROUP FuncGrp,
                   uint8_t FuncState) {
  auto functionCallback = [&](uint8_t functionNumber, bool functionState) {
    String topic = DCC_TOPIC "/func/";
    topic += Addr;
    topic += '/';
    topic += functionNumber;
    client.publish(topic.c_str(), functionState ? "1" : "0");
  };
  forEachFunctionBit(FuncGrp, FuncState, functionCallback);
}

void notifyDccAccTurnoutBoard(uint16_t BoardAddr,
                              uint8_t OutputPair,
                              uint8_t Direction,
                              uint8_t OutputPower) {
  Sprintf("dcc turnoutboard addr=%d pair=%d dir=%d pow=%d\n", BoardAddr,
          OutputPair, Direction, OutputPower);
  String topic = DCC_TOPIC "/turnoutBoard/";
  topic += BoardAddr;
  topic += '/';
  topic += OutputPair;
  topic += '/';
  topic += Direction;
  client.publish(topic.c_str(), OutputPower ? "1" : "0");

  if (OutputPower == 1) {
    String topic = DCC_TOPIC "/turnoutPair/";
    topic += BoardAddr;
    topic += '/';
    topic += OutputPair;
    client.publish(topic.c_str(), Direction ? "1" : "0");
  }
}
void notifyDccAccTurnoutOutput(uint16_t Addr,
                               uint8_t Direction,
                               uint8_t OutputPower) {
  Sprintf("dcc turnoutoutput addr=%d dir=%d pow=%d\n", Addr, Direction,
          OutputPower);
  String topic = DCC_TOPIC "/turnoutOutput/";
  topic += Addr;
  topic += '/';
  topic += Direction;
  client.publish(topic.c_str(), OutputPower ? "1" : "0");

  if (OutputPower == 1) {
    String topic = DCC_TOPIC "/turnoutOutputConstant/";
    topic += Addr;
    client.publish(topic.c_str(), Direction ? "1" : "0");
  }
}

void notifyServiceMode(bool inServiceMode) {
  client.publish(DCC_TOPIC "/serviceMode", inServiceMode ? "1" : "0");
}
