#include "server.hpp"
#include <Arduino.h>
#include <Wire.h>
#include <SPI.h>
#include <ArduinoJson.h>

extern SdFat SD;  

WebServer server(80);  

void handleRoot() {
    server.sendHeader("Access-Control-Allow-Origin", "*");
    server.send(200, "text/plain", "Connected");
}

void handleDays() {
    server.sendHeader("Access-Control-Allow-Origin", "*");
    Serial.println("Listing available days...");

    FsFile root = SD.open("/");
    if (!root) {
        Serial.println("Failed to open root directory");
        server.send(500, "text/plain", "Failed to open root directory");
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
    FsFile dayDir = SD.open(path.c_str());
    if (!dayDir || !dayDir.isDir()) {
        Serial.println("Day folder not found");
        server.send(404, "text/plain", "Day folder not found");
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
    FsFile file = SD.open(path.c_str(), O_READ);
    if (!file) {
        server.send(404, "text/plain", "Drive file not found");
        return;
    }

    String content;
    while (file.available()) {
        content += (char)file.read();
    }
    file.close();

    server.send(200, "application/json", content);
}

void handleLiveData() {
    server.sendHeader("Access-Control-Allow-Origin", "*");
    Serial.println("Fetching latest drive data...");

    FsFile root = SD.open("/");
    if (!root) {
        server.send(500, "text/plain", "Failed to open root directory");
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
        return;
    }

    String path = "/" + latestDay;
    FsFile dayDir = SD.open(path.c_str());
    if (!dayDir || !dayDir.isDir()) {
        server.send(500, "text/plain", "Could not access latest day folder");
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
        return;
    }

    String fullPath = "/" + latestDay + "/" + latestDrive;
    FsFile file = SD.open(fullPath.c_str(), O_READ);
    if (!file) {
        server.send(404, "text/plain", "Latest drive file not found");
        return;
    }

    String content;
    while (file.available()) {
        content += (char)file.read();
    }
    file.close();

    server.send(200, "application/json", content);
}

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
