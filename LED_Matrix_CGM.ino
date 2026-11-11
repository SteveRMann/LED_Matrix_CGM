/*
  LED Matrix MQTT display
  Board  : Wemos D1 Mini (ESP8266)
  Matrix : 8x32 WS2812B (NeoPixel) panel, DIN on D4 (GPIO2)
  Shows the plain-text payload from MQTT topic "cgm/bg",
  followed by a trend symbol from "cgm/trend" (1-7, 0 = none).
  Brightness set by "cgm/bright" (0-9, scaled to BRIGHT_MIN..BRIGHT_MAX).

  Libraries (Library Manager):
    - PubSubClient (Nick O'Leary)
    - Adafruit NeoMatrix  (pulls in Adafruit GFX + Adafruit NeoPixel)

  WiFi / MQTT credentials live in secrets.h (not in git).
  Copy secrets_sample.h to secrets.h and fill in your values.
*/

#include <ESP8266WiFi.h>
#include <PubSubClient.h>
#include <Adafruit_GFX.h>
#include <Adafruit_NeoMatrix.h>
#include <Adafruit_NeoPixel.h>
#include <ESP8266NetBIOS.h>  //answer NetBIOS (and mDNS)
#include <ESP8266mDNS.h>
#include "secrets.h"  // WIFI_SSID, WIFI_PASS, MQTT_HOST, MQTT_PORT, MQTT_USER, MQTT_PASS

// ---------- User settings ----------
const char* MQTT_TOPIC_BG = "cgm/bg";
const char* MQTT_TOPIC_TREND = "cgm/trend";
const char* MQTT_TOPIC_BRIGHT = "cgm/bright";

#define MATRIX_PIN D4
#define BRIGHTNESS 1  // 0-255. Startup brightness until cgm/bright arrives
#define BRIGHT_MIN 1  // cgm/bright 0 -> this
#define BRIGHT_MAX 4  // cgm/bright 9 -> this

// Color bands for numeric values (mg/dL)
const int BG_URGENT_LOW = 55;
const int BG_LOW = 70;
const int BG_HIGH = 180;
const int BG_URGENT_HIGH = 250;

#define TREND_GREY 120  // grey level (0-255) for the trend symbol

const unsigned long STALE_MS = 15UL * 60UL * 1000UL;  // grey out after 15 min with no update
const unsigned long SCROLL_MS = 60;                   // scroll speed for long text

// Friendly names per board (chip ID is printed on Serial at boot).
// Boards not listed get "LEDMatrix-<chipid>".
struct DeviceName {
  uint32_t chipId;
  const char* name;
};

const DeviceName DEVICE_NAMES[] = {
  { 0x8B45AC, "cgm-matrix-1" },  // MAC 84:0D:8E:8B:45:AC
  { 0x0DD020, "cgm-matrix-2" },  // MAC D8:F1:5B:0D:D0:20
};                               // -----------------------------------



// Most 8x32 flexible panels are column-wired zigzag starting top-left.
// If text appears scrambled/mirrored, change these flags.
Adafruit_NeoMatrix matrix(32, 8, MATRIX_PIN,
                          NEO_MATRIX_TOP + NEO_MATRIX_LEFT + NEO_MATRIX_COLUMNS + NEO_MATRIX_ZIGZAG,
                          NEO_GRB + NEO_KHZ800);

WiFiClient wifiClient;
PubSubClient mqtt(wifiClient);


// ---------- Trend arrows ----------
// The built-in font has no arrow glyphs, so they are drawn as 7-row bitmaps
// (one byte per row, leftmost pixel = MSB).
const uint8_t ARROW_UP[] PROGMEM = { 0x20, 0x70, 0xA8, 0x20, 0x20, 0x20, 0x20 };     // ↑ 5 wide
const uint8_t ARROW_DOWN[] PROGMEM = { 0x20, 0x20, 0x20, 0x20, 0xA8, 0x70, 0x20 };   // ↓ 5 wide
const uint8_t ARROW_UP2[] PROGMEM = { 0x44, 0xEE, 0x44, 0x44, 0x44, 0x44, 0x44 };    // ⇈ 7 wide
const uint8_t ARROW_DOWN2[] PROGMEM = { 0x44, 0x44, 0x44, 0x44, 0x44, 0xEE, 0x44 };  // ⇊ 7 wide

struct TrendStyle {
  const char* suffix;
  const uint8_t* glyph;
  uint8_t glyphW;
};
const TrendStyle TRENDS[8] = {
  { "", nullptr, 0 },      // 0 unknown -> nothing appended
  { "", ARROW_UP2, 7 },    // 1 rising_quickly   ⇈
  { "", ARROW_UP, 5 },     // 2 rising           ↑
  { "/", nullptr, 0 },     // 3 rising_slightly
  { "-", nullptr, 0 },     // 4 steady
  { "\\", nullptr, 0 },    // 5 falling_slightly
  { "", ARROW_DOWN, 5 },   // 6 falling          ↓
  { "", ARROW_DOWN2, 7 },  // 7 falling_quickly  ⇊
};
// -----------------------------------

