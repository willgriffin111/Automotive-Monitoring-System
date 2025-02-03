#include <Arduino.h>
#include <Wire.h>
#include <SPI.h>
#include <SparkFun_u-blox_GNSS_Arduino_Library.h>
#include "obd.hpp"
#include <SdFat.h>
#include <ArduinoJson.h>
#include <WiFi.h>
#include <WebServer.h>

#define LOG_TO_SD true    
#define SD_CS_PIN A0   

// WiFi Access Point 
const char* ssid = "MyESP32AP";
const char* password = "12345678";
WebServer server(80);

char folderName[20]; 
char fileName[40];  


OBD obd;
SFE_UBLOX_GNSS myGNSS;  
SdFat SD;
FsFile logFile;


float totalSpeedTimeProduct = 0.0;
float totalFuelTimeProduct = 0.0;
unsigned long lastTime = 0;

bool isCalibrated = false;  

bool firstLog = true;

void handleRoot() {
  server.sendHeader("Access-Control-Allow-Origin", "*");
  String message = "Connected";
  server.send(200, "text/plain", message);
}

void handleDays() {
  server.sendHeader("Access-Control-Allow-Origin", "*");
  Serial.println("Listing available days (directories)...");
  
  // Open the root directory.
  FsFile root = SD.open("/");
  if (!root) {
    Serial.println("Failed to open root directory");
    server.send(500, "text/plain", "Failed to open root directory");
    return;
  }

  String json = "[";
  bool first = true;
  
  // Iterate over items in the root directory.
  while (true) {
    FsFile entry = root.openNextFile();
    if (!entry) break;  // No more files/folders.
    
    if (entry.isDir()) {
      char nameBuffer[32];
      entry.getName(nameBuffer, sizeof(nameBuffer));
      
      // Skip hidden directories (names starting with a dot)
      if (nameBuffer[0] == '.') {
        entry.close();
        continue;
      }
      
      if (!first) {
        json += ",";
      }
      json += "\"";
      json += nameBuffer;
      json += "\"";
      first = false;
    }
    entry.close();
  }
  json += "]";
  root.close();
  
  server.send(200, "application/json", json);
}

void handleDrives() {
  server.sendHeader("Access-Control-Allow-Origin", "*");
  if (!server.hasArg("day")) {
    server.send(400, "text/plain", "Missing 'day' parameter");
    return;
  }
  
  String day = server.arg("day");
  Serial.print("Listing drives for day: ");
  Serial.println(day);
  
  // Build the full path to the day folder.
  String path = "/" + day;
  FsFile dayDir = SD.open(path.c_str());
  if (!dayDir || !dayDir.isDir()) {
    Serial.println("Day folder not found");
    server.send(404, "text/plain", "Day folder not found");
    return;
  }
  
  String json = "[";
  bool first = true;
  
  // Iterate over the files in the day folder.
  while (true) {
    FsFile entry = dayDir.openNextFile();
    if (!entry) break;
    
    // Only include files (not subdirectories).
    if (!entry.isDir()) {
      char nameBuffer[32];
      entry.getName(nameBuffer, sizeof(nameBuffer));
      
      // Skip hidden files (names starting with a dot)
      if (nameBuffer[0] == '.') {
        entry.close();
        continue;
      }
      
      if (!first) {
        json += ",";
      }
      json += "\"";
      json += nameBuffer;
      json += "\"";
      first = false;
    }
    entry.close();
  }
  json += "]";
  dayDir.close();
  
  server.send(200, "application/json", json);
}

void handleDrive() {
  server.sendHeader("Access-Control-Allow-Origin", "*");
  if (!server.hasArg("day") || !server.hasArg("drive")) {
    server.send(400, "text/plain", "Missing 'day' or 'drive' parameter");
    return;
  }
  
  String day = server.arg("day");
  String driveFile = server.arg("drive");
  
  // Reject if the day or drive file starts with a dot
  if (day.startsWith(".") || driveFile.startsWith(".")) {
    server.send(403, "text/plain", "Access to hidden files is forbidden");
    return;
  }
  
  Serial.print("Fetching drive data for day: ");
  Serial.print(day);
  Serial.print(", drive file: ");
  Serial.println(driveFile);
  
  // Build the full path to the drive file.
  String path = "/" + day + "/" + driveFile;
  FsFile file = SD.open(path.c_str(), O_READ);
  if (!file) {
    Serial.println("Drive file not found");
    server.send(404, "text/plain", "Drive file not found");
    return;
  }
  
  // Read the file content.
  String content;
  while (file.available()) {
    content += (char)file.read();
  }
  // Serial.println(content);

  file.close();
  
  server.send(200, "application/json", content);
}


void calibrateGNNS() {

    Serial.println("Starting GNSS Calibration...");

    while (!isCalibrated) {
        if (myGNSS.getEsfInfo()) {
            int fusionMode = myGNSS.packetUBXESFSTATUS->data.fusionMode;
            Serial.print(F("Fusion Mode: "));
            Serial.println(fusionMode);

            if (fusionMode == 1) {
                Serial.println(F(" Calibrated!"));
                isCalibrated = true;
            } else {
                Serial.println(F(" → Initialising... Perform calibration maneuvers."));
            }
        } else {
            Serial.println(F("Failed to retrieve ESF Info. Retrying..."));
        }
        delay(1000); 
    }

    Serial.println(F("Calibration Complete!"));
}


void setup() {
    Serial.begin(115200);
    delay(1000);


    Serial.println("Initialising System...");
    Serial.println("Setting up WiFi Access Point...");
    WiFi.softAP(ssid, password);
    Serial.print("AP IP address: ");
    Serial.println(WiFi.softAPIP());
    
    // Define API endpoints.
    server.on("/", HTTP_GET, handleRoot);
    server.on("/days", HTTP_GET, handleDays);
    server.on("/drives", HTTP_GET, handleDrives);
    server.on("/drive", HTTP_GET, handleDrive);
    
    // Start the web server.
    server.begin();
    Serial.println("Web server started.");
    
    // SD Card Initialization
    Wire1.setPins(SDA1, SCL1);
    
    Wire1.begin();

    if (!SD.begin(SD_CS_PIN)) {
        Serial.println("SD card initialization failed!");
    } else {
        Serial.println("SD card initialized successfully.");
    }
    
    // Initialize GPS Module/IMU

    if (myGNSS.begin(Wire1)) {
        Serial.println("GPS Module Initialized");
        myGNSS.setI2COutput(COM_TYPE_UBX);
    } else {
        Serial.println("Failed to initialize GPS Module");
    }

    // myGNSS.resetIMUalignment();
    // delay(1000);
    // calibrateGNNS();

    // Initialize OBD-II Adapter
    if (obd.initialize()) {
        Serial.println("OBD-II Adapter Initialized");
    } else {
        Serial.println("Failed to initialize OBD-II Adapter");
    }

    lastTime = millis();
}


