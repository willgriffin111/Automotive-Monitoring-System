#include <SparkfunOBD2UART.h>

class OBD : public COBD {
public:
    OBD();

    // Initialisation
    bool initialise();

    // Data Retrieval
    bool readRPM(int& rpm);
    bool readSpeed(int& speed_kph);
    bool readMAF(float& maf);
    bool readThrottle(int& throttle);

    // Fuel Efficiency Calculation
    float calculateInstantMPG(int speed_kph, float maf);
    float calculateAverageMPG(float totalSpeedTimeProduct, float totalFuelTimeProduct);

private:
    // Helper Functions
    bool sendPIDCommand(const char* pid, char* response, int bufsize);
    int parseHexValue(const char* response, int startIndex, int length);
};


extern OBD obd;  