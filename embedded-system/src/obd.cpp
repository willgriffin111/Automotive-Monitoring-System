#include "obd.hpp"
#include <Arduino.h>

// Constructor
OBD::OBD() {}


bool OBD::initialise() {
    Serial1.begin(9600);            
    return init(PROTO_AUTO);        
}


bool OBD::sendPIDCommand(const char* pid, char* response, int bufsize) {
    unsigned long startTime = millis();
    write(pid);  // Transmit the PID request

    // Loop until we receive data or timeout
    while (millis() - startTime < OBD_TIMEOUT_LONG) {
        int len = receive(response, bufsize, OBD_TIMEOUT_LONG);
        if (len > 0) {
            return true;  // Successful read
        }
    }
    // Timeout without data
    return false;
}


int OBD::parseHexValue(const char* response, int startIndex, int length) {
    // strtol will stop after 'length' hex digits or at NUL
    return strtol(&response[startIndex], NULL, 16);
}


bool OBD::readRPM(int& rpm) {
    char response[64];
    if (sendPIDCommand("010C", response, sizeof(response))) {
        // Find the "41 0C" header in the reply
        char* ptr = strstr(response, "41 0C");
        if (ptr) {
            int A = parseHexValue(ptr, 6, 2);  // Byte A at offset 6
            int B = parseHexValue(ptr, 9, 2);  // Byte B at offset 9
            rpm = ((A * 256) + B) / 4;          // Compute RPM
            return true;
        }
    }
    return false;
}


bool OBD::readSpeed(int& speed_kph) {
    char response[64];
    if (sendPIDCommand("010D", response, sizeof(response))) {
        char* ptr = strstr(response, "41 0D");
        if (ptr) {
            speed_kph = parseHexValue(ptr, 6, 2);  // Single byte value
            return true;
        }
    }
    return false;
}


bool OBD::readMAF(float& maf) {
    char response[64];
    if (sendPIDCommand("0110", response, sizeof(response))) {
        char* ptr = strstr(response, "41 10");
        if (ptr) {
            int A = parseHexValue(ptr, 6, 2);
            int B = parseHexValue(ptr, 9, 2);
            maf = ((A * 256) + B) / 100.0;  // Convert to g/s
            return true;
        }
    }
    return false;
}


bool OBD::readThrottle(int& throttle) {
    char response[64];
    if (sendPIDCommand("014A", response, sizeof(response))) {
        char* ptr = strstr(response, "41 4A");
        if (ptr && strlen(ptr) >= 8) {
            // Extract two hex digits after the header
            char hexVal[3] = { ptr[6], ptr[7], '\0' };
            int raw = strtol(hexVal, NULL, 16);
            throttle = (raw * 100) / 255;  // Scale to percent
            return true;
        }
    }
    Serial.println("Failed to parse absolute throttle data.");
    return false;
}



float OBD::calculateInstantMPG(int speed_kph, float maf) {
    if (speed_kph > 0 && maf > 0) {
        float mph = speed_kph * 0.621317;     // Convert to miles/hour
        float gph = maf * 0.0805;             // Convert g/s to gal/hr
        return mph / gph;
    }
    return 0.0;
}


float OBD::calculateAverageMPG(float totalSpeedTimeProduct, float totalFuelTimeProduct) {
    if (totalFuelTimeProduct > 0) {
        return totalSpeedTimeProduct / totalFuelTimeProduct;
    }
    return 0.0;
}
