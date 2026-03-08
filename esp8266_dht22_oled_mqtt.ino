#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <PubSubClient.h>
#include <Wire.h>
#include <U8g2lib.h>
#include <DHT.h>
#include <time.h>

// ===== User configuration =====
static const char *WIFI_SSID = "yournetwork";
static const char *WIFI_PASS = "yournetworkpassword"; // TODO: set your WiFi password

// ===== MQTT configuration =====
static const char *MQTT_HOST = "mqtt-broker.local"; // TODO: set your MQTT broker address
static const uint16_t MQTT_PORT = 1883;
static const char *MQTT_USER = "usernameMQTTAccess"; // optional
static const char *MQTT_PASS = "passwordMQTTAccess"; // optional
static const uint16_t MQTT_PACKET_BUFFER_SIZE = 1024;

static const char *MQTT_TOPIC_ROOT = "home/esp8266/dht22";
static const char *HA_DISCOVERY_PREFIX = "homeassistant";
static const char *DEVICE_NAME = "ESP8266 DHT22";
static const int32_t CLOCK_GMT_OFFSET_SEC = 7 * 3600; // UTC+7 (WIB)
static const char *NTP_SERVER_1 = "pool.ntp.org";
static const char *NTP_SERVER_2 = "time.google.com";

// ===== Hardware pins =====
static const uint8_t PIN_I2C_SDA = 4;  // GPIO4
static const uint8_t PIN_I2C_SCL = 5;  // GPIO5
static const uint8_t PIN_DHT = 15;     // GPIO15
static const uint8_t PIN_DHT_POWER = 13; // GPIO13 must be enabled for DHT22

// ===== Timing =====
static const uint32_t SENSOR_INTERVAL_MS = 30000;
static const uint32_t WIFI_RETRY_MS = 10000;
static const uint32_t MQTT_RETRY_MS = 5000;
static const uint32_t SENSOR_PAGE_INTERVAL_MS = 60000;
static const uint32_t STATUS_PAGE_INTERVAL_MS = 5000;
static const uint32_t ANIM_INTERVAL_MS = 400;
static const uint32_t HISTORY_INTERVAL_MS = 60000;
static const uint8_t HISTORY_POINTS = 60;

// ===== Display =====
U8G2_SH1106_128X64_NONAME_F_HW_I2C display(U8G2_R0, /* reset=*/ U8X8_PIN_NONE);

// ===== Sensor =====
DHT dht(PIN_DHT, DHT22);

// ===== Networking =====
WiFiClient wifiClient;
PubSubClient mqttClient(wifiClient);

static uint32_t lastSensorMs = 0;
static uint32_t lastWifiAttemptMs = 0;
static uint32_t lastMqttAttemptMs = 0;
static uint32_t lastPublishMs = 0;
static uint32_t lastPageMs = 0;
static uint32_t lastHistoryMs = 0;

static uint8_t pageIndex = 0;
static bool discoveryPublished = false;

static float lastTempC = NAN;
static float lastHumidity = NAN;
static float tempHistory[HISTORY_POINTS];
static uint8_t historyCount = 0;
static uint8_t historyIndex = 0;

static char deviceId[20] = {0};
static char topicBase[80] = {0};
static char topicTemperature[96] = {0};
static char topicHumidity[96] = {0};
static char topicStatus[96] = {0};
static char topicUptime[96] = {0};

// 8x8 icons (monochrome)
static const uint8_t ICON_WIFI[] = {
  0x18, 0x3C, 0x7E, 0xDB, 0x18, 0x24, 0x00, 0x18
};

static const uint8_t ICON_MQTT[] = {
  0x00, 0x3C, 0x42, 0x9D, 0xA1, 0x42, 0x3C, 0x00
};

void drawWifiIconWindowsStyle(int16_t x, int16_t y) {
  // Top arc
  display.drawPixel(x + 2, y + 1);
  display.drawPixel(x + 3, y + 0);
  display.drawPixel(x + 4, y + 0);
  display.drawPixel(x + 5, y + 1);

  // Middle arc
  display.drawPixel(x + 1, y + 3);
  display.drawPixel(x + 2, y + 2);
  display.drawPixel(x + 3, y + 2);
  display.drawPixel(x + 4, y + 2);
  display.drawPixel(x + 5, y + 2);
  display.drawPixel(x + 6, y + 3);

  // Bottom arc
  display.drawPixel(x + 0, y + 5);
  display.drawPixel(x + 1, y + 4);
  display.drawPixel(x + 2, y + 4);
  display.drawPixel(x + 3, y + 4);
  display.drawPixel(x + 4, y + 4);
  display.drawPixel(x + 5, y + 4);
  display.drawPixel(x + 6, y + 4);
  display.drawPixel(x + 7, y + 5);

  // Dot
  display.drawPixel(x + 3, y + 6);
  display.drawPixel(x + 4, y + 6);
  display.drawPixel(x + 3, y + 7);
  display.drawPixel(x + 4, y + 7);
}

