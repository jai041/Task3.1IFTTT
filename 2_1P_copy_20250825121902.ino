#include <WiFiNINA.h>
#include <ArduinoHttpClient.h>
#include <Wire.h>
#include <BH1750.h>

// ---------- USER CONFIG ----------
const char* WIFI_SSID = "Ok Bro";
const char* WIFI_PASS = "        ";

const char* AIO_USERNAME = "jaideep4807";
const char* AIO_KEY      = "aio_LlpD184YFmrqKtfCc0M86KsLiaVl";

// Adafruit IO REST host/port
const char* AIO_HOST = "io.adafruit.com";
const int   AIO_HTTPS_PORT = 443;

// Feeds (create on Adafruit IO first)
const char* FEED_EVENTS = "terrarium-events"; // text: "sun_start" / "sun_stop (xxxx lux)"
const char* FEED_LUX    = "terrarium-lux";    // optional numeric snapshots

// Sunlight thresholds (tune after a quick calibration in your location)
const float THRESH_START_LUX = 12000.0;  // sunlight "starts" when lux >= this
const float THRESH_STOP_LUX  =  8000.0;  // sunlight "stops"  when lux <= this

// Optional: post lux every N seconds (set 0 to disable)
const unsigned long LUX_POST_INTERVAL_MS = 60UL * 1000UL; // 60s

// Optional: how often to sample the BH1750 for smoothing
const uint8_t SAMPLE_COUNT = 5;
const unsigned long SAMPLE_GAP_MS = 100;

// ---------- GLOBALS ----------
BH1750 lightMeter;
WiFiSSLClient wifi;
HttpClient client(wifi, AIO_HOST, AIO_HTTPS_PORT);

bool inSun = false;
unsigned long lastLuxPost = 0;

void connectWiFi() {
  if (WiFi.status() == WL_CONNECTED) return;
  WiFi.disconnect();
  Serial.print("Connecting to WiFi: "); Serial.println(WIFI_SSID);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  unsigned long t0 = millis();
  while (WiFi.status() != WL_CONNECTED && (millis() - t0) < 20000) {
    delay(500);
    Serial.print(".");
  }
  Serial.println();
  if (WiFi.status() == WL_CONNECTED) {
    Serial.print("WiFi OK. IP: "); Serial.println(WiFi.localIP());
  } else {
    Serial.println("WiFi connect failed. Will retry...");
  }
}

bool aioPostJSON(const String& feedKey, const String& jsonBody) {
  String path = "/api/v2/" + String(AIO_USERNAME) + "/feeds/" + String(feedKey) + "/data";
  // Start HTTPS POST
  client.beginRequest();
  client.post(path);
  client.sendHeader("Content-Type", "application/json");
  client.sendHeader("X-AIO-Key", AIO_KEY);
  client.sendHeader("Content-Length", jsonBody.length());
  client.beginBody();
  client.print(jsonBody);
  client.endRequest();

  int status = client.responseStatusCode();
  String resp = client.responseBody();
  Serial.print("POST "); Serial.print(feedKey); Serial.print(" -> ");
  Serial.print(status); Serial.print(" | "); Serial.println(resp);

  return (status >= 200 && status < 300);
}

float readLuxSmoothed() {
  float sum = 0;
  uint8_t n = 0;
  for (uint8_t i = 0; i < SAMPLE_COUNT; i++) {
    if (lightMeter.measurementReady(true)) {
      float lux = lightMeter.readLightLevel(); // in lux
      sum += lux;
      n++;
    }
    delay(SAMPLE_GAP_MS);
  }
  if (n == 0) return NAN;
  return sum / n;
}

void sendEvent(const String& label, float lux) {
  // Put lux inside the text for your IFTTT notification
  String value = label + " (" + String(lux, 0) + " lux)";
  String body = String("{\"value\":\"") + value + "\"}";
  aioPostJSON(FEED_EVENTS, body);
}

void postLux(float lux) {
  // Adafruit IO expects {"value": <string or number>}
  String body = String("{\"value\":") + String(lux, 1) + "}";
  aioPostJSON(FEED_LUX, body);
}

void setup() {
  Serial.begin(115200);
  while (!Serial) {;}

  Wire.begin();
  if (lightMeter.begin(BH1750::CONTINUOUS_HIGH_RES_MODE)) {
    Serial.println("BH1750 ready.");
  } else {
    Serial.println("BH1750 init failed. Check wiring/power.");
  }

  connectWiFi();
}

void loop() {
  if (WiFi.status() != WL_CONNECTED) {
    connectWiFi();
  }

  float lux = readLuxSmoothed();
  if (!isnan(lux)) {
    Serial.print("Lux: "); Serial.println(lux, 1);

    // State machine with hysteresis
    if (!inSun && lux >= THRESH_START_LUX) {
      inSun = true;
      sendEvent("sun_start", lux); // IFTTT will notify
    } else if (inSun && lux <= THRESH_STOP_LUX) {
      inSun = false;
      sendEvent("sun_stop", lux);  // IFTTT will notify
    }

    // Optional periodic lux posting (for charts on Adafruit IO dashboard)
    if (LUX_POST_INTERVAL_MS > 0 && (millis() - lastLuxPost >= LUX_POST_INTERVAL_MS)) {
      postLux(lux);
      lastLuxPost = millis();
    }
  }

  delay(500); // base loop delay
}