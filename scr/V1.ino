/*Created by Melker H aka SA1CKW 20260213

GitHub https://github.com/Melkutt

I created this sketch for a DIN mounted ESP32S3 with a DHT22 to track temperature and humidity in my workshop.
I will test this and possibly modify the sketch with several other processors in the future.

For more info, check out this GitHub links
Homeys arduino lib, or "Homeyduino" --> https://github.com/athombv/homey-arduino-library
emilv and his ArduSorting lib --> https://github.com/emilv/ArduinoSort
Adafruit exelent lib --> https://github.com/adafruit/Adafruit_Sensor
Pycom's watchdog timer --> https://github.com/pycom/esp-idf-2.0/tree/master (obsolit)

*/

#include <WiFi.h>
#include <Homey.h>
#include <Adafruit_Sensor.h>
#include <DHT.h>
#include <ArduinoSort.h>
#include <ArduinoOTA.h>
#include "esp_task_wdt.h"

/* ================== CONFIG ================== */

#define FW_VERSION "1.0.0"

#define DHT_PIN 4
#define DHT_TYPE DHT22

#define SAMPLE_COUNT 5
#define SAMPLE_INTERVAL 2000UL
#define MAX_DHT_ERRORS 5
#define WIFI_RETRY_INTERVAL 10000UL

#define TEMP_THRESHOLD 0.2
#define HUM_THRESHOLD 1.0

float lastSentTemp = NAN;
float lastSentHum = NAN;

//Add your SSID and PASSWORD down below to your WIFI
const char* WIFI_SSID = "<SSID>";
const char* WIFI_PASS = "<PASSWORD>";

//Add your device namn
#define DEVICE_NAME "<Device name>"

// ---- Activate static IP ----
// If you dont want to use a static IP, comment out the row under
#define USE_STATIC_IP

#ifdef USE_STATIC_IP
  IPAddress local_IP(192, 168, 1, 40);
  IPAddress gateway(192, 168, 1, 1);
  IPAddress subnet(255, 255, 255, 0);
  IPAddress primaryDNS(8, 8, 8, 8);
  IPAddress secondaryDNS(8, 8, 4, 4);
#endif

/* ================== OTA CONFIG ================== */

#define ENABLE_OTA
#define OTA_PASSWORD "update123"   // You can leave it with "" if you don't want a password

/* ================== POWER CONFIG ================== */
/*Remember that you will have problems with OTA programming if you want deep sleep!
Delete the comment if you want deep sleep for ex. battery power*/

//#define USE_DEEP_SLEEP
#define SLEEP_DURATION_SEC 600

/* ================== SENSOR ================== */

DHT dht(DHT_PIN, DHT_TYPE);

/* ================== BUFFER ================== */

float tempArray[SAMPLE_COUNT];
float humiArray[SAMPLE_COUNT];

/* ================== STATE ================== */

unsigned long lastSampleTime = 0;
unsigned long lastWiFiAttempt = 0;

uint8_t sampleIndex = 0;
uint8_t dhtErrorCount = 0;

bool wifiConnected = false;

/* ================== SETUP ================== */

void setup() {

  /*watchdog timer reboots the microprocessor if it is stuck in the loop for more than 10 seconds*/

  esp_task_wdt_config_t wdt_config = {
    .timeout_ms = 10000,        // 10 seconds
    .idle_core_mask = (1 << portNUM_PROCESSORS) - 1,
    .trigger_panic = true
};

  esp_task_wdt_init(&wdt_config);
  esp_task_wdt_add(NULL); 
  esp_task_wdt_add(NULL);

  Serial.begin(115200);
  delay(100);
  Serial.print("Firmware: ");
  Serial.println(FW_VERSION);

  dht.begin();
  delay(2000);   // DHT power-up time

  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);
  WiFi.begin(WIFI_SSID, WIFI_PASS);

#ifdef USE_STATIC_IP
  if (!WiFi.config(local_IP, gateway, subnet, primaryDNS, secondaryDNS)) {
    Serial.println("[WiFi] Static IP configuration FAILED");
  } else {
    Serial.println("[WiFi] Static IP configured");
  }
#else
  Serial.println("[WiFi] DHCP mode");
#endif

  Homey.begin(DEVICE_NAME);
  Homey.setClass("sensor");
  Homey.addCapability("measure_temperature");
  Homey.addCapability("measure_humidity");

  #ifdef ENABLE_OTA

  ArduinoOTA.setHostname(DEVICE_NAME);

  if (strlen(OTA_PASSWORD) > 0) {
    ArduinoOTA.setPassword(OTA_PASSWORD);
  }

  ArduinoOTA
    .onStart([]() {
      Serial.println("[OTA] Starting update...");
    })
    .onEnd([]() {
      Serial.println("\n[OTA] ready!");
    })
    .onProgress([](unsigned int progress, unsigned int total) {
      Serial.printf("[OTA] Progress: %u%%\r", (progress * 100) / total);
    })
    .onError([](ota_error_t error) {
      Serial.printf("\n[OTA] Error [%u]: ", error);
      if (error == OTA_AUTH_ERROR) Serial.println("Auth Failed");
      else if (error == OTA_BEGIN_ERROR) Serial.println("Begin Failed");
      else if (error == OTA_CONNECT_ERROR) Serial.println("Connect Failed");
      else if (error == OTA_RECEIVE_ERROR) Serial.println("Receive Failed");
      else if (error == OTA_END_ERROR) Serial.println("End Failed");
    });

  ArduinoOTA.begin();
  Serial.println("[OTA] Ready to receive updates");

