/*
  GridWise DC - ESP32 rack node
  Sensor: DHT22 on GPIO 4
  LEDs: green 25, yellow 26, red 27
  Fan driver/MOSFET: GPIO 14 (do NOT drive a fan directly from an ESP32 pin)
  Active buzzer: GPIO 32
  MQTT telemetry: gridwise/rack1/telemetry
  MQTT commands:  gridwise/rack1/command
*/
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include <DHT.h>

// Fill in your own credentials before uploading.
const char* WIFI_SSID = "YOUR_WIFI_NAME";
const char* WIFI_PASSWORD = "YOUR_WIFI_PASSWORD";
const char* MQTT_HOST = "your-cluster.s1.eu.hivemq.cloud";
const int MQTT_PORT = 8883;
const char* MQTT_USER = "YOUR_HIVEMQ_USERNAME";
const char* MQTT_PASS = "YOUR_HIVEMQ_PASSWORD";
const char* CLIENT_ID = "gridwise-esp32-rack1";

const char* TELEMETRY_TOPIC = "gridwise/rack1/telemetry";
const char* COMMAND_TOPIC = "gridwise/rack1/command";
#define DHTPIN 4
#define DHTTYPE DHT22
#define LED_GREEN 25
#define LED_YELLOW 26
#define LED_RED 27
#define FAN_PIN 14
#define BUZZER_PIN 32

DHT dht(DHTPIN, DHTTYPE);
WiFiClientSecure secureClient;
PubSubClient mqtt(secureClient);
String fanState = "OFF", ledState = "GREEN";
bool buzzerState = false;
unsigned long lastPublish = 0, lastWifiAttempt = 0;

void setLeds(const String& state) {
  digitalWrite(LED_GREEN, state == "GREEN" ? HIGH : LOW);
  digitalWrite(LED_YELLOW, state == "YELLOW" ? HIGH : LOW);
  digitalWrite(LED_RED, state == "RED" ? HIGH : LOW);
}
void applyFan(String state) {
  state.toUpperCase();
  if (state != "OFF" && state != "LOW" && state != "HIGH") return;
  fanState = state;
  // Arduino-ESP32 core 2.x compatible PWM approximation; use a MOSFET/driver.
  analogWrite(FAN_PIN, state == "HIGH" ? 255 : state == "LOW" ? 120 : 0);
}
void applyBuzzer(bool on) {
  buzzerState = on;
  digitalWrite(BUZZER_PIN, on ? HIGH : LOW); // active buzzer module
}
void mqttCallback(char* topic, byte* payload, unsigned int length) {
  if (String(topic) != COMMAND_TOPIC) return;
  StaticJsonDocument<256> doc;
  if (deserializeJson(doc, payload, length)) return;
  if (doc.containsKey("fan")) applyFan(doc["fan"].as<String>());
  if (doc.containsKey("led")) {
    ledState = doc["led"].as<String>();
    if (ledState != "GREEN" && ledState != "YELLOW" && ledState != "RED") ledState = "GREEN";
    setLeds(ledState);
  }
  if (doc.containsKey("buzzer")) applyBuzzer(doc["buzzer"].as<bool>());
}
void connectWiFi() {
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  Serial.print("Connecting to Wi-Fi");
  unsigned long start = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - start < 25000) {
    delay(400); Serial.print(".");
  }
  Serial.println(WiFi.status() == WL_CONNECTED ? "\nWi-Fi connected" : "\nWi-Fi not connected yet");
}
void reconnectMqtt() {
  if (WiFi.status() != WL_CONNECTED) return;
  if (mqtt.connect(CLIENT_ID, MQTT_USER, MQTT_PASS)) {
    Serial.println("MQTT connected");
    mqtt.subscribe(COMMAND_TOPIC);
  } else {
    Serial.printf("MQTT connect failed, state=%d\n", mqtt.state());
  }
}
void setup() {
  Serial.begin(115200);
  pinMode(LED_GREEN, OUTPUT); pinMode(LED_YELLOW, OUTPUT); pinMode(LED_RED, OUTPUT);
  pinMode(FAN_PIN, OUTPUT); pinMode(BUZZER_PIN, OUTPUT);
  setLeds("GREEN"); applyFan("OFF"); applyBuzzer(false); dht.begin();
  connectWiFi();
  // Demo convenience only. For deployment, validate HiveMQ's CA certificate instead.
  secureClient.setInsecure();
  mqtt.setServer(MQTT_HOST, MQTT_PORT);
  mqtt.setCallback(mqttCallback);
  mqtt.setBufferSize(512);
}
void loop() {
  if (WiFi.status() != WL_CONNECTED && millis() - lastWifiAttempt > 10000) {
    lastWifiAttempt = millis(); connectWiFi();
  }
  if (WiFi.status() == WL_CONNECTED && !mqtt.connected()) reconnectMqtt();
  mqtt.loop();

  if (millis() - lastPublish >= 2000) {
    lastPublish = millis();
    float t = dht.readTemperature();
    if (isnan(t)) { Serial.println("DHT read failed; telemetry temperature omitted"); }
    else {
      // Local thermal safety has priority over dashboard commands.
      if (t >= 50.0) { ledState = "RED"; applyFan("HIGH"); applyBuzzer(true); }
      else if (t >= 40.0) { ledState = "RED"; applyFan("HIGH"); applyBuzzer(false); }
      else if (t >= 32.0) { ledState = "YELLOW"; if (fanState == "OFF") applyFan("LOW"); applyBuzzer(false); }
      else { ledState = "GREEN"; applyFan("OFF"); applyBuzzer(false); }
      setLeds(ledState);
      StaticJsonDocument<256> doc;
      doc["temperature"] = t; doc["fan"] = fanState; doc["led"] = ledState;
      doc["buzzer"] = buzzerState; doc["rack"] = "rack1";
      char payload[256]; size_t n = serializeJson(doc, payload, sizeof(payload));
      if (mqtt.connected()) mqtt.publish(TELEMETRY_TOPIC, payload, n);
      Serial.println(payload);
    }
  }
}
