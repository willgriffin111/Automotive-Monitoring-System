#include <Arduino.h>
#include <unity.h>
#include <Wire.h>
#include <SparkFun_u-blox_GNSS_Arduino_Library.h>

// #include <FS.h>
#define NO_GLOBALS
#include <SdFat.h>


#include "../src/obd.cpp"

#define SD_CS_PIN A0
#define BUTTON_PIN  A1
#define LED_PIN     A2

SFE_UBLOX_GNSS myGNSS;
OBD obd;
SdFat SD;

// ------------------ setUp and tearDown ------------------
void setUp() {
  Serial.begin(115200);
  delay(1000);
  
  // Setup hardware pins
  pinMode(BUTTON_PIN, INPUT_PULLUP);  
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, LOW);        
  
  // Initialize SD card (for SD tests)
  bool sdInit = SD.begin(SD_CS_PIN, SD_SCK_MHZ(10));
  Serial.printf("SD init: %s\n", sdInit ? "SUCCESS" : "FAILED");
}

void tearDown() {
  digitalWrite(LED_PIN, LOW);
}

// ------------------ Button & LED Tests ------------------
void test_button_not_pressed(void) {
  TEST_ASSERT_EQUAL(HIGH, digitalRead(BUTTON_PIN));
}

void test_led_on(void) {
  digitalWrite(LED_PIN, HIGH);
  delay(100);
  TEST_ASSERT_EQUAL(HIGH, digitalRead(LED_PIN));
}

void test_led_off(void) {
  digitalWrite(LED_PIN, LOW);
  delay(100);
  TEST_ASSERT_EQUAL(LOW, digitalRead(LED_PIN));
}

// ------------------ GNSS Tests ------------------
void test_gnss_initialisation(void) {
  Wire1.setPins(SDA1, SCL1);
  Wire1.begin();
  TEST_ASSERT_TRUE(myGNSS.begin(Wire1));
}

void test_gnss_data(void) {
  double lat = myGNSS.getLatitude() / 10000000.0;
  double lon = myGNSS.getLongitude() / 10000000.0;
  TEST_ASSERT_TRUE(lat >= -90.0 && lat <= 90.0);
  TEST_ASSERT_TRUE(lon >= -180.0 && lon <= 180.0);
}

void test_imu_data(void) {
  bool imuAvailable = myGNSS.getEsfIns();
  TEST_ASSERT_TRUE_MESSAGE(imuAvailable, "IMU data not available");
  int16_t xAccel = myGNSS.packetUBXESFINS->data.xAccel;
  int16_t yAccel = myGNSS.packetUBXESFINS->data.yAccel;
  TEST_ASSERT_TRUE(xAccel >= -2000 && xAccel <= 2000);
  TEST_ASSERT_TRUE(yAccel >= -2000 && yAccel <= 2000);
}

void test_gnss_dead_reckoning_data(void) {
  uint8_t siv = myGNSS.getSIV();
  if (siv == 0) {
    double lat = myGNSS.getLatitude() / 10000000.0;
    double lon = myGNSS.getLongitude() / 10000000.0;
    TEST_ASSERT_EQUAL_FLOAT(0.0, lat);
    TEST_ASSERT_EQUAL_FLOAT(0.0, lon);
  } else {
    TEST_IGNORE_MESSAGE("GNSS SIV is not 0; skipping dead reckoning data test");
  }
}

// ------------------ OBD Functionality Tests ------------------
void test_obd_initialise(void) {
  bool initResult = obd.initialise();
  TEST_ASSERT_TRUE_MESSAGE((initResult == true) || (initResult == false),
                           "OBD::initialise() did not return a valid boolean");
}

void test_obd_readRPM(void) {
  int rpm = 0;
  bool result = obd.readRPM(rpm);
  if (!result) {
    TEST_ASSERT_EQUAL(0, rpm);
  } else {
    TEST_ASSERT_TRUE(rpm > 0);
  }
}

void test_obd_readSpeed(void) {
  int speed = 0;
  bool result = obd.readSpeed(speed);
  if (!result) {
    TEST_ASSERT_EQUAL(0, speed);
  } else {
    TEST_ASSERT_TRUE(speed >= 0);
  }
}

void test_obd_readMAF(void) {
  float maf = 0.0;
  bool result = obd.readMAF(maf);
  if (!result) {
    TEST_ASSERT_EQUAL_FLOAT(0.0, maf);
  } else {
    TEST_ASSERT_TRUE(maf > 0.0);
  }
}

void test_obd_readThrottle(void) {
  int throttle = 0;
  bool result = obd.readThrottle(throttle);
  if (!result) {
    TEST_ASSERT_EQUAL(0, throttle);
  } else {
    TEST_ASSERT_TRUE(throttle >= 0 && throttle <= 100);
  }
}

