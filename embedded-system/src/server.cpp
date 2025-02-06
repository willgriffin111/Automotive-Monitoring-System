#include "server.hpp"
#include <Arduino.h>
#include <Wire.h>
#include <SPI.h>
#include <ArduinoJson.h>

extern SdFat SD;  

WebServer server(80);  

void setupServer() {
    Serial.println("Setting up Web Server...");
    
    server.on("/", HTTP_GET, handleRoot);
    server.on("/days", HTTP_GET, handleDays);
    server.on("/drives", HTTP_GET, handleDrives);
    server.on("/drive", HTTP_GET, handleDrive);
    server.on("/live", HTTP_GET, handleLiveData);

    server.begin();
    Serial.println("Web server started.");
}


void handleRoot() {
    server.sendHeader("Access-Control-Allow-Origin", "*");
    server.send(200, "text/plain", "Connected");
}


void handleDays() {
    server.sendHeader("Access-Control-Allow-Origin", "*");
    Serial.println("Listing available days...");

    if (xSemaphoreTake(sdMutex, pdMS_TO_TICKS(1000)) == pdTRUE) {
        FsFile root = SD.open("/");
        if (!root) {
            Serial.println("Failed to open root directory");
            server.send(500, "text/plain", "Failed to open root directory");
            xSemaphoreGive(sdMutex); 
            return;
        }

        String json = "[";
        bool first = true;

        while (true) {
            FsFile entry = root.openNextFile();
            if (!entry) break;

            if (entry.isDir()) {
                char nameBuffer[32];
                entry.getName(nameBuffer, sizeof(nameBuffer));

                if (nameBuffer[0] == '.') {
                    entry.close();
                    continue;
                }

                if (!first) json += ",";
                json += "\"" + String(nameBuffer) + "\"";
                first = false;
            }
            entry.close();
        }
        json += "]";
        root.close();

        server.send(200, "application/json", json);

        xSemaphoreGive(sdMutex);
    } else {
        Serial.println("SD Mutex timeout in handleDays()");
        server.send(500, "text/plain", "SD card access timeout");
    }
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

    String path = "/" + day;

    if (xSemaphoreTake(sdMutex, pdMS_TO_TICKS(1000)) == pdTRUE) {
        FsFile dayDir = SD.open(path.c_str());
        if (!dayDir || !dayDir.isDir()) {
            Serial.println("Day folder not found");
            server.send(404, "text/plain", "Day folder not found");
            xSemaphoreGive(sdMutex);
            return;
        }

        String json = "[";
        bool first = true;

        while (true) {
            FsFile entry = dayDir.openNextFile();
            if (!entry) break;

            if (!entry.isDir()) {
                char nameBuffer[32];
                entry.getName(nameBuffer, sizeof(nameBuffer));

                if (nameBuffer[0] == '.') {
                    entry.close();
                    continue;
                }

                if (!first) json += ",";
                json += "\"" + String(nameBuffer) + "\"";
                first = false;
            }
            entry.close();
        }
        json += "]";
        dayDir.close();

        server.send(200, "application/json", json);
        xSemaphoreGive(sdMutex);
    } else {
        Serial.println("SD Mutex timeout in handleDrives()");
        server.send(500, "text/plain", "SD card access timeout");
    }
}

void handleDrive() {
    server.sendHeader("Access-Control-Allow-Origin", "*");
    if (!server.hasArg("day") || !server.hasArg("drive")) {
        server.send(400, "text/plain", "Missing 'day' or 'drive' parameter");
        return;
    }

    String day = server.arg("day");
    String driveFile = server.arg("drive");

    if (day.startsWith(".") || driveFile.startsWith(".")) {
        server.send(403, "text/plain", "Access forbidden");
        return;
    }

    Serial.printf("Fetching drive data for %s/%s\n", day.c_str(), driveFile.c_str());

    String path = "/" + day + "/" + driveFile;

    if (xSemaphoreTake(sdMutex, pdMS_TO_TICKS(1000)) == pdTRUE) {
        FsFile file = SD.open(path.c_str(), O_READ);
        if (!file) {
            server.send(404, "text/plain", "Drive file not found");
            xSemaphoreGive(sdMutex);
            return;
        }

        String content;
        while (file.available()) {
            content += (char)file.read();
        }
        file.close();

        server.send(200, "application/json", content);
        xSemaphoreGive(sdMutex);
    } else {
        Serial.println("SD Mutex timeout in handleDrive()");
        server.send(500, "text/plain", "SD card access timeout");
    }
}

void handleLiveData() {
    server.sendHeader("Access-Control-Allow-Origin", "*");
    Serial.println("Fetching latest drive data...");

    if (xSemaphoreTake(sdMutex, pdMS_TO_TICKS(1000))) {
        FsFile root = SD.open("/");
        if (!root) {
            server.send(500, "text/plain", "Failed to open root directory");
            xSemaphoreGive(sdMutex);
            return;
        }

        String latestDay = "";
        while (true) {
            FsFile entry = root.openNextFile();
            if (!entry) break;

            if (entry.isDir()) {
                char nameBuffer[32];
                entry.getName(nameBuffer, sizeof(nameBuffer));

                if (strlen(nameBuffer) == 10 && nameBuffer[4] == '-' && nameBuffer[7] == '-') {
                    if (latestDay == "" || String(nameBuffer) > latestDay) {
                        latestDay = nameBuffer;
                    }
                }
            }
            entry.close();
        }
        root.close();

        if (latestDay == "") {
            server.send(404, "text/plain", "No log data found");
            xSemaphoreGive(sdMutex);
            return;
        }

        String path = "/" + latestDay;
        FsFile dayDir = SD.open(path.c_str());
        if (!dayDir || !dayDir.isDir()) {
            server.send(500, "text/plain", "Could not access latest day folder");
            xSemaphoreGive(sdMutex);
            return;
        }

        String latestDrive = "";
        while (true) {
            FsFile entry = dayDir.openNextFile();
            if (!entry) break;

            if (!entry.isDir()) {
                char nameBuffer[32];
                entry.getName(nameBuffer, sizeof(nameBuffer));

                if (strlen(nameBuffer) >= 12 && nameBuffer[2] == '-' && nameBuffer[5] == '-' && strstr(nameBuffer, ".json")) {
                    if (latestDrive == "" || String(nameBuffer) > latestDrive) {
                        latestDrive = nameBuffer;
                    }
                }
            }
            entry.close();
        }
        dayDir.close();

        if (latestDrive == "") {
            server.send(404, "text/plain", "No latest drive data found");
            xSemaphoreGive(sdMutex);
            return;
        }

        String fullPath = "/" + latestDay + "/" + latestDrive;
        FsFile file = SD.open(fullPath.c_str(), O_READ);
        if (!file) {
            server.send(404, "text/plain", "Latest drive file not found");
            xSemaphoreGive(sdMutex);
            return;
        }

        String content;
        while (file.available()) {
            content += (char)file.read();
        }
        file.close();
        xSemaphoreGive(sdMutex);

        server.send(200, "application/json", content);
    } else {
        Serial.println("SD Mutex timeout in handleLiveData()");
        server.send(500, "text/plain", "SD busy, try again later");
    }
}

