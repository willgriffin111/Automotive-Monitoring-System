#include "server.hpp"
#include <Arduino.h>
#include <Wire.h>
#include <SPI.h>
#include <ArduinoJson.h>
#include <iostream>

// External SD filesystem instance and HTTP server on port 80
extern SdFat SD;
WebServer server(80);

// Function declaration
bool deleteRecursively(const char* path);

//-------------------------------------------------------------------------------
// Ensures a dummy JSON file exists under /test; creates it with sample data if not
//-------------------------------------------------------------------------------
void createDummyFileIfNotExists() {
    // Check if dummy file already exists
    if (!SD.exists("/test/dummy.json")) {
        Serial.println("Dummy file not found, creating it...");
        // Open (or create) file for writing
        FsFile file = SD.open("/test/dummy.json", O_WRITE | O_CREAT);
        if (file) {
            // Write data
            file.print(
                "{\"gps\":{\"time\":\"16:09:32\",\"latitude\":40.7590,\"longitude\":-73.9860},"
                "\"obd\":{\"rpm\":0,\"speed\":0,\"maf\":0.94,\"instant_mpg\":0,"
                "\"throttle\":14,\"avg_mpg\":0},\"imu\":{\"accel_x\":-19,\"accel_y\":-4}}\n"
                "{\"gps\":{\"time\":\"16:09:35\",\"latitude\":40.7590,\"longitude\":-73.9850},"
                "\"obd\":{\"rpm\":217,\"speed\":0,\"maf\":2.97,\"instant_mpg\":0,"
                "\"throttle\":14,\"avg_mpg\":0},\"imu\":{\"accel_x\":3,\"accel_y\":0}}\n"
                "{\"gps\":{\"time\":\"16:09:38\",\"latitude\":40.7580,\"longitude\":-73.9850},"
                "\"obd\":{\"rpm\":772,\"speed\":0,\"maf\":8.33,\"instant_mpg\":0,"
                "\"throttle\":14,\"avg_mpg\":0},\"imu\":{\"accel_x\":1,\"accel_y\":3}}\n"
                "{\"gps\":{\"time\":\"16:09:41\",\"latitude\":40.7580,\"longitude\":-73.9860},"
                "\"obd\":{\"rpm\":778,\"speed\":0,\"maf\":8.16,\"instant_mpg\":0,"
                "\"throttle\":14,\"avg_mpg\":0},\"imu\":{\"accel_x\":-1,\"accel_y\":0}}"
            );
            file.close();
            Serial.println("Dummy file created.");
        } else {
            Serial.println("Failed to create dummy file.");
        }
    } else {
        // Already present - no action needed
        Serial.println("Dummy file already exists.");
    }
}

//-------------------------------------------------------------------------------
// Handler for GET /
// Responds with a simple text to confirm connectivity
//-------------------------------------------------------------------------------
void handleRoot() {
    server.sendHeader("Access-Control-Allow-Origin", "*");
    server.send(200, "text/plain", "Connected");
}

//-------------------------------------------------------------------------------
// Handler for GET /days
// Lists top‑level directories (YYYY‑MM‑DD folders) as JSON array
//-------------------------------------------------------------------------------
void handleDays() {
    server.sendHeader("Access-Control-Allow-Origin", "*");
    Serial.println("Listing available days...");

    // Lock SD card for safe multi‑thread access 
    if (xSemaphoreTake(sdMutex, pdMS_TO_TICKS(1000)) == pdTRUE) {
        FsFile root = SD.open("/");
        if (!root) {
            // Couldn’t open root directory
            Serial.println("Failed to open root directory");
            server.send(500, "text/plain", "Failed to open root directory");
            xSemaphoreGive(sdMutex);
            return;
        }

        String json = "[";
        bool first = true;

        // Iterate each entry in root
        while (true) {
            FsFile entry = root.openNextFile();
            if (!entry) break;                     // No more entries

            if (entry.isDir()) {
                char name[32];
                entry.getName(name, sizeof(name));
                // Skip hidden dirs starting with '.'
                if (name[0] != '.') {
                    if (!first) json += ",";
                    json += "\"" + String(name) + "\"";
                    first = false;
                }
            }
            entry.close();
        }
        json += "]";
        root.close();

        // Send JSON list of day folders
        server.send(200, "application/json", json);
        xSemaphoreGive(sdMutex);
    } else {
        // Mutex lock timed out
        Serial.println("SD Mutex timeout in handleDays()");
        server.send(500, "text/plain", "SD card access timeout");
    }
}

