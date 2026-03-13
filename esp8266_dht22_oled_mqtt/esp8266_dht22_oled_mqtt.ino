#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <PubSubClient.h>
#include <Wire.h>
#include <U8g2lib.h>
#include <DHT.h>
#include <time.h>

#if __has_include("credentials.h")
#include "credentials.h"
#endif

#ifndef SECRET_WIFI_SSID
#define SECRET_WIFI_SSID "yournetwork"
#endif

#ifndef SECRET_WIFI_PASS
#define SECRET_WIFI_PASS "yournetworkpassword"
#endif

#ifndef SECRET_MQTT_HOST
#define SECRET_MQTT_HOST "mqtt-broker.local"
#endif

#ifndef SECRET_MQTT_PORT
#define SECRET_MQTT_PORT 1883
#endif

#ifndef SECRET_MQTT_USER
#define SECRET_MQTT_USER ""
#endif

#ifndef SECRET_MQTT_PASS
#define SECRET_MQTT_PASS ""
#endif


// ===== User configuration =====
static const char *WIFI_SSID = SECRET_WIFI_SSID;
static const char *WIFI_PASS = SECRET_WIFI_PASS;

// ===== MQTT configuration =====
static const char *MQTT_HOST = SECRET_MQTT_HOST;
static const uint16_t MQTT_PORT = SECRET_MQTT_PORT;
static const char *MQTT_USER = SECRET_MQTT_USER;
static const char *MQTT_PASS = SECRET_MQTT_PASS;
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
static const uint32_t PAGE_INTERVAL_MS = 15000;
static const uint32_t PAGE_TRANSITION_MS = 650;
static const uint32_t ANIM_INTERVAL_MS = 400;
static const uint32_t HISTORY_INTERVAL_MS = 60000;
static const uint8_t HISTORY_POINTS = 60;
static const uint8_t PAGE_COUNT = 3;

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
static uint32_t pageTransitionStartMs = 0;

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

void drawHeartIcon(int16_t x, int16_t y) {
  display.drawPixel(x + 1, y + 0);
  display.drawPixel(x + 2, y + 0);
  display.drawPixel(x + 4, y + 0);
  display.drawPixel(x + 5, y + 0);

  display.drawPixel(x + 0, y + 1);
  display.drawPixel(x + 1, y + 1);
  display.drawPixel(x + 2, y + 1);
  display.drawPixel(x + 3, y + 1);
  display.drawPixel(x + 4, y + 1);
  display.drawPixel(x + 5, y + 1);
  display.drawPixel(x + 6, y + 1);

  display.drawPixel(x + 1, y + 2);
  display.drawPixel(x + 2, y + 2);
  display.drawPixel(x + 3, y + 2);
  display.drawPixel(x + 4, y + 2);
  display.drawPixel(x + 5, y + 2);

  display.drawPixel(x + 2, y + 3);
  display.drawPixel(x + 3, y + 3);
  display.drawPixel(x + 4, y + 3);

  display.drawPixel(x + 3, y + 4);
}

void drawClockIconCute(int16_t x, int16_t y) {
  display.drawCircle(x + 3, y + 3, 3, U8G2_DRAW_ALL);
  display.drawLine(x + 3, y + 3, x + 3, y + 1);
  display.drawLine(x + 3, y + 3, x + 5, y + 3);
  display.drawPixel(x + 3, y + 0);
  display.drawPixel(x + 3, y + 6);
}

void drawPageIndicator(uint8_t activePage) {
  const int16_t startX = 56;
  const int16_t y = 61;
  for (uint8_t i = 0; i < PAGE_COUNT; ++i) {
    const int16_t x = startX + i * 8;
    if (i == activePage) {
      display.drawDisc(x, y, 2, U8G2_DRAW_ALL);
    } else {
      display.drawCircle(x, y, 2, U8G2_DRAW_ALL);
    }
  }
}

void drawCuteFrame() {
  display.drawRFrame(0, 0, 128, 64, 4);
  display.drawRFrame(2, 2, 124, 60, 3);
  drawHeartIcon(5, 5);
  drawHeartIcon(116, 5);
}

void drawRtcPage(bool wifiOk, bool mqttOk, int8_t xOffset, uint8_t activePage) {
  display.clearBuffer();
  drawStatusIcons(wifiOk, mqttOk);
  drawCuteFrame();
  drawClockIconCute(29 + xOffset, 7);

  char timeBig[6] = "--:--";
  char timeSec[3] = "--";
  char dateText[20] = "Tanggal belum sync";

  const time_t now = time(nullptr);
  if (now > 100000) {
    static const char *WEEKDAY[] = {"Min", "Sen", "Sel", "Rab", "Kam", "Jum", "Sab"};
    static const char *MONTH[] = {
      "Jan", "Feb", "Mar", "Apr", "Mei", "Jun",
      "Jul", "Agu", "Sep", "Okt", "Nov", "Des"
    };

    struct tm localTime;
    localtime_r(&now, &localTime);

    snprintf(timeBig, sizeof(timeBig), "%02d:%02d", localTime.tm_hour, localTime.tm_min);
    snprintf(timeSec, sizeof(timeSec), "%02d", localTime.tm_sec);
    snprintf(dateText, sizeof(dateText), "%s, %02d %s %04d",
             WEEKDAY[localTime.tm_wday],
             localTime.tm_mday,
             MONTH[localTime.tm_mon],
             localTime.tm_year + 1900);
  }

  display.setFont(u8g2_font_6x10_tf);
  display.setCursor(39 + xOffset, 14);
  display.print("RTC Clock");

  display.setFont(u8g2_font_logisoso24_tf);
  display.setCursor(10 + xOffset, 42);
  display.print(timeBig);

  display.setFont(u8g2_font_6x13_tf);
  display.setCursor(94 + xOffset, 40);
  display.print(timeSec);

  // Cute animated dots around the date line.
  const uint8_t dots = (millis() / ANIM_INTERVAL_MS) % 4;
  display.setFont(u8g2_font_5x8_tf);
  display.setCursor(8 + xOffset, 57);
  display.print(dateText);
  for (uint8_t i = 0; i < dots; ++i) {
    display.drawPixel(112 + xOffset + i * 3, 55);
  }

  drawPageIndicator(activePage);

  display.sendBuffer();
}