char text[64] = "WiFi";
char bgValue[32] = "";
int trend = 0;
const uint8_t* glyph = nullptr;  // arrow drawn after text (nullptr = none)
uint8_t glyphW = 0;
uint8_t valueLen = 0;  // chars of text drawn in value color; rest is trend (grey)
uint16_t textColor;
int textWidth = 0;
int scrollX = 32;
bool haveValue = false;
bool needRedraw = true;
unsigned long lastMsgMs = 0;
unsigned long lastScrollMs = 0;
unsigned long lastMqttTryMs = 0;



// -------------------- setDeviceName() --------------------
char deviceName[33];

void setDeviceName() {
  uint32_t id = ESP.getChipId();
  for (const auto& d : DEVICE_NAMES) {
    if (d.chipId == id) {
      strlcpy(deviceName, d.name, sizeof(deviceName));
      return;
    }
  }
  snprintf(deviceName, sizeof(deviceName), "LEDMatrix-%06X", id);
}



// -------------------- colorFor() --------------------
uint16_t colorFor(const char* s) {
  char* end;
  long v = strtol(s, &end, 10);
  if (end == s) return matrix.Color(255, 255, 255);  // not a number -> white
  if (v <= BG_URGENT_LOW || v >= BG_URGENT_HIGH) return matrix.Color(255, 0, 0);
  if (v < BG_LOW || v > BG_HIGH) return matrix.Color(255, 160, 0);
  return matrix.Color(0, 255, 0);
}



// -------------------- setText() --------------------
void setText(const char* s, uint16_t color) {
  strncpy(text, s, sizeof(text) - 1);
  text[sizeof(text) - 1] = '\0';
  textColor = color;
  textWidth = strlen(text) * 6 - 1;  // default 5x7 font = 6 px per char incl. gap
  valueLen = strlen(text);
  glyph = nullptr;
  glyphW = 0;
  scrollX = 32;
  needRedraw = true;
}


// -------------------- showReading() --------------------
// Compose "<bg><trend>" and show it
void showReading() {
  const TrendStyle& t = TRENDS[(trend >= 0 && trend <= 7) ? trend : 0];
  char buf[64];
  snprintf(buf, sizeof(buf), "%s%s", bgValue, t.suffix);
  setText(buf, colorFor(bgValue));
  valueLen = min(strlen(bgValue), strlen(text));
  if (t.glyph) {
    glyph = t.glyph;
    glyphW = t.glyphW;
    textWidth = strlen(text) * 6 + glyphW;  // text (incl. 1px char gap) + arrow
  }
}


// -------------------- mqttCallback() --------------------
void mqttCallback(char* topic, byte* payload, unsigned int length) {
  char buf[64];
  unsigned int n = min(length, (unsigned int)(sizeof(buf) - 1));
  memcpy(buf, payload, n);
  buf[n] = '\0';

  // trim whitespace / newlines
  char* start = buf;
  while (*start && isspace((unsigned char)*start)) start++;
  char* end = start + strlen(start);
  while (end > start && isspace((unsigned char)end[-1])) *--end = '\0';

  if (strcmp(topic, MQTT_TOPIC_BRIGHT) == 0) {
    static int lastLevel = -1;
    int level = constrain(atoi(start), 0, 9);
    if (level != lastLevel) {
      lastLevel = level;
      uint8_t b = BRIGHT_MIN + (long)level * (BRIGHT_MAX - BRIGHT_MIN) / 9;
      Serial.printf("MQTT [%s]: %d -> brightness %u\n", topic, level, b);
      matrix.setBrightness(b);
      needRedraw = true;
    }
    return;
  }

  if (strcmp(topic, MQTT_TOPIC_TREND) == 0) {
    int t = atoi(start);
    if (t < 0 || t > 7) t = 0;
    if (t != trend) {
      Serial.printf("MQTT [%s]: %s\n", topic, start);
      trend = t;
      if (haveValue) showReading();
    }
    return;
  }

  // cgm/bg
  static char lastValue[64] = "";
  if (strcmp(start, lastValue) != 0) {
    Serial.printf("MQTT [%s]: %s\n", topic, start);
    strncpy(lastValue, start, sizeof(lastValue) - 1);
    lastValue[sizeof(lastValue) - 1] = '\0';
  }
  strncpy(bgValue, start, sizeof(bgValue) - 1);
  bgValue[sizeof(bgValue) - 1] = '\0';
  haveValue = true;
  lastMsgMs = millis();
  showReading();
}