//-------------------------------------------------------------------------------
// Handler for GET /drives?day=YYYY-MM-DD
// Lists all JSON files (drives) under the specified day folder
//-------------------------------------------------------------------------------
void handleDrives() {
    server.sendHeader("Access-Control-Allow-Origin", "*");
    if (!server.hasArg("day")) {
        // Missing required query parameter
        server.send(400, "text/plain", "Missing 'day' parameter");
        return;
    }

    String day = server.arg("day");
    Serial.print("Listing drives for day: ");
    Serial.println(day);

    String path = "/" + day;
    if (xSemaphoreTake(sdMutex, pdMS_TO_TICKS(1000)) == pdTRUE) {
        FsFile dir = SD.open(path.c_str());
        if (!dir || !dir.isDir()) {
            // Folder doesn’t exist
            Serial.println("Day folder not found");
            server.send(404, "text/plain", "Day folder not found");
            xSemaphoreGive(sdMutex);
            return;
        }

        String json = "[";
        bool first = true;
        while (true) {
            FsFile entry = dir.openNextFile();
            if (!entry) break;

            // Only include files (skip sub-directories)
            if (!entry.isDir()) {
                char name[32];
                entry.getName(name, sizeof(name));
                if (name[0] != '.') {
                    if (!first) json += ",";
                    json += "\"" + String(name) + "\"";
                    first = false;
                }
            }
            entry.close();
        }
        json += "]";
        dir.close();

        // Return JSON array of drive filenames
        server.send(200, "application/json", json);
        xSemaphoreGive(sdMutex);
    } else {
        Serial.println("SD Mutex timeout in handleDrives()");
        server.send(500, "text/plain", "SD card access timeout");
    }
}

//-------------------------------------------------------------------------------
// Handler for GET /drive?day=YYYY-MM-DD&drive=FILE.json
// Streams the contents of a specific drive file
//-------------------------------------------------------------------------------
void handleDrive() {
    server.sendHeader("Access-Control-Allow-Origin", "*");
    if (!server.hasArg("day") || !server.hasArg("drive")) {
        server.send(400, "text/plain", "Missing 'day' or 'drive' parameter");
        return;
    }
    String day   = server.arg("day");
    String drive = server.arg("drive");

    // Prevent path traversal
    if (day.startsWith(".") || drive.startsWith(".")) {
        server.send(403, "text/plain", "Access forbidden");
        return;
    }

    String path = "/" + day + "/" + drive;
    if (xSemaphoreTake(sdMutex, pdMS_TO_TICKS(1000)) == pdTRUE) {
        FsFile file = SD.open(path.c_str(), O_READ);
        if (!file) {
            server.send(404, "text/plain", "Drive file not found");
            xSemaphoreGive(sdMutex);
            return;
        }

        // Send headers, then stream file in chunks
        server.sendHeader("Content-Type", "application/json");
        server.setContentLength(file.size());
        server.send(200);

        const size_t bufSize = 512;
        uint8_t buf[bufSize];
        while (file.available()) {
            size_t n = file.read(buf, bufSize);
            server.sendContent((const char*)buf, n);
        }
        file.close();
        xSemaphoreGive(sdMutex);
    } else {
        Serial.println("SD Mutex timeout in handleDrive()");
        server.send(500, "text/plain", "SD card access timeout");
    }
}