#endif
}

/* ================== LOOP ================== */

void loop() {

  Homey.loop();
  handleWiFi();
  handleDHT();
  esp_task_wdt_reset();

#ifdef ENABLE_OTA
  ArduinoOTA.handle();
#endif
}

/* ================== WIFI ================== */

void handleWiFi() {

  if (WiFi.status() == WL_CONNECTED) {
    if (!wifiConnected) {
      Serial.println("[WiFi] Connected");
      Serial.print("[WiFi] IP: ");
      Serial.println(WiFi.localIP());
      wifiConnected = true;
    }
    return;
  }

  wifiConnected = false;

  unsigned long now = millis();
  if (now - lastWiFiAttempt >= WIFI_RETRY_INTERVAL) {
    lastWiFiAttempt = now;
    Serial.println("[WiFi] Try to connect...");
    WiFi.disconnect();
    WiFi.begin(WIFI_SSID, WIFI_PASS);
  }
}

/* ================== DEEP SLEEP ================== */

void goToDeepSleep() {

  Serial.println("[POWER] Preparing for deep sleep...");

  WiFi.disconnect(true);
  WiFi.mode(WIFI_OFF);

  delay(100);

  esp_sleep_enable_timer_wakeup((uint64_t)SLEEP_DURATION_SEC * 1000000ULL);

  Serial.print("[POWER] Sleeps for ");
  Serial.print(SLEEP_DURATION_SEC);
  Serial.println(" seconds...");

  delay(50);
  Serial.flush();

  esp_deep_sleep_start();
}

/* ================== DHT STATE MACHINE ================== */

void handleDHT() {

  unsigned long now = millis();

  if (now - lastSampleTime < SAMPLE_INTERVAL) {
    return;
  }

  lastSampleTime = now;

  float h = dht.readHumidity();
  float t = dht.readTemperature();

  if (isnan(h) || isnan(t)) {

    dhtErrorCount++;
    Serial.println("[DHT] Reading error (NaN)");

    if (dhtErrorCount >= MAX_DHT_ERRORS) {
      Serial.println("[DHT] To many errors – reset buffer");
      resetSamples();
      dhtErrorCount = 0;
    }

    return;
  }

  dhtErrorCount = 0;

  tempArray[sampleIndex] = t;
  humiArray[sampleIndex] = h;
  sampleIndex++;

  Serial.print("Sample ");
  Serial.print(sampleIndex);
  Serial.print(": ");
  Serial.print(t);
  Serial.print(" °C, ");
  Serial.print(h);
  Serial.println(" %");

  if (sampleIndex >= SAMPLE_COUNT) {
    processSamples();
  }
}

/* ================== PROCESS ================== */

void processSamples() {

  sortArray(tempArray, SAMPLE_COUNT);
  sortArray(humiArray, SAMPLE_COUNT);

  float medianTemp = tempArray[SAMPLE_COUNT / 2];
  float medianHumi = humiArray[SAMPLE_COUNT / 2];

  Serial.println("---- MEDIAN ----");
  Serial.print("Temp: ");
  Serial.print(medianTemp);
  Serial.print(" °C  Hum: ");
  Serial.print(medianHumi);
  Serial.println(" %");

  bool sendTemp = false;
  bool sendHum  = false;

  if (isnan(lastSentTemp) || abs(medianTemp - lastSentTemp) > TEMP_THRESHOLD) {
    sendTemp = true;
  }

  if (isnan(lastSentHum) || abs(medianHumi - lastSentHum) > HUM_THRESHOLD) {
    sendHum = true;
  }

  if (sendTemp || sendHum) {

    Serial.println("[SEND] Change across threshold, sends to Homey");

    if (sendTemp) {
      Homey.setCapabilityValue("measure_temperature", medianTemp);
      lastSentTemp = medianTemp;
    }

    if (sendHum) {
      Homey.setCapabilityValue("measure_humidity", medianHumi);
      lastSentHum = medianHumi;
    }

  } else {
    Serial.println("[SEND] No significant change");
  }

  resetSamples();
}
/* ================== RESET ================== */

void resetSamples() {

  memset(tempArray, 0, sizeof(tempArray));
  memset(humiArray, 0, sizeof(humiArray));
  sampleIndex = 0;
}