// -------------------- mqttConnect() --------------------
void mqttConnect() {
  if (mqtt.connected())
    return;

  if (WiFi.status() != WL_CONNECTED) {
    Serial.printf(
      "MQTT skipped, WiFi=%d RSSI=%d\n",
      WiFi.status(),
      WiFi.RSSI());
    return;
  }

  if (millis() - lastMqttTryMs < 5000)
    return;

  Serial.printf(
    "WiFi=%d RSSI=%d IP=%s\n",
    WiFi.status(),
    WiFi.RSSI(),
    WiFi.localIP().toString().c_str());

  lastMqttTryMs = millis();

  //String clientId = "LEDMatrix-" + String(ESP.getChipId(), HEX);
  String clientId = deviceName;

  bool ok = (strlen(MQTT_USER) > 0)
              ? mqtt.connect(clientId.c_str(), MQTT_USER, MQTT_PASS)
              : mqtt.connect(clientId.c_str());

  if (ok) {
    Serial.println("MQTT connected");

    mqtt.subscribe(MQTT_TOPIC_BG);
    mqtt.subscribe(MQTT_TOPIC_TREND);
    mqtt.subscribe(MQTT_TOPIC_BRIGHT);

    if (!haveValue)
      setText("---", matrix.Color(80, 80, 80));
  } else {
    Serial.printf(
      "MQTT failed, rc=%d WiFi=%d RSSI=%d Heap=%u\n",
      mqtt.state(),
      WiFi.status(),
      WiFi.RSSI(),
      ESP.getFreeHeap());

    if (!haveValue)
      setText("MQTT", matrix.Color(255, 0, 0));
  }
}


// -------------------- drawDisplay() --------------------
void drawDisplay() {
  // When stale, show only the value (no trend), greyed out
  bool stale = haveValue && millis() - lastMsgMs > STALE_MS;
  uint8_t len = stale ? valueLen : strlen(text);
  int width = stale ? valueLen * 6 - 1 : textWidth;
  bool showGlyph = glyph && !stale;

  bool scrolling = width > 32;
  if (!needRedraw && !scrolling) return;
  if (scrolling && millis() - lastScrollMs < SCROLL_MS) return;
  lastScrollMs = millis();
  needRedraw = false;

  uint16_t c = stale ? matrix.Color(60, 60, 60) : textColor;
  uint16_t tc = matrix.Color(TREND_GREY, TREND_GREY, TREND_GREY);  // trend is always grey

  matrix.fillScreen(0);
  if (scrolling) {
    matrix.setCursor(scrollX, 0);
    if (--scrollX < -width) scrollX = 32;
  } else {
    matrix.setCursor((32 - width) / 2, 0);
  }
  for (uint8_t i = 0; i < len; i++) {
    matrix.setTextColor(i < valueLen ? c : tc);
    matrix.print(text[i]);
  }
  if (showGlyph) matrix.drawBitmap(matrix.getCursorX(), 0, glyph, glyphW, 7, tc);

  // Small red dot bottom-right when MQTT is down
  if (!mqtt.connected()) matrix.drawPixel(31, 7, matrix.Color(255, 0, 0));

  matrix.show();
}


// -------------------- setup() --------------------
void setup() {
  Serial.begin(115200);
  delay(500);
  Serial.println();

  matrix.begin();
  matrix.setTextWrap(false);
  matrix.setBrightness(BRIGHTNESS);
  setText("WiFi", matrix.Color(0, 0, 255));
  drawDisplay();

  setDeviceName();
  Serial.printf("Chip ID: 0x%06X  Name: %s\n", ESP.getChipId(), deviceName);

  WiFi.mode(WIFI_STA);
  WiFi.setSleepMode(WIFI_NONE_SLEEP);   // keep radio awake: low latency, no dropped pings
  WiFi.hostname(deviceName);
  WiFi.setAutoReconnect(true);          // rejoin the access point if the connection drops
  WiFi.begin(WIFI_SSID, WIFI_PASS);

  Serial.print("Connecting to WiFi");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(WiFi.status());
  }

  Serial.println();
  Serial.println("WiFi connected");
  Serial.print("IP Address: ");
  Serial.println(WiFi.localIP());

  Serial.print("MAC Address: ");
  Serial.println(WiFi.macAddress());

  NBNS.begin(deviceName);  // Windows IP scanners, ping by name
  MDNS.begin(deviceName);  // reachable as cgm-matrix-1.local

  mqtt.setServer(MQTT_HOST, MQTT_PORT);
  mqtt.setCallback(mqttCallback);
}


// -------------------- loop() --------------------
void loop() {
  static bool wasStale = false;
  static bool wasConnected = false;

  mqttConnect();
  mqtt.loop();
  MDNS.update();

  // Redraw when stale state or connection state flips
  bool stale = haveValue && millis() - lastMsgMs > STALE_MS;
  if (stale != wasStale || mqtt.connected() != wasConnected) needRedraw = true;
  wasStale = stale;
  wasConnected = mqtt.connected();

  drawDisplay();
}