void drawStatusIcons(bool wifiOk, bool mqttOk) {
  const int16_t xWifi = 108;
  const int16_t xMqtt = 118;
  const int16_t y = 0;

  drawWifiIconWindowsStyle(xWifi, y);
  if (!wifiOk) {
    display.drawLine(xWifi, y + 7, xWifi + 7, y);
  }

  display.drawXBM(xMqtt, y, 8, 8, ICON_MQTT);
  if (!mqttOk) {
    display.drawLine(xMqtt, y + 7, xMqtt + 7, y);
  }
}

void drawDots(int16_t x, int16_t y, uint8_t count) {
  display.setCursor(x, y);
  for (uint8_t i = 0; i < count; ++i) {
    display.print('.');
  }
}

void drawTempGraph(int16_t x, int16_t y, int16_t w, int16_t h) {
  display.drawFrame(x, y, w, h);

  if (historyCount < 2) {
    return;
  }

  float minV = 1000.0f;
  float maxV = -1000.0f;
  for (uint8_t i = 0; i < historyCount; ++i) {
    const uint8_t idx = (historyIndex + HISTORY_POINTS - historyCount + i) % HISTORY_POINTS;
    const float v = tempHistory[idx];
    if (isnan(v)) {
      continue;
    }
    if (v < minV) {
      minV = v;
    }
    if (v > maxV) {
      maxV = v;
    }
  }

  if (minV > maxV) {
    return;
  }

  if (maxV - minV < 0.5f) {
    maxV += 0.25f;
    minV -= 0.25f;
  }

  int16_t lastX = -1;
  int16_t lastY = -1;
  for (uint8_t i = 0; i < historyCount; ++i) {
    const uint8_t idx = (historyIndex + HISTORY_POINTS - historyCount + i) % HISTORY_POINTS;
    const float v = tempHistory[idx];
    if (isnan(v)) {
      lastX = -1;
      lastY = -1;
      continue;
    }

    const int16_t px = x + 1 + (int32_t)(w - 2) * i / (HISTORY_POINTS - 1);
    const float norm = (v - minV) / (maxV - minV);
    const int16_t py = y + h - 2 - (int16_t)((h - 2) * norm);

    if (lastX >= 0) {
      display.drawLine(lastX, lastY, px, py);
    }
    lastX = px;
    lastY = py;
  }
}

void drawRealtimeClock(int16_t x, int16_t y) {
  char clockText[9] = "--:--:--";
  const time_t now = time(nullptr);
  if (now > 100000) {
    struct tm localTime;
    localtime_r(&now, &localTime);
    snprintf(clockText, sizeof(clockText), "%02d:%02d:%02d",
             localTime.tm_hour, localTime.tm_min, localTime.tm_sec);
  }

  display.setCursor(x, y);
  display.print(clockText);
}

void drawSensorPage(bool wifiOk, bool mqttOk, float tempC, float humidity) {
  display.clearBuffer();

  drawStatusIcons(wifiOk, mqttOk);

  display.setFont(u8g2_font_6x10_tf);
  drawRealtimeClock(0, 10);

  drawTempGraph(72, 12, 54, 24);

  display.setFont(u8g2_font_5x8_tf);
  display.setCursor(72, 44);
  display.print("60m");

  display.setFont(u8g2_font_logisoso24_tf);
  display.setCursor(0, 42);
  if (isnan(tempC)) {
    display.print("--.-");
  } else {
    display.print(tempC, 1);
  }
  display.setFont(u8g2_font_6x10_tf);
  display.print(" C");

  display.setFont(u8g2_font_logisoso16_tf);
  display.setCursor(0, 62);
  if (isnan(humidity)) {
    display.print("--.-");
  } else {
    display.print(humidity, 1);
  }
  display.print(" %");

  display.sendBuffer();
}

