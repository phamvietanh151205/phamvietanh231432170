#include <Arduino.h>
#include <Wire.h>
#include <WiFi.h>
#include <PubSubClient.h>
#include <Adafruit_BMP085.h>
#include <math.h>
// 1. KHAI BAO CHAN GPIO
#define SDA_PIN    21
#define SCL_PIN    22
#define LED_PIN    25
#define BUZZER_PIN 26
// 2. WIFI
const char* WIFI_SSID = "Wokwi-GUEST";
const char* WIFI_PASSWORD = "";
// 3. MQTT BROKER
const char* MQTT_SERVER = "broker.emqx.io";
const int MQTT_PORT = 1883;
const char* TOPIC_SENSOR = "runner/sensor";
const char* TOPIC_LED    = "runner/led";
const char* TOPIC_BUZZER = "runner/buzzer";
const char* TOPIC_MODE   = "runner/mode";
const char* TOPIC_STATUS = "runner/status";
// 4. DOI TUONG
Adafruit_BMP085 bmp;
WiFiClient espClient;
PubSubClient mqtt(espClient);
// 5. CAU TRUC DU LIEU
struct SensorData { float temperature, pressure, altitude; };
struct AlertStatus { bool active, temperatureAlert, altitudeAlert; };
// 6. KHAI BAO QUEUE
QueueHandle_t dataQueue, dataAlert, statusQueue;
// 7. NGUONG CANH BAO
const float TEMP_MAX = 35.0;
const float TEMP_MIN = 15.0;
const float ALT_CHANGE = 10.0;
// 8. BIEN DIEU KHIEN
enum ControlMode { AUTO_MODE, MANUAL_MODE };
ControlMode currentMode = AUTO_MODE;
bool ledManual = false, buzzerManual = false;
portMUX_TYPE controlMux = portMUX_INITIALIZER_UNLOCKED;
// 9. HAM MQTT CALLBACK
void mqttCallback(char* topic, byte* payload, unsigned int length) {
  String msg;
  for (unsigned int i = 0; i < length; i++) msg += (char)payload[i];
  msg.trim();
  msg.toUpperCase();
  portENTER_CRITICAL(&controlMux);
  if (!strcmp(topic, TOPIC_LED)) ledManual = (msg == "ON");
  else if (!strcmp(topic, TOPIC_BUZZER)) buzzerManual = (msg == "ON");
  else if (!strcmp(topic, TOPIC_MODE)) {
    if (msg == "AUTO") currentMode = AUTO_MODE;
    else if (msg == "MANUAL") currentMode = MANUAL_MODE;
  }
  portEXIT_CRITICAL(&controlMux);
  Serial.printf("MQTT Command: %s = %s\n", topic, msg.c_str());
}
// 10. HAM KET NOI MQTT
void reconnectMQTT() {
  while (!mqtt.connected()) {
    Serial.println("Dang ket noi MQTT...");
    String clientID = "RunnerESP32-" + String((uint32_t)ESP.getEfuseMac(), HEX);
    if (mqtt.connect(clientID.c_str())) {
      Serial.println("MQTT connected!");
      mqtt.subscribe(TOPIC_LED);
      mqtt.subscribe(TOPIC_BUZZER);
      mqtt.subscribe(TOPIC_MODE);
    } else {
      Serial.printf("MQTT failed, state = %d\n", mqtt.state());
      vTaskDelay(pdMS_TO_TICKS(2000));
    }
  }
}
// 11. TASK DOC DU LIEU CAM BIEN - CORE 1
void TaskRead(void *pv) {
  SensorData d;
  while (true) {
    d.temperature = bmp.readTemperature();
    d.pressure = bmp.readPressure() / 100.0;
    d.altitude = bmp.readAltitude(1013.25);
    Serial.printf("T: %.2f C | P: %.2f hPa | Alt: %.2f m\n", d.temperature, d.pressure, d.altitude);
    if (xQueueSend(dataQueue, &d, pdMS_TO_TICKS(100)) != pdTRUE) Serial.println("dataQueue is full!");
    xQueueOverwrite(dataAlert, &d);
    vTaskDelay(pdMS_TO_TICKS(2000));
  }
}
// 12. TASK TRUYEN DU LIEU MQTT - CORE 0
void TaskTransmit(void *pv) {
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  while (WiFi.status() != WL_CONNECTED) vTaskDelay(pdMS_TO_TICKS(500));
  Serial.println("WiFi connected! IP: " + WiFi.localIP().toString());
  mqtt.setServer(MQTT_SERVER, MQTT_PORT);
  mqtt.setCallback(mqttCallback);
  mqtt.setKeepAlive(30);
  while (true) {
    if (WiFi.status() != WL_CONNECTED) {
      Serial.println("WiFi disconnected!");
      WiFi.reconnect();
      vTaskDelay(pdMS_TO_TICKS(1000));
      continue;
    }
    if (!mqtt.connected()) reconnectMQTT();
    mqtt.loop();
    SensorData d;
    AlertStatus s;
    char json[200];
    if (xQueueReceive(dataQueue, &d, pdMS_TO_TICKS(50)) == pdTRUE) {
      snprintf(json, sizeof(json),
               "{\"temperature\":%.2f,\"pressure\":%.2f,\"altitude\":%.2f}",
               d.temperature, d.pressure, d.altitude);
      if (mqtt.publish(TOPIC_SENSOR, json)) Serial.printf("MQTT SENT: %s\n", json);
      else Serial.println("MQTT publish failed!");
    }
    if (xQueueReceive(statusQueue, &s, 0) == pdTRUE) {
      snprintf(json, sizeof(json),
               "{\"alert\":%s,\"temperatureAlert\":%s,\"altitudeAlert\":%s}",
               s.active ? "true" : "false",
               s.temperatureAlert ? "true" : "false",
               s.altitudeAlert ? "true" : "false");
      mqtt.publish(TOPIC_STATUS, json);
      Serial.println("Alert status sent!");
    }
    vTaskDelay(pdMS_TO_TICKS(10));
  }
}
// 13. TASK CANH BAO - CORE 1
void TaskAlert(void *pv) {
  SensorData d;
  float prevAlt = 0;
  bool first = true;
  while (true) {
    if (xQueueReceive(dataAlert, &d, portMAX_DELAY) != pdTRUE) continue;
    bool tempAlert = d.temperature > TEMP_MAX || d.temperature < TEMP_MIN;
    bool altAlert = !first && fabsf(d.altitude - prevAlt) > ALT_CHANGE;
    prevAlt = d.altitude;
    first = false;
    bool active = tempAlert || altAlert;
    portENTER_CRITICAL(&controlMux);
    ControlMode mode = currentMode;
    bool led = ledManual, buz = buzzerManual;
    portEXIT_CRITICAL(&controlMux);
    if (mode == AUTO_MODE) led = buz = active;
    digitalWrite(LED_PIN, led);
    if (buz) tone(BUZZER_PIN, 2000);
    else noTone(BUZZER_PIN);
    Serial.printf("ALERT | Temp: %s | Alt: %s | Mode: %s | Status: %s\n",
                  tempAlert ? "YES" : "NO", altAlert ? "YES" : "NO",
                  mode == AUTO_MODE ? "AUTO" : "MANUAL",
                  active ? "WARNING" : "NORMAL");
    AlertStatus s = { active, tempAlert, altAlert };
    xQueueOverwrite(statusQueue, &s);
  }
}
// 14. SETUP
void setup() {
  Serial.begin(115200);
  pinMode(LED_PIN, OUTPUT);
  pinMode(BUZZER_PIN, OUTPUT);
  digitalWrite(LED_PIN, LOW);
  noTone(BUZZER_PIN);
  Wire.begin(SDA_PIN, SCL_PIN);
  if (!bmp.begin()) {
    Serial.println("BMP180 NOT FOUND!");
    while (true) delay(1000);
  }
  Serial.println("BMP180 READY!");
  dataQueue = xQueueCreate(5, sizeof(SensorData));
  dataAlert = xQueueCreate(1, sizeof(SensorData));
  statusQueue = xQueueCreate(1, sizeof(AlertStatus));
  if (!dataQueue || !dataAlert || !statusQueue) {
    Serial.println("QUEUE CREATION FAILED!");
    while (true) delay(1000);
  }
  xTaskCreatePinnedToCore(TaskRead, "TaskRead", 4096, NULL, 2, NULL, 1);
  xTaskCreatePinnedToCore(TaskTransmit, "TaskTransmit", 8192, NULL, 1, NULL, 0);
  xTaskCreatePinnedToCore(TaskAlert, "TaskAlert", 4096, NULL, 2, NULL, 1);
  Serial.println("ALL TASKS STARTED!");
}
// 15. LOOP
void loop() {
  vTaskDelay(pdMS_TO_TICKS(1000));
}