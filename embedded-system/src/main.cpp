#include <Arduino.h>
#include <Wire.h>
#include <SPI.h>
#include <SparkFun_u-blox_GNSS_Arduino_Library.h>
#include "obd.hpp"
#include "server.hpp"
#include <SdFat.h>
#include <ArduinoJson.h>
#include <WiFi.h>
#include <WebServer.h>

#define LOG_TO_SD true    
#define SD_CS_PIN A0   

// WiFi Access Point 
const char* ssid = "MyESP32AP";
const char* password = "12345678";

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

     WiFi.softAP(ssid, password);
    Serial.println("AP IP address: " + WiFi.softAPIP().toString());
    
    // SD Card Initialization
    Wire1.setPins(SDA1, SCL1);
    
    Wire1.begin();

    if (!SD.begin(SD_CS_PIN)) {
        Serial.println("SD card initialization failed!");
    } else {
        Serial.println("SD card initialized successfully.");
    }
    
    setupServer();
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