//-------------------------------------------------------------------------------
// Handler for GET /live
// Finds the most recent date folder and drive file, then streams its contents
//-------------------------------------------------------------------------------
void handleLiveData() {
    server.sendHeader("Access-Control-Allow-Origin", "*");
    Serial.println("Fetching latest drive data...");

    if (xSemaphoreTake(sdMutex, pdMS_TO_TICKS(1000)) == pdTRUE) {
        // 1) Scan root for latest YYYY-MM-DD folder
        FsFile root = SD.open("/");
        String latestDay;
        while (true) {
            FsFile e = root.openNextFile();
            if (!e) break;
            if (e.isDir()) {
                char n[32];
                e.getName(n, sizeof(n));
                // Simple lexicographical compare for YYYY-MM-DD
                if (strlen(n)==10 && n[4]=='-' && n[7]=='-'
                    && (latestDay.isEmpty() || String(n) > latestDay)) {
                    latestDay = n;
                }
            }
            e.close();
        }
        root.close();

        if (latestDay.isEmpty()) {
            server.send(404, "text/plain", "No log data found");
            xSemaphoreGive(sdMutex);
            return;
        }

        // 2) Scan that folder for latest HH-MM-SS*.json file
        FsFile dayDir = SD.open(("/" + latestDay).c_str());
        String latestDrive;
        while (true) {
            FsFile e = dayDir.openNextFile();
            if (!e) break;
            if (!e.isDir()) {
                char n[32];
                e.getName(n, sizeof(n));
                // Check for time‑formatted name
                if (strlen(n)>=12 && n[2]=='-' && n[5]=='-' && strstr(n, ".json") &&
                    (latestDrive.isEmpty() || String(n) > latestDrive)) {
                    latestDrive = n;
                }
            }
            e.close();
        }
        dayDir.close();

        if (latestDrive.isEmpty()) {
            server.send(404, "text/plain", "No latest drive data found");
            xSemaphoreGive(sdMutex);
            return;
        }

        // 3) Stream that latest file
        FsFile file = SD.open(("/" + latestDay + "/" + latestDrive).c_str(), O_READ);
        if (!file) {
            server.send(404, "text/plain", "Latest drive file not found");
            xSemaphoreGive(sdMutex);
            return;
        }
        server.sendHeader("Content-Type", "application/json");
        server.setContentLength(file.size());
        server.send(200);
        const size_t bufSize = 512;
        uint8_t buf[bufSize];
        while (file.available()) {
            size_t n = file.read(buf, bufSize);
            server.sendContent((const char*)buf, n);
        }
        file.close();
        xSemaphoreGive(sdMutex);
    } else {
        Serial.println("SD Mutex timeout in handleLiveData()");
        server.send(500, "text/plain", "SD busy, try again later");
    }
}

//-------------------------------------------------------------------------------
// Handler for GET /sdinfo
// Reports SD card health and sizes, plus ESP32 uptime
//-------------------------------------------------------------------------------
void handleSDInfo() {
    server.sendHeader("Access-Control-Allow-Origin", "*");
    Serial.println("Fetching SD diagnostics...");

    if (xSemaphoreTake(sdMutex, pdMS_TO_TICKS(1000)) == pdTRUE) {
        String json = "{";
        FsVolume* vol = SD.vol();  // Get volume metadata
        if (!vol) {
            // No card detected
            json += "\"sd_status\":\"Not detected\"";
        } else {
            // Calculate sizes 
            uint32_t spc = vol->sectorsPerCluster();
            uint32_t cc  = vol->clusterCount();
            uint32_t fc  = vol->freeClusterCount();
            uint64_t total = (uint64_t)cc * spc * 512;
            uint64_t free  = (uint64_t)fc * spc * 512;
            uint64_t used  = total - free;

            json += "\"sd_status\":\"OK\","
                    "\"total_size\":" + String(total/1024.0/1024.0,2) + ","
                    "\"used_size\":"  + String(used /1024.0/1024.0,2) + ","
                    "\"free_size\":"  + String(free /1024.0/1024.0,2);
        }

        // Append ESP32 uptime in seconds
        json += ",\"esp32_uptime_sec\":" + String(millis()/1000) + "}";
        server.send(200, "application/json", json);
        xSemaphoreGive(sdMutex);
    } else {
        Serial.println("SD Mutex timeout in handleSDInfo()");
        server.send(500, "text/plain", "SD card access timeout");
    }
}

