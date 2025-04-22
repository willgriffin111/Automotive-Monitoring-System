#include "obd.hpp"
#include <Arduino.h>

// Constructor
OBD::OBD() {}

//-------------------------------------------------------------------------------
// initialise()
//   - Starts Serial1 at 9600 baud to communicate with the OBD adapter.
//   - Calls init() with PROTO_AUTO to let the library negotiate the correct
//     OBD protocol automatically.
// Returns:
//   - true if init() succeeds, false otherwise.
//-------------------------------------------------------------------------------
bool OBD::initialise() {
    Serial1.begin(9600);            
    return init(PROTO_AUTO);        
}

//-------------------------------------------------------------------------------
// sendPIDCommand(pid, response, bufsize)
//   - Sends an OBD-II PID request string (e.g., "010C").
//   - Waits up to OBD_TIMEOUT_LONG ms for a response.
//   - Fills the provided buffer with the reply 
// Parameters:
//   - pid:      String of the PID command.
//   - response: char array to receive the reply.
//   - bufsize:  Size of the response buffer.
// Returns:
//   - true if any bytes were received before timeout, false on timeout.
//-------------------------------------------------------------------------------
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

//-------------------------------------------------------------------------------
// parseHexValue(response, startIndex, length)
//   - Parses a substring of ASCII hex digits into an integer.
// Parameters:
//   - response:    Response string.
//   - startIndex:  Index in the string where hex digits begin.
//   - length:      Number of hex characters to parse.
// Returns:
//   - Integer value of the parsed hex substring.
//-------------------------------------------------------------------------------
int OBD::parseHexValue(const char* response, int startIndex, int length) {
    // strtol will stop after 'length' hex digits or at NUL
    return strtol(&response[startIndex], NULL, 16);
}

//-------------------------------------------------------------------------------
// readRPM(rpm)
//   - Queries PID 0x0C to get engine RPM.
//   - Response format: "41 0C AA BB", where RPM = ((AA*256) + BB) / 4.
// Parameters:
//   - rpm: Reference to an int to store the result.
// Returns:
//   - true on success (rpm set), false otherwise.
//-------------------------------------------------------------------------------
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

//-------------------------------------------------------------------------------
// readSpeed(speed_kph)
//   - Queries PID 0x0D to get vehicle speed in km/h.
//   - Response: "41 0D AA", where speed_kph = AA.
// Parameters:
//   - speed_kph: Reference to an int to store the speed.
// Returns:
//   - true on success, false otherwise.
//-------------------------------------------------------------------------------
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

//-------------------------------------------------------------------------------
// readMAF(maf)
//   - Queries PID 0x10 to get mass air flow sensor data (g/s).
//   - Response: "41 10 AA BB", where maf = ((AA*256) + BB) / 100.
// Parameters:
//   - maf: Reference to a float to store grams per second.
// Returns:
//   - true on success, false otherwise.
//-------------------------------------------------------------------------------
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

//-------------------------------------------------------------------------------
// readThrottle(throttle)
//   - Queries PID 0x4A for absolute throttle position (%).
//   - Response: "41 4A AA", where position = (AA * 100) / 255.
// Parameters:
//   - throttle: Reference to an int to store percentage [0–100].
// Returns:
//   - true on success, false otherwise.
//-------------------------------------------------------------------------------
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

//-------------------------------------------------------------------------------
// calculateInstantMPG(speed_kph, maf)
//   - Converts speed (km/h) to mph and MAF (g/s) to gallons per hour (gph).
//   - Computes instant MPG = mph / gph.
// Parameters:
//   - speed_kph: Vehicle speed in km/h
//   - maf:       Mass air flow in grams/sec
// Returns:
//   - Instant MPG as a float, or 0.0 if inputs invalid.
//-------------------------------------------------------------------------------
float OBD::calculateInstantMPG(int speed_kph, float maf) {
    if (speed_kph > 0 && maf > 0) {
        float mph = speed_kph * 0.621317;     // Convert to miles/hour
        float gph = maf * 0.0805;             // Convert g/s to gal/hr
        return mph / gph;
    }
    return 0.0;
}

//-------------------------------------------------------------------------------
// calculateAverageMPG(totalSpeedTimeProduct, totalFuelTimeProduct)
//   - Computes average MPG over a trip given the sum of (speed * delta_t)
//     and sum of (fuel_flow * delta_t).
// Parameters:
//   - totalSpeedTimeProduct: Sum of (speed * time interval)
//   - totalFuelTimeProduct:  Sum of (fuel flow * time interval)
// Returns:
//   - Average MPG, or 0.0 if no fuel consumed.
//-------------------------------------------------------------------------------
float OBD::calculateAverageMPG(float totalSpeedTimeProduct, float totalFuelTimeProduct) {
    if (totalFuelTimeProduct > 0) {
        return totalSpeedTimeProduct / totalFuelTimeProduct;
    }
    return 0.0;
}