void drawStatusPage(bool wifiOk, bool mqttOk) {
  display.clearBuffer();
  drawStatusIcons(wifiOk, mqttOk);

  const uint8_t dots = (millis() / ANIM_INTERVAL_MS) % 4;

  display.setFont(u8g2_font_6x10_tf);
  display.setCursor(0, 10);
  display.print("Status");
  if (!wifiOk || !mqttOk) {
    drawDots(36, 10, dots);
  }

  display.setCursor(0, 24);
  display.print("WiFi: ");
  if (wifiOk) {
    display.print("OK");
  } else {
    display.print("Wait");
    drawDots(48, 24, dots);
  }

  display.setCursor(0, 34);
  display.print("IP: ");
  if (wifiOk) {
    display.print(WiFi.localIP());
  } else {
    display.print("--");
  }

  display.setCursor(0, 44);
  display.print("RSSI: ");
  if (wifiOk) {
    display.print(WiFi.RSSI());
    display.print(" dBm");
  } else {
    display.print("--");
  }

  display.setCursor(0, 54);
  display.print("MQTT: ");
  if (mqttOk) {
    display.print("OK");
  } else {
    display.print("Wait");
    drawDots(54, 54, dots);
  }

  display.setCursor(0, 64);
  display.print("Last: ");
  if (lastPublishMs > 0) {
    display.print((millis() - lastPublishMs) / 1000);
    display.print("s");
  } else {
    display.print("--");
  }

  display.sendBuffer();
}

uint8_t activePage(bool wifiOk, bool mqttOk) {
  if (!wifiOk || !mqttOk) {
    return 1; // Status page when disconnected
  }

  const uint32_t now = millis();
  const uint32_t pageInterval = (pageIndex == 0)
                                    ? SENSOR_PAGE_INTERVAL_MS
                                    : STATUS_PAGE_INTERVAL_MS;
  if (now - lastPageMs >= pageInterval) {
    lastPageMs = now;
    pageIndex = (pageIndex + 1) % 2;
  }
  return pageIndex;
}

void updateDisplay(bool wifiOk, bool mqttOk, float tempC, float humidity) {
  const uint8_t page = activePage(wifiOk, mqttOk);
  if (page == 0) {
    drawSensorPage(wifiOk, mqttOk, tempC, humidity);
  } else {
    drawStatusPage(wifiOk, mqttOk);
  }
}

void buildTopics() {
  snprintf(topicBase, sizeof(topicBase), "%s/%s", MQTT_TOPIC_ROOT, deviceId);
  snprintf(topicTemperature, sizeof(topicTemperature), "%s/temperature_c", topicBase);
  snprintf(topicHumidity, sizeof(topicHumidity), "%s/humidity", topicBase);
  snprintf(topicStatus, sizeof(topicStatus), "%s/status", topicBase);
  snprintf(topicUptime, sizeof(topicUptime), "%s/uptime_s", topicBase);
}

void connectWiFi() {
  if (WiFi.status() == WL_CONNECTED) {
    return;
  }

  const uint32_t now = millis();
  if (now - lastWifiAttemptMs < WIFI_RETRY_MS) {
    return;
  }
  lastWifiAttemptMs = now;

  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
}

void connectMQTT() {
  if (mqttClient.connected()) {
    return;
  }

  if (WiFi.status() != WL_CONNECTED) {
    return;
  }

  const uint32_t now = millis();
  if (now - lastMqttAttemptMs < MQTT_RETRY_MS) {
    return;
  }
  lastMqttAttemptMs = now;

  const String clientId = String("esp8266-") + String(ESP.getChipId(), HEX);
  bool ok = false;
  if (strlen(MQTT_USER) > 0) {
    ok = mqttClient.connect(clientId.c_str(), MQTT_USER, MQTT_PASS,
                            topicStatus, 1, true, "offline");
  } else {
    ok = mqttClient.connect(clientId.c_str(),
                            topicStatus, 1, true, "offline");
  }

  if (ok) {
    mqttClient.publish(topicStatus, "online", true);
    discoveryPublished = false;
  }
}