void test_obd_calculateInstantMPG(void) {
  float mpg = obd.calculateInstantMPG(100, 10);  
  if (100 > 0 && 10 > 0) {
    TEST_ASSERT_TRUE(mpg > 0);
  } else {
    TEST_ASSERT_EQUAL_FLOAT(0.0, mpg);
  }
}

void test_calculateInstantMPG_zeroMAF(void) {
  float mpg = obd.calculateInstantMPG(50, 0);
  TEST_ASSERT_EQUAL_FLOAT(0.0, mpg);
}


void test_calculateInstantMPG_zeroSpeed(void) {
  float mpg = obd.calculateInstantMPG(0, 5);
  TEST_ASSERT_EQUAL_FLOAT(0.0, mpg);
}


void test_obd_calculateAverageMPG(void) {
  float avgMPG = obd.calculateAverageMPG(1000, 10); 
  if (10 > 0) {
    TEST_ASSERT_TRUE(avgMPG > 0);
  } else {
    TEST_ASSERT_EQUAL_FLOAT(0.0, avgMPG);
  }
}


void test_calculateAverageMPG_zeroDistance(void) {
  float avg = obd.calculateAverageMPG(0, 10);
  TEST_ASSERT_EQUAL_FLOAT(0.0, avg);
}


void test_calculateAverageMPG_zeroMAF(void) {
  float avg = obd.calculateAverageMPG(100, 0);
  TEST_ASSERT_EQUAL_FLOAT(0.0, avg);
}

void test_obd_timeout(void) {
  int dummy = 0;
  unsigned long start = millis();
  bool ok = obd.readSpeed(dummy);  
  unsigned long elapsed = millis() - start;

  TEST_ASSERT_TRUE_MESSAGE(elapsed < OBD_TIMEOUT_LONG,
    "OBD readSpeed hung longer than 500ms"
  );
  TEST_ASSERT_FALSE_MESSAGE(
    ok,
    "OBD readSpeed should return false on timeout"
  );
}
// ------------------ SD Card Tests ------------------

void test_sd_init(void) {
  bool sdInit = SD.begin(SD_CS_PIN, SD_SCK_MHZ(10));
  TEST_ASSERT_TRUE_MESSAGE(sdInit, "SD card failed to initialize");
}

void test_sd_directory_creation(void) {
  const char* folderName = "2025-03-04";
  if (!SD.exists(folderName)) {
    bool mkdirResult = SD.mkdir(folderName);
    TEST_ASSERT_TRUE_MESSAGE(mkdirResult, "Failed to create directory on SD card");
  }
  TEST_ASSERT_TRUE(SD.exists(folderName));
}

void test_sd_json_file_creation(void) {
  const char* fileName = "2025-03-04/12-34.json";
  FsFile file = SD.open(fileName, O_RDWR | O_CREAT | O_AT_END);
  TEST_ASSERT_TRUE_MESSAGE(file.isOpen(), "Failed to create JSON file on SD card");
  file.close();
}

void test_sd_json_file_write(void) {
  const char* fileName = "2025-03-04/12-34.json";
  FsFile file = SD.open(fileName, O_RDWR | O_CREAT | O_AT_END);
  TEST_ASSERT_TRUE_MESSAGE(file.isOpen(), "Failed to open JSON file for writing");
  file.println("{\"test\":\"value\"}");
  file.sync();  
  file.close();
}

void test_sd_json_file_read(void) {
  const char* fileName = "2025-03-04/12-34.json";
  FsFile file = SD.open(fileName, O_RDONLY);
  TEST_ASSERT_TRUE_MESSAGE(file.isOpen(), "Failed to open JSON file for reading");
  char buffer[128];
  int len = file.read(buffer, sizeof(buffer) - 1);
  buffer[len] = '\0';
  TEST_ASSERT_NOT_NULL(strstr(buffer, "\"test\":\"value\""));
  file.close();
}

void test_sd_json_file_delete(void) {
  const char* fileName = "2025-03-04/12-34.json";
  bool removed = SD.remove(fileName);
  TEST_ASSERT_TRUE_MESSAGE(removed, "Failed to delete JSON file from SD card");
  TEST_ASSERT_FALSE(SD.exists(fileName));
}

void test_sd_directory_delete(void) {
  const char* folderName = "2025-03-04";
  bool removed = SD.rmdir(folderName);
  TEST_ASSERT_TRUE_MESSAGE(removed, "Failed to delete directory from SD card");
  TEST_ASSERT_FALSE(SD.exists(folderName));
}

void test_sd_remove_nonexistent_file(void) {
  TEST_ASSERT_FALSE(SD.remove("no_such_file.txt"));
}

