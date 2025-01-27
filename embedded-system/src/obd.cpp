#include "obd.hpp"
#include <Arduino.h>

// Constructor
OBD::OBD() {}

// Initialisation
bool OBD::initialize() {
    Serial1.begin(9600);  // Start communication on Serial1 at 9600 baud
    return init(PROTO_AUTO); // Use automatic protocol detection
}

// Send PID Command
bool OBD::sendPIDCommand(const char* pid, char* response, int bufsize) {
    unsigned long startTime = millis();
    write(pid);

    while (millis() - startTime < OBD_TIMEOUT_LONG) {
        if (receive(response, bufsize, OBD_TIMEOUT_LONG) > 0) {
            return true;
        }
    }

    return false;
}

// Parse Hex Value
int OBD::parseHexValue(const char* response, int startIndex, int length) {
    return strtol(&response[startIndex], NULL, 16);
}

// Read RPM
bool OBD::readRPM(int& rpm) {
    char response[64];
    if (sendPIDCommand("010C", response, sizeof(response))) {
        char* rpmPtr = strstr(response, "41 0C");
        if (rpmPtr != nullptr) {
            int A = parseHexValue(rpmPtr, 6, 2);
            int B = parseHexValue(rpmPtr, 9, 2);
            rpm = ((A * 256) + B) / 4;
            return true;
        }
    }
    return false;
}

// Read Speed (in KPH)
bool OBD::readSpeed(int& speed_kph) {
    char response[64];
    if (sendPIDCommand("010D", response, sizeof(response))) {
        char* speedPtr = strstr(response, "41 0D");
        if (speedPtr != nullptr) {
            speed_kph = parseHexValue(speedPtr, 6, 2);
            return true;
        }
    }
    return false;
}

// Read MAF (grams/sec)
bool OBD::readMAF(float& maf) {
    char response[64];
    if (sendPIDCommand("0110", response, sizeof(response))) {
        char* mafPtr = strstr(response, "41 10");
        if (mafPtr != nullptr) {
            int A = parseHexValue(mafPtr, 6, 2);
            int B = parseHexValue(mafPtr, 9, 2);
            maf = ((A * 256) + B) / 100.0;
            return true;
        }
    }
    return false;
}

// Read Throttle Position (%)
bool OBD::readThrottle(int& throttle) {
    char response[64];
    if (sendPIDCommand("014A", response, sizeof(response))) {
        // Serial.print("Raw Absolute Throttle Response: ");
        // Serial.println(response); // Debug raw response

        char* throttlePtr = strstr(response, "41 4A");
        if (throttlePtr != nullptr && strlen(throttlePtr) >= 5) {
            char hexValue[3] = {throttlePtr[6], throttlePtr[7], '\0'}; 
            int hexThrottle = strtol(hexValue, NULL, 16); // Convert hex to integer
            throttle = (hexThrottle * 100) / 255; // Calculate percentage

            // Serial.print("Parsed Hex Absolute Throttle: ");
            // Serial.println(hexThrottle);
            // Serial.print("Parsed Absolute Throttle (%): ");
            // Serial.println(throttle);
            return true;
        }
    }
    Serial.println("Failed to parse absolute throttle data.");
    return false;
}


float OBD::calculateInstantMPG(int speed_kph, float maf) {
    if (speed_kph > 0 && maf > 0) {
        float speed_mph = speed_kph * 0.621317;
        float gph = maf * 0.0805;
        float mpg = speed_mph / gph;
        return mpg;
    }
    return 0.0;
}

// Average MPG Calculation
float OBD::calculateAverageMPG(float totalSpeedTimeProduct, float totalFuelTimeProduct) {
    if (totalFuelTimeProduct > 0) {
        float average_mpg = totalSpeedTimeProduct / totalFuelTimeProduct;
        return average_mpg;
    }
    return 0.0;
}