//-------------------------------------------------------------------------------
// Handler for OPTIONS /delete
// Enables CORS and allowed methods/headers for DELETE endpoint
//-------------------------------------------------------------------------------
void handleDeleteOptions() {
    server.sendHeader("Access-Control-Allow-Origin", "*");
    server.sendHeader("Access-Control-Allow-Methods", "GET, POST, DELETE, OPTIONS");
    server.sendHeader("Access-Control-Allow-Headers", "Content-Type");
    server.send(200);
}

//-------------------------------------------------------------------------------
// Handler for DELETE /delete?path=/some/path
// Safely deletes a file or directory tree under given path
//-------------------------------------------------------------------------------
void handleDelete() {
    server.sendHeader("Access-Control-Allow-Origin", "*");

    if (!server.hasArg("path")) {
        server.send(400, "text/plain", "Missing 'path' parameter");
        return;
    }
    String path = server.arg("path");

    // Validate path: must start with '/'
    if (!path.startsWith("/") || path.indexOf("..") != -1 ||
        (path.length()>1 && path.charAt(1)=='.')) {
        server.send(403, "text/plain", "Access forbidden");
        return;
    }

    Serial.printf("Deleting path: %s\n", path.c_str());
    if (xSemaphoreTake(sdMutex, pdMS_TO_TICKS(1000)) == pdTRUE) {
        bool ok = deleteRecursively(path.c_str());
        xSemaphoreGive(sdMutex);
        if (ok) {
            server.send(200, "text/plain", "Deleted successfully");
        } else {
            server.send(500, "text/plain", "Failed to delete");
        }
    } else {
        Serial.println("SD Mutex timeout in handleDelete()");
        server.send(500, "text/plain", "SD card access timeout");
    }
}

//-------------------------------------------------------------------------------
// Recursively deletes a file or directory at given path
// Returns true on success, false on any error
//-------------------------------------------------------------------------------
bool deleteRecursively(const char* path) {
    FsFile f = SD.open(path);
    if (!f) {
        Serial.printf("Path not found: %s\n", path);
        return false;
    }

    if (f.isDir()) {
        // Delete all entries within directory first
        while (true) {
            FsFile e = f.openNextFile();
            if (!e) break;
            char name[32];
            e.getName(name, sizeof(name));
            // Skip . and .. protections
            if (strcmp(name, ".") && strcmp(name, "..")) {
                String sub = String(path) + "/" + name;
                // Recurse or remove file
                if (e.isDir()) {
                    if (!deleteRecursively(sub.c_str())) {
                        e.close(); f.close();
                        return false;
                    }
                } else {
                    if (!SD.remove(sub.c_str())) {
                        Serial.printf("Failed to delete file: %s\n", sub.c_str());
                        e.close(); f.close();
                        return false;
                    }
                }
            }
            e.close();
        }
        f.close();
        // Now remove the empty directory
        if (!SD.rmdir(path)) {
            Serial.printf("Failed to remove directory: %s\n", path);
            return false;
        }
    } else {
        // Single file removal
        f.close();
        if (!SD.remove(path)) {
            Serial.printf("Failed to remove file: %s\n", path);
            return false;
        }
    }
    return true;
}

//-------------------------------------------------------------------------------
// Configures routes, initialises dummy file, and starts the server
//-------------------------------------------------------------------------------
void setupServer() {
    Serial.println("Setting up Web Server...");

    // Create test data 
    createDummyFileIfNotExists();

    // Bind URL paths to handler functions
    server.on("/", HTTP_GET, handleRoot);
    server.on("/days", HTTP_GET, handleDays);
    server.on("/drives", HTTP_GET, handleDrives);
    server.on("/drive", HTTP_GET, handleDrive);
    server.on("/live", HTTP_GET, handleLiveData);
    server.on("/sdinfo", HTTP_GET, handleSDInfo);
    server.on("/delete", HTTP_OPTIONS, handleDeleteOptions);
    server.on("/delete", HTTP_DELETE, handleDelete);

    // Boost Wi-Fi transmit power 
    WiFi.setTxPower(WIFI_POWER_19_5dBm);

    // Launch the server
    server.begin();
    Serial.println("Web server started.");
}