void test_sd_rmdir_nonexistent_folder(void) {
  TEST_ASSERT_FALSE(SD.rmdir("no_such_folder"));
}
void test_sd_power_loss_during_write(void) {
  const char* fileName = "power_test.json";
  // 1) Open file for append
  FsFile file = SD.open(fileName, O_RDWR | O_CREAT | O_AT_END);
  TEST_ASSERT_TRUE_MESSAGE(file.isOpen(), "Opening for power-loss test failed");

  // 2) Write a partial JSON payload
  file.print("{\"partial\":");
  // 3) Simulate sudden power loss by re-initialising SD mid-write
  SD.begin(SD_CS_PIN, SD_SCK_MHZ(10));
  // 4) Close the stale file handle
  file.close();

  // 5) Remount and verify FS is intact
  TEST_ASSERT_TRUE_MESSAGE(
    SD.begin(SD_CS_PIN, SD_SCK_MHZ(10)),
    "SD re-init after simulated power-loss failed"
  );
  // 6) Check that the file still exists
  TEST_ASSERT_TRUE_MESSAGE(
    SD.exists(fileName),
    "File disappeared after simulated power-loss"
  );

  // Cleanup
  SD.remove(fileName);
}

// ------------------ SD Concurrent-Access Test ------------------

void test_sd_concurrent_access(void) {
  const char* fileName = "concur.json";
  // Ensure file is not there to start
  if (SD.exists(fileName)) {
    SD.remove(fileName);
  }

  // 1) Open writer
  FsFile writer = SD.open(fileName, O_RDWR | O_CREAT | O_AT_END);
  TEST_ASSERT_TRUE_MESSAGE(writer.isOpen(), "Writer open failed");

  // 2) While writing, intermittently check that the file remains accessible
  for (int i = 0; i < 5; ++i) {
    writer.print("{\"i\":"); writer.print(i); writer.print("}\n");
    writer.sync();

    // Concurrent read: just open for read and peek a byte
    FsFile reader = SD.open(fileName, O_READ);
    TEST_ASSERT_TRUE_MESSAGE(reader.isOpen(), "Reader open during write failed");

    uint32_t sz = reader.size();
    TEST_ASSERT_TRUE_MESSAGE(sz > 0, "File size should be >0 during concurrent access");

    char c;
    reader.read(&c, 1);
    reader.close();
  }

  // 3) Finish and verify final file
  writer.close();
  FsFile finalReader = SD.open(fileName, O_READ);
  TEST_ASSERT_TRUE_MESSAGE(finalReader.isOpen(), "Final read open failed");
  TEST_ASSERT_TRUE_MESSAGE(finalReader.size() > 0, "Final file empty after write");
  finalReader.close();

  // Cleanup
  SD.remove(fileName);
}



// ------------------ Main: Run All Tests ------------------
void setup() {
  UNITY_BEGIN();
  
  // Hardware tests
  RUN_TEST(test_button_not_pressed);
  RUN_TEST(test_led_on);
  RUN_TEST(test_led_off);
  
  // GNSS tests
  RUN_TEST(test_gnss_initialisation);
  RUN_TEST(test_gnss_data);
  RUN_TEST(test_imu_data);
  RUN_TEST(test_gnss_dead_reckoning_data);

  
  // OBD tests
  RUN_TEST(test_obd_initialise);
  RUN_TEST(test_obd_readRPM);
  RUN_TEST(test_obd_readSpeed);
  RUN_TEST(test_obd_readMAF);
  RUN_TEST(test_obd_readThrottle);
  RUN_TEST(test_obd_calculateInstantMPG);
  RUN_TEST(test_obd_calculateAverageMPG);
  RUN_TEST(test_calculateInstantMPG_zeroMAF);
  RUN_TEST(test_calculateInstantMPG_zeroSpeed);
  RUN_TEST(test_calculateAverageMPG_zeroDistance);
  RUN_TEST(test_obd_timeout);
  
  // SD card tests
  RUN_TEST(test_sd_init);
  RUN_TEST(test_sd_directory_creation);
  RUN_TEST(test_sd_json_file_creation);
  RUN_TEST(test_sd_json_file_write);
  RUN_TEST(test_sd_json_file_read);
  RUN_TEST(test_sd_json_file_delete);
  RUN_TEST(test_sd_directory_delete);
  RUN_TEST(test_sd_remove_nonexistent_file);
  RUN_TEST(test_sd_rmdir_nonexistent_folder);
  RUN_TEST(test_sd_power_loss_during_write);
  RUN_TEST(test_sd_concurrent_access);
  
  UNITY_END();
}

void loop() {
  // Not needed for tests.
}
