#include <WebServer.h>
#include <SdFat.h>

extern WebServer server;
extern SemaphoreHandle_t sdMutex;

void setupServer();
void handleRoot();
void handleDays();
void handleDrives();
void handleDrive();
void handleLiveData();
void handleSDInfo();