void loop() {
    server.handleClient();

    bool journyActive = false;
    int rpm = 0, speed = 0, throttle = 0;
    float maf = 0.0, mpg = 0.0;

    unsigned long currentTime = millis();
    float deltaTime = (currentTime - lastTime) / 1000.0; // Convert to seconds

    // OBD-II Data Retrieval
    if (obd.readRPM(rpm)) { Serial.print("\nRPM: "); Serial.print(rpm); }
    if (rpm > 0) journyActive = true;
    if (obd.readSpeed(speed)) { Serial.print(" Speed (MPH): "); Serial.print(speed * 0.621371); }
    if (obd.readMAF(maf)) { Serial.print(" MAF (g/sec): "); Serial.print(maf);  }
    if (obd.readThrottle(throttle)) { Serial.print(" Throttle (%): "); Serial.print(throttle); }

    // Fuel Efficiency Calculation
    mpg = obd.calculateInstantMPG(speed, maf);
    totalSpeedTimeProduct += (speed * 0.621371) * deltaTime; // Speed in MPH over time
    totalFuelTimeProduct += (maf * 0.0805) * deltaTime;      // Fuel consumed over time

    float avgMPG = obd.calculateAverageMPG(totalSpeedTimeProduct, totalFuelTimeProduct);

    Serial.print(" Instant MPG: "); Serial.print(mpg, 2); 
    Serial.print(", Avg MPG: "); Serial.print(avgMPG, 2);

    // GPS Data Retrieval
    byte SIV = myGNSS.getSIV();
    double latitude = myGNSS.getLatitude() / 10000000.0;
    double longitude = myGNSS.getLongitude() / 10000000.0;
    uint8_t hour = myGNSS.getHour();
    uint8_t minute = myGNSS.getMinute();
    uint8_t second = myGNSS.getSecond();
    uint16_t day = myGNSS.getDay();
    uint16_t month = myGNSS.getMonth();
    uint16_t year = myGNSS.getYear();

    char time[10];  
    sprintf(time, "%02d:%02d:%02d", hour, minute, second);

    char date[12];  
    sprintf(date, "%04d-%02d-%02d", year, month, day);

    if (SIV > 0 && myGNSS.getFixType() > 2) {
        Serial.printf("\nTime: %s, Date: %s, Lat: %.7f, Long: %.7f, SIV: %d", time, date, latitude, longitude, SIV);
    } else {
        Serial.printf("\nTime: %s, Date: %s, Lat: %.7f, Long: %.7f, SIV: Dead Reckoning", time, date, latitude, longitude);
    }

    // IMU Data Retrieval
    int accelX = 0, accelY = 0, accelZ = 0;
    if (myGNSS.getEsfIns()) {
        accelX = myGNSS.packetUBXESFINS->data.xAccel;
        accelY = -myGNSS.packetUBXESFINS->data.yAccel;  // Invert Y-Axis for correct orientation
        accelZ = myGNSS.packetUBXESFINS->data.zAccel;
        Serial.printf("\nIMU Data: AccelX: %d, AccelY: %d, AccelZ: %d", accelX, accelY, accelZ);
    } else {
        Serial.println(F("Failed to retrieve IMU data."));
    }

    // Logging Data to SD Card
    if (LOG_TO_SD && journyActive) {
    if (firstLog) {
        // Create folder and file names
        sprintf(folderName, "%04d-%02d-%02d", myGNSS.getYear(), myGNSS.getMonth(), myGNSS.getDay());
        if (!SD.exists(folderName)) SD.mkdir(folderName);

        sprintf(fileName, "%s/%02d-%02d-%02d.json", folderName, myGNSS.getHour(), myGNSS.getMinute(), myGNSS.getSecond());
        logFile = SD.open(fileName, O_RDWR | O_CREAT | O_AT_END);

        if (logFile) {
            Serial.print("Log file created: ");
            Serial.println(fileName);
            firstLog = false;
        } else {
            Serial.println("Failed to create or open log file.");
            return; // Exit if file creation fails
        }
    }

        if (logFile) {
        StaticJsonDocument<1024> jsonDoc;
        jsonDoc["gps"]["time"] = time;
        jsonDoc["gps"]["latitude"] = latitude;
        jsonDoc["gps"]["longitude"] = longitude;
        // jsonDoc["gps"]["siv"] = SIV;
        jsonDoc["obd"]["rpm"] = rpm;
        jsonDoc["obd"]["speed"] = speed;
        jsonDoc["obd"]["maf"] = maf;
        jsonDoc["obd"]["instant_mpg"] = mpg;
        jsonDoc["obd"]["throttle"] = throttle;
        // jsonDoc["maf"] = maf;
        jsonDoc["obd"]["avg_mpg"] = totalFuelTimeProduct > 0 ? totalSpeedTimeProduct / totalFuelTimeProduct : 0.0;
        jsonDoc["imu"]["accel_x"] = accelX;
        jsonDoc["imu"]["accel_y"] = accelY;
        // jsonDoc["imu"]["accel_z"] = accelZ;

        // Debug: Print JSON to Serial
        // Serial.println("Logging JSON data:");
        // serializeJsonPretty(jsonDoc, Serial);
        // Serial.println();

        // Write JSON to file
        if (serializeJson(jsonDoc, logFile) == 0) {
            Serial.println("Failed to serialize JSON to SD card.");
        } else {
            logFile.println(); // Add newline for readability
            Serial.print("\nData logged to SD card.");
        }

        // Flush the file to ensure data is written
        logFile.flush();
    } else {
        Serial.println("Log file is not open. Attempting to reopen...");
        logFile = SD.open(fileName, O_RDWR | O_CREAT | O_AT_END);
    }
    
    lastTime = currentTime;
    Serial.println();
    delay(1000); // 1-second delay
    }
}