bool publishDiscoverySensor(const char *configTopic,
                            const char *nameSuffix,
                            const char *uniqueSuffix,
                            const char *stateTopic,
                            const char *unit,
                            const char *deviceClass) {
  char payload[512];
  snprintf(payload, sizeof(payload),
           "{\"name\":\"%s %s\","
           "\"unique_id\":\"%s-%s\","
           "\"state_topic\":\"%s\","
           "\"unit_of_measurement\":\"%s\","
           "\"device_class\":\"%s\","
           "\"state_class\":\"measurement\","
           "\"availability_topic\":\"%s\","
           "\"payload_available\":\"online\","
           "\"payload_not_available\":\"offline\","
           "\"device\":{\"identifiers\":[\"%s\"],\"name\":\"%s\","
           "\"model\":\"ESP8266 + DHT22 + SH1106\",\"manufacturer\":\"Espressif\"}}",
           DEVICE_NAME, nameSuffix,
           deviceId, uniqueSuffix,
           stateTopic,
           unit,
           deviceClass,
           topicStatus,
           deviceId,
           DEVICE_NAME);

  const bool published = mqttClient.publish(configTopic, payload, true);
  if (!published) {
    Serial.print("Discovery publish failed: ");
    Serial.println(configTopic);
  }
  return published;
}

void publishDiscovery() {
  if (!mqttClient.connected() || discoveryPublished) {
    return;
  }

  const String node = String(deviceId);
  const String tempConfig = String(HA_DISCOVERY_PREFIX) + "/sensor/" + node + "/temperature/config";
  const String humConfig = String(HA_DISCOVERY_PREFIX) + "/sensor/" + node + "/humidity/config";
  const String uptimeConfig = String(HA_DISCOVERY_PREFIX) + "/sensor/" + node + "/uptime/config";

  const bool tempOk = publishDiscoverySensor(tempConfig.c_str(), "Temperature", "temperature",
                                             topicTemperature, "\\u00B0C", "temperature");
  const bool humOk = publishDiscoverySensor(humConfig.c_str(), "Humidity", "humidity",
                                            topicHumidity, "%", "humidity");
  const bool uptimeOk = publishDiscoverySensor(uptimeConfig.c_str(), "Uptime", "uptime",
                                               topicUptime, "s", "duration");

  discoveryPublished = tempOk && humOk && uptimeOk;
}

void publishSensor(float tempC, float humidity) {
  if (!mqttClient.connected()) {
    return;
  }

  char payload[32];
  if (!isnan(tempC)) {
    snprintf(payload, sizeof(payload), "%.1f", tempC);
    mqttClient.publish(topicTemperature, payload, true);
  }
  if (!isnan(humidity)) {
    snprintf(payload, sizeof(payload), "%.1f", humidity);
    mqttClient.publish(topicHumidity, payload, true);
  }

  snprintf(payload, sizeof(payload), "%lu", (unsigned long)(millis() / 1000));
  mqttClient.publish(topicUptime, payload, true);

  lastPublishMs = millis();
}

void updateHistoryIfDue() {
  const uint32_t now = millis();
  if (now - lastHistoryMs < HISTORY_INTERVAL_MS) {
    return;
  }
  lastHistoryMs = now;

  tempHistory[historyIndex] = lastTempC;
  historyIndex = (historyIndex + 1) % HISTORY_POINTS;
  if (historyCount < HISTORY_POINTS) {
    historyCount++;
  }
}

void setup() {
  pinMode(PIN_DHT_POWER, OUTPUT);
  digitalWrite(PIN_DHT_POWER, HIGH); // Enable DHT22 power/enable pin

  Serial.begin(115200);
  delay(200);

  Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL);
  display.begin();
  display.clear();

  dht.begin();

  configTime(CLOCK_GMT_OFFSET_SEC, 0, NTP_SERVER_1, NTP_SERVER_2);

  for (uint8_t i = 0; i < HISTORY_POINTS; ++i) {
    tempHistory[i] = NAN;
  }

  snprintf(deviceId, sizeof(deviceId), "esp8266-%06X", ESP.getChipId());
  buildTopics();

  mqttClient.setServer(MQTT_HOST, MQTT_PORT);
  mqttClient.setBufferSize(MQTT_PACKET_BUFFER_SIZE);

  connectWiFi();
}

void loop() {
  connectWiFi();
  connectMQTT();
  mqttClient.loop();

  publishDiscovery();
  updateHistoryIfDue();

  const uint32_t now = millis();
  if (now - lastSensorMs >= SENSOR_INTERVAL_MS) {
    lastSensorMs = now;

    float tempC = dht.readTemperature();
    float humidity = dht.readHumidity();

    lastTempC = tempC;
    lastHumidity = humidity;

    publishSensor(tempC, humidity);
  }

  updateDisplay(WiFi.status() == WL_CONNECTED, mqttClient.connected(), lastTempC, lastHumidity);
  delay(50);
}
