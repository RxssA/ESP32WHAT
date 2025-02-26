#include <Wire.h>
#include <Adafruit_Sensor.h>
#include <Adafruit_MCP9808.h>
#include <Adafruit_GPS.h>
#include <WiFi.h>
#include <HTTPClient.h>
// Wi-Fi Credentials
const char* SECRET_SSID = "VM8841012";
const char* SECRET_PASS = "ewPq5o2hiWzknurt";
//const char* SECRET_SSID = "iPhone";
//const char* SECRET_PASS = "Rossarmo123";
const char* serverUrl = "http://192.168.0.23:4000/data";

// Initialize sensors
Adafruit_MCP9808 tempsensor = Adafruit_MCP9808();
Adafruit_GPS GPS(&Serial1); // Use Serial1 for GPS (TX, RX pins)

// Heart rate pin and variables
#define heartPin A5
const int numReadings = 20;
int readings[numReadings] = {0};
int readIndex = 0, total = 0, average = 0, lastAverage = 0;
bool rising = false;
unsigned long lastPeakTime = 0;
const int bufferSize = 10;
int bpmBuffer[bufferSize] = {0}, bpmIndex = 0, totalBpm = 0, avgBpm = 0;
int highestBpm = 0, lowestBpm = 999;
unsigned long lastSendTime = 0;


 void sendHttpRequestTask(void* pvParameters) {
  String payload = *(String*)pvParameters;
  delete (String*)pvParameters;

  WiFiClient client;
  HTTPClient http;
  http.begin(client, serverUrl);  // Explicitly use WiFiClient
  http.addHeader("Content-Type", "application/json");
  
  int httpCode = http.POST(payload);
  if (httpCode <= 0) {
    Serial.printf("HTTP Error: %s\n", http.errorToString(httpCode).c_str());
  } else {
    Serial.printf("HTTP Code: %d\n", httpCode);
  }
  http.end();
  vTaskDelete(NULL);
}


void sendHttpRequestAsync(String payload){
    String* payloadCopy = new String(payload);
    xTaskCreatePinnedToCore(sendHttpRequestTask, "HttpTask", 12288, payloadCopy, 1 ,NULL,0);
}

void setup() {
  Serial.begin(115200);
  Serial1.begin(115200);

  WiFi.begin(SECRET_SSID, SECRET_PASS);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nConnected to WiFi");

  if (!tempsensor.begin()) {
    Serial.println("Couldn't find MCP9808 sensor!");
  }

  GPS.begin(9600);
  GPS.sendCommand(PMTK_SET_NMEA_OUTPUT_RMCGGA);
  GPS.sendCommand(PMTK_SET_NMEA_UPDATE_1HZ);
  GPS.sendCommand(PGCMD_ANTENNA);
  Wire.begin(21, 22);
  Serial.println("Sensors Initialized");
}

void loop() {
  int heartValue = analogRead(heartPin);
  unsigned long currentMillis = millis();

  // Calculate moving average
  total -= readings[readIndex];
  readings[readIndex] = heartValue;
  total += readings[readIndex];
  readIndex = (readIndex + 1) % numReadings;
  average = total / numReadings;

  // Detect peak and calculate BPM
  if (average > lastAverage) {
    rising = true;
  } else if (average < lastAverage && rising) {
    unsigned long currentTime = millis();
    if (lastPeakTime != 0) {
      unsigned long timeBetweenPeaks = currentTime - lastPeakTime;
      if (timeBetweenPeaks > 300) { // Ignore peaks too close (~200 BPM max)
        int bpm = 60000 / timeBetweenPeaks;
        
        // Update BPM buffer
        totalBpm -= bpmBuffer[bpmIndex];
        bpmBuffer[bpmIndex] = bpm;
        totalBpm += bpmBuffer[bpmIndex];
        bpmIndex = (bpmIndex + 1) % bufferSize;
        avgBpm = totalBpm / bufferSize;

        // Update highest and lowest BPM
        if (bpm > highestBpm) highestBpm = bpm;
        if (bpm < lowestBpm) lowestBpm = bpm;
      }
    }
    lastPeakTime = currentTime;
    rising = false;
  }
  lastAverage = average;

  // Reset BPM if no peaks detected for 3 seconds
  if (millis() - lastPeakTime > 3000) {
    avgBpm = 0;
    highestBpm = 0;
    lowestBpm = 999;
    totalBpm = 0;
    memset(bpmBuffer, 0, sizeof(bpmBuffer));
  }

  // Read temperature
  float celsius = tempsensor.readTempC();

  // Read GPS data
  while (GPS.available()) {
    char c = GPS.read();
    if (GPS.newNMEAreceived()) {
      if (!GPS.parse(GPS.lastNMEA())) continue;
    }
  }
  
  // Get real GPS data, fallback to fixed coordinates if unavailable
  float gpsLat = (GPS.latitudeDegrees != 0) ? GPS.latitudeDegrees : 53.270962;
  float gpsLng = (GPS.longitudeDegrees != 0) ? GPS.longitudeDegrees : -9.062691;

  // Ensure sending data every 5 seconds
  if (currentMillis - lastSendTime >= 1000) {
    lastSendTime = currentMillis;
    String payload = "{";
    payload += "\"heartRate\":" + String(avgBpm);
    payload += ",\"averageHeartRate\":" + String(avgBpm);
    payload += ",\"highestHeartRate\":" + String(highestBpm);
    payload += ",\"lowestHeartRate\":" + String(lowestBpm);
    payload += ",\"temperature\":" + String(celsius, 2);
    payload += ",\"location\":{\"lat\":" + String(gpsLat, 6);
    payload += ",\"lng\":" + String(gpsLng, 6) + "}";
    payload += "}";

    sendHttpRequestAsync(payload);
    Serial.println("Sent: " + payload);
  }

  Serial.print("Heart Rate (BPM): ");
  Serial.println(avgBpm > 0 ? String(avgBpm) : "Calculating...");

  delay(20);
}