void drawSensorPage(bool wifiOk, bool mqttOk, float tempC, float humidity, int8_t xOffset, uint8_t activePage) {
  display.clearBuffer();

  drawStatusIcons(wifiOk, mqttOk);

  display.setFont(u8g2_font_6x10_tf);
  drawRealtimeClock(0 + xOffset, 10);

  drawTempGraph(72 + xOffset, 12, 54, 24);

  display.setFont(u8g2_font_5x8_tf);
  display.setCursor(72 + xOffset, 44);
  display.print("60m");

  display.setFont(u8g2_font_logisoso24_tf);
  display.setCursor(0 + xOffset, 42);
  if (isnan(tempC)) {
    display.print("--.-");
  } else {
    display.print(tempC, 1);
  }
  display.setFont(u8g2_font_6x10_tf);
  display.print(" C");

  display.setFont(u8g2_font_logisoso16_tf);
  display.setCursor(0 + xOffset, 62);
  if (isnan(humidity)) {
    display.print("--.-");
  } else {
    display.print(humidity, 1);
  }
  display.print("%");

  drawPageIndicator(activePage);

  display.sendBuffer();
}

void drawStatusPage(bool wifiOk, bool mqttOk, int8_t xOffset, uint8_t activePage) {
  display.clearBuffer();
  drawStatusIcons(wifiOk, mqttOk);

  const uint8_t dots = (millis() / ANIM_INTERVAL_MS) % 4;

  display.setFont(u8g2_font_6x10_tf);
  display.setCursor(0 + xOffset, 10);
  display.print("Status");
  if (!wifiOk || !mqttOk) {
    drawDots(36 + xOffset, 10, dots);
  }

  display.setCursor(0 + xOffset, 24);
  display.print("WiFi: ");
  if (wifiOk) {
    display.print("OK");
  } else {
    display.print("Wait");
    drawDots(48 + xOffset, 24, dots);
  }

  display.setCursor(0 + xOffset, 34);
  display.print("IP: ");
  if (wifiOk) {
    display.print(WiFi.localIP());
  } else {
    display.print("--");
  }

  display.setCursor(0 + xOffset, 44);
  display.print("RSSI: ");
  if (wifiOk) {
    display.print(WiFi.RSSI());
    display.print(" dBm");
  } else {
    display.print("--");
  }

  display.setCursor(0 + xOffset, 54);
  display.print("MQTT: ");
  if (mqttOk) {
    display.print("OK");
  } else {
    display.print("Wait");
    drawDots(54 + xOffset, 54, dots);
  }

  display.setCursor(0 + xOffset, 64);
  display.print("Last: ");
  if (lastPublishMs > 0) {
    display.print((millis() - lastPublishMs) / 1000);
    display.print("s");
  } else {
    display.print("--");
  }

  drawPageIndicator(activePage);

  display.sendBuffer();
}

int8_t pageSlideOffset() {
  const uint32_t elapsed = millis() - pageTransitionStartMs;
  if (elapsed >= PAGE_TRANSITION_MS) {
    return 0;
  }

  const int16_t startOffset = 18;
  return (int32_t)startOffset * (PAGE_TRANSITION_MS - elapsed) / PAGE_TRANSITION_MS;
}

uint8_t activePage(bool wifiOk, bool mqttOk) {
  if (!wifiOk || !mqttOk) {
    if (pageIndex != 1) {
      pageIndex = 1;
      pageTransitionStartMs = millis();
    }
    return 1; // Status page when disconnected
  }

  const uint32_t now = millis();
  if (now - lastPageMs >= PAGE_INTERVAL_MS) {
    lastPageMs = now;
    pageIndex = (pageIndex + 1) % PAGE_COUNT;
    pageTransitionStartMs = now;
  }
  return pageIndex;
}

void updateDisplay(bool wifiOk, bool mqttOk, float tempC, float humidity) {
  const uint8_t page = activePage(wifiOk, mqttOk);
  const int8_t xOffset = pageSlideOffset();
  if (page == 0) {
    drawSensorPage(wifiOk, mqttOk, tempC, humidity, xOffset, page);
  } else if (page == 1) {
    drawStatusPage(wifiOk, mqttOk, xOffset, page);
  } else {
    drawRtcPage(wifiOk, mqttOk, xOffset, page);
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
