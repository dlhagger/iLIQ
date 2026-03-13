// =======================
// Configuration & Includes
// =======================

#define DEBUG_MODE true

#include <Wire.h>
#include <SPI.h>
#include <SdFat.h>
#include <Adafruit_SH110X.h>
#include <Adafruit_MPR121.h>
#include <RTClib.h>
#include <string.h>
#include <pico/time.h>

#include "ButtonDebouncer.h"
#include "SessionLogger.h"

// =======================
// Pin Definitions
// =======================

#define SD_CS_PIN 23
#define SD_DETECT_PIN 16
#define BUTTON_A 9
#define BUTTON_B 6
#define BUTTON_C 5
#define TTL_OUT_PIN 26  // RP2040 GPIO26 (A0)

static const uint8_t MPR121_I2C_ADDR = 0x5A;

// =======================
// State Management
// =======================

enum RecorderState {
  IDLE,
  RECORDING,
  DEGRADED
};

RecorderState currentState = IDLE;
bool cardPresent = false;
bool rtcAvailable = false;
bool mprAvailable = false;
bool displayEnabled = true;
bool debugSerialReady = false;
uint64_t noCardMessageUntil = 0;

// =======================
// Timing Variables
// =======================

uint64_t lastCardCheck = 0;
uint64_t lastDisplayUpdate = 0;
uint64_t recordStartTime = 0;
uint64_t lastPeripheralRetry = 0;
uint64_t lastMprHealthCheck = 0;
uint64_t lastMprSelfHealAttempt = 0;

const unsigned long CARD_CHECK_INTERVAL_MS = 500;
const unsigned long DISPLAY_IDLE_INTERVAL_MS = 500;
const unsigned long DISPLAY_RECORDING_INTERVAL_MS = 1000;
const unsigned long BUFFER_FLUSH_TIMEOUT_MS = 10000;
const unsigned long PERIPHERAL_RETRY_INTERVAL_MS = 5000;
const uint64_t MPR_HEALTH_CHECK_INTERVAL_MS = 1000ULL;
const uint64_t MPR_SELF_HEAL_COOLDOWN_MS = 30ULL * 1000ULL;

// =======================
// Hardware Interfaces
// =======================

SdFat SD;
SdSpiConfig sdConfig(SD_CS_PIN, DEDICATED_SPI, SD_SCK_MHZ(16), &SPI1);
Adafruit_SH1107 display = Adafruit_SH1107(64, 128, &Wire);
Adafruit_MPR121 cap = Adafruit_MPR121();
RTC_DS3231 rtc;

SessionLogger sessionLogger(SD);
ButtonDebouncer buttonA(BUTTON_A);
ButtonDebouncer buttonB(BUTTON_B);
ButtonDebouncer buttonC(BUTTON_C);

// =======================
// Touch Tracking
// =======================

uint16_t lastTouched = 0;
uint16_t touchCounts[12] = { 0 };

// =======================
// Sensor Parameters
// =======================

int CUSTOM_TOUCH_THRESHOLD = 4;
int CUSTOM_RELEASE_THRESHOLD = 2;
int CUSTOM_DEBOUNCE = 0;

#ifndef _BV
#define _BV(bit) (1 << (bit))
#endif

// =======================
// Utility Functions
// =======================

void updateDisplay();
bool getRtcTimestampFields(uint16_t* year, uint8_t* month, uint8_t* day,
                           uint8_t* hour, uint8_t* minute, uint8_t* second);

void debugPrint(const String& msg) {
  if (DEBUG_MODE && debugSerialReady) {
    Serial.println(msg);
  }
}

static inline uint64_t millis64() {
  return time_us_64() / 1000ULL;
}

const char* stateName(RecorderState state) {
  switch (state) {
    case IDLE:
      return "IDLE";
    case RECORDING:
      return "RECORDING";
    case DEGRADED:
      return "DEGRADED";
    default:
      return "UNKNOWN";
  }
}

void setState(RecorderState nextState, const char* reason) {
  if (nextState == currentState) {
    return;
  }

  debugPrint("State: " + String(stateName(currentState)) + " -> " + String(stateName(nextState)) + " (" + reason + ")");
  currentState = nextState;
  lastDisplayUpdate = 0;

  // Safety: TTL output should idle LOW whenever we're not actively recording.
  if (nextState != RECORDING) {
    digitalWrite(TTL_OUT_PIN, LOW);
  }

  if (displayEnabled) {
    updateDisplay();
  }
}

void buildWallTime(char* out, size_t outSize) {
  if (!rtcAvailable) {
    strncpy(out, "RTC_UNAVAILABLE", outSize - 1);
    out[outSize - 1] = '\0';
    return;
  }

  DateTime now = rtc.now();
  snprintf(out, outSize, "%04d-%02d-%02dT%02d:%02d:%02d",
           now.year(), now.month(), now.day(),
           now.hour(), now.minute(), now.second());
}

void buildSessionFilename(char* out, size_t outSize) {
  uint64_t nowMs = millis64();

  if (rtcAvailable) {
    DateTime now = rtc.now();
    uint16_t msPart = static_cast<uint16_t>(nowMs % 1000ULL);
    snprintf(out, outSize, "/log_%04d%02d%02d_%02d%02d%02d_%03u.csv",
             now.year(), now.month(), now.day(),
             now.hour(), now.minute(), now.second(),
             static_cast<unsigned int>(msPart));

    if (cardPresent && SD.exists(out)) {
      for (uint16_t suffix = 1; suffix <= 999; suffix++) {
        snprintf(out, outSize, "/log_%04d%02d%02d_%02d%02d%02d_%03u_%03u.csv",
                 now.year(), now.month(), now.day(),
                 now.hour(), now.minute(), now.second(),
                 static_cast<unsigned int>(msPart),
                 static_cast<unsigned int>(suffix));
        if (!SD.exists(out)) {
          break;
        }
      }
    }
  } else {
    snprintf(out, outSize, "/log_ms%llu.csv",
             static_cast<unsigned long long>(nowMs));

    if (cardPresent && SD.exists(out)) {
      for (uint16_t suffix = 1; suffix <= 999; suffix++) {
        snprintf(out, outSize, "/log_ms%llu_%03u.csv",
                 static_cast<unsigned long long>(nowMs),
                 static_cast<unsigned int>(suffix));
        if (!SD.exists(out)) {
          break;
        }
      }
    }
  }
}

void clearTouchCounts() {
  for (uint8_t i = 0; i < 12; i++) {
    touchCounts[i] = 0;
  }
}

bool getRtcTimestampFields(uint16_t* year, uint8_t* month, uint8_t* day,
                           uint8_t* hour, uint8_t* minute, uint8_t* second) {
  if (!rtcAvailable) {
    return false;
  }

  DateTime now = rtc.now();
  *year = now.year();
  *month = now.month();
  *day = now.day();
  *hour = now.hour();
  *minute = now.minute();
  *second = now.second();
  return true;
}

// =======================
// Peripheral Initialization
// =======================

void dumpMPR121Regs() {
  if (!DEBUG_MODE) {
    return;
  }

  Serial.println("=== MPR121 Config Dump ===");

  Serial.print("CDC: ");
  for (int i = 0; i < 12; i++) {
    uint8_t cdc = cap.readRegister8(0x5F + i);
    Serial.print(cdc < 10 ? " " : "");
    Serial.print(cdc);
    Serial.print(" ");
  }
  Serial.println();

  Serial.print("CDT: ");
  for (int i = 0; i < 6; i++) {
    uint8_t reg = cap.readRegister8(0x6C + i);
    uint8_t cdtx = reg & 0b111;
    uint8_t cdty = (reg >> 4) & 0b111;
    Serial.print(cdtx);
    Serial.print(" ");
    Serial.print(cdty);
    Serial.print(" ");
  }
  Serial.println();

  Serial.println("==========================");
}

bool initializeRTC() {
  if (!rtc.begin()) {
    debugPrint("RTC not found; running without wall clock.");
    return false;
  }

  if (rtc.lostPower()) {
    debugPrint("RTC lost power, setting time from compile timestamp.");
    rtc.adjust(DateTime(F(__DATE__), F(__TIME__)));
  }

  if (DEBUG_MODE) {
    DateTime now = rtc.now();
    Serial.print("RTC ready: ");
    Serial.print(now.timestamp(DateTime::TIMESTAMP_DATE));
    Serial.print(" ");
    Serial.println(now.timestamp(DateTime::TIMESTAMP_TIME));
  }

  return true;
}

bool initializeMPR121() {
  // Hot-plug recovery can leave I2C in a bad state; re-begin bus and retry once.
  Wire.begin();
  delay(2);

  if (!cap.begin(MPR121_I2C_ADDR)) {
    delay(10);
    Wire.begin();
    delay(2);
    if (!cap.begin(MPR121_I2C_ADDR)) {
      debugPrint("MPR121 not found; recording unavailable until sensor recovers.");
      return false;
    }
  }

  cap.setThresholds(CUSTOM_TOUCH_THRESHOLD, CUSTOM_RELEASE_THRESHOLD);
  cap.writeRegister(MPR121_DEBOUNCE, (CUSTOM_DEBOUNCE << 4) | CUSTOM_DEBOUNCE);
  cap.setAutoconfig(true);

  debugPrint("MPR121 ready");
  dumpMPR121Regs();
  return true;
}

void attemptPeripheralRecovery(uint64_t nowMs) {
  if (nowMs - lastPeripheralRetry < PERIPHERAL_RETRY_INTERVAL_MS) {
    return;
  }

  lastPeripheralRetry = nowMs;

  if (!mprAvailable) {
    debugPrint("Retrying MPR121 init...");
    mprAvailable = initializeMPR121();
    if (mprAvailable) {
      lastTouched = 0;
      if (currentState == DEGRADED) {
        setState(IDLE, "MPR121 recovered");
      }
    }
  }

  if (!rtcAvailable) {
    debugPrint("Retrying RTC init...");
    rtcAvailable = initializeRTC();
  }
}

// =======================
// SD Card Utilities
// =======================

bool cardInserted() {
  return digitalRead(SD_DETECT_PIN) == HIGH;
}

bool initSDCard() {
  if (!SD.begin(sdConfig)) {
    debugPrint("SD init failed.");
    return false;
  }

  debugPrint("SD init successful.");
  return true;
}

void handleCardStatus(uint64_t nowMs) {
  bool nowInserted = cardInserted();

  if (nowInserted && !cardPresent) {
    debugPrint("SD card inserted.");
    cardPresent = initSDCard();
    if (cardPresent && sessionLogger.hasBufferedData()) {
      if (sessionLogger.flush()) {
        debugPrint("Recovered buffered log events after SD reinsert.");
        if (currentState != RECORDING) {
          if (!sessionLogger.closeSession()) {
            debugPrint("Recovered writes but failed to finalize session close.");
          }
        }
      } else {
        debugPrint("Pending buffered events still waiting for SD write.");
      }
    }
    lastDisplayUpdate = 0;
  }

  if (!nowInserted && cardPresent) {
    debugPrint("SD card removed.");
    sessionLogger.handleCardRemoval();
    cardPresent = false;
    displayEnabled = true;
    noCardMessageUntil = nowMs + 5000;
    lastDisplayUpdate = 0;

    if (currentState == RECORDING) {
      // Continue recording in RAM; writes will retry on card reinsertion.
      debugPrint("Continuing recording with SD absent; buffering events in RAM.");
    } else {
      lastTouched = 0;
    }
  }
}

// =======================
// Logging Utilities
// =======================

void logSystemEvent(const char* eventName) {
  if (!sessionLogger.isSessionOpen()) {
    return;
  }

  uint64_t nowMs64 = millis64();
  char wallTime[24];
  buildWallTime(wallTime, sizeof(wallTime));
  sessionLogger.appendEvent(nowMs64, wallTime, "E-", eventName);
}

void logTouchEvent(uint8_t electrode, const char* eventType) {
  if (!sessionLogger.isSessionOpen()) {
    return;
  }

  uint64_t nowMs64 = millis64();
  char electrodeLabel[6];

  snprintf(electrodeLabel, sizeof(electrodeLabel), "E%u", electrode);

  // Keep hot-path logging lean: millisecond timestamp only for touch edges.
  sessionLogger.appendEvent(nowMs64, "", electrodeLabel, eventType);
}

// =======================
// State Transitions
// =======================

void startRecordingSession() {
  if (currentState != IDLE) {
    return;
  }

  if (!cardPresent) {
    debugPrint("Cannot start recording: no SD card.");
    noCardMessageUntil = millis64() + 5000;
    lastDisplayUpdate = 0;
    return;
  }

  if (sessionLogger.hasBufferedData()) {
    if (!sessionLogger.flush()) {
      debugPrint("Cannot start: pending log buffer not yet written to SD.");
      noCardMessageUntil = millis64() + 5000;
      lastDisplayUpdate = 0;
      return;
    }
  }

  if (sessionLogger.isSessionOpen()) {
    if (!sessionLogger.closeSession()) {
      debugPrint("Cannot start: failed to finalize previous session writes.");
      noCardMessageUntil = millis64() + 5000;
      lastDisplayUpdate = 0;
      return;
    }
  }

  // Re-run MPR121 setup/autoconfig at every session start.
  mprAvailable = initializeMPR121();
  if (!mprAvailable) {
    debugPrint("Cannot start recording: MPR121 unavailable after session autoconfig.");
    setState(DEGRADED, "MPR121 unavailable on start request");
    return;
  }

  char filename[40];
  buildSessionFilename(filename, sizeof(filename));

  if (!sessionLogger.startSession(filename)) {
    debugPrint("Failed to open session log file.");
    return;
  }

  clearTouchCounts();
  lastTouched = 0;
  digitalWrite(TTL_OUT_PIN, LOW);
  recordStartTime = millis64();
  displayEnabled = true;

  logSystemEvent("SESSION_START");
  setState(RECORDING, "Start button");
}

void stopRecordingSession(const char* reason, bool logStopEvent) {
  if (currentState != RECORDING) {
    return;
  }

  if (logStopEvent && cardPresent && sessionLogger.isSessionOpen()) {
    logSystemEvent("SESSION_STOP");
  }

  if (!sessionLogger.flush()) {
    debugPrint("Warning: flush failed while stopping session.");
  }

  if (!sessionLogger.closeSession()) {
    debugPrint("Session close deferred; pending data will retry on SD reinsertion.");
  }
  displayEnabled = true;
  setState(mprAvailable ? IDLE : DEGRADED, reason);
}

// Considered "faulted" if device stopped (ECR=0) or OOR flags set.
static inline bool mpr121Faulted() {
  uint8_t ecr = cap.readRegister8(MPR121_ECR);
  if (ecr == 0x00) {
    return true;  // In STOP mode.
  }

  // OOR (Out-of-Range) status registers: 0x02 (L), 0x03 (H)
  uint8_t oorL = cap.readRegister8(0x02);
  uint8_t oorH = cap.readRegister8(0x03);
  if ((oorL | oorH) != 0) {
    return true;  // Any electrode out-of-range.
  }

  return false;
}

void runMprSelfHeal(uint64_t nowMs, const char* reason) {
  if ((nowMs - lastMprSelfHealAttempt) < MPR_SELF_HEAL_COOLDOWN_MS) {
    return;
  }

  lastMprSelfHealAttempt = nowMs;
  bool wasRecording = (currentState == RECORDING);

  debugPrint(String("Running MPR121 self-heal: ") + reason);
  mprAvailable = initializeMPR121();
  if (!mprAvailable) {
    if (wasRecording) {
      stopRecordingSession("MPR121 self-heal failed", true);
    } else {
      setState(DEGRADED, "MPR121 self-heal failed");
    }
    return;
  }

  // Resync transition baseline after reinit to avoid spurious edge logs.
  lastTouched = cap.touched();

  if (wasRecording) {
    logSystemEvent("MPR121_SELF_HEAL");
  }
}

void maybeRunMprSelfHeal(uint64_t nowMs) {
  if (!mprAvailable || currentState != RECORDING) {
    return;
  }

  if ((nowMs - lastMprHealthCheck) < MPR_HEALTH_CHECK_INTERVAL_MS) {
    return;
  }
  lastMprHealthCheck = nowMs;

  if (mpr121Faulted()) {
    runMprSelfHeal(nowMs, "FAULT_REGISTERS");
  }
}

// =======================
// Input Handling
// =======================

void handleButtons(unsigned long nowMs) {
  if (buttonA.pollPressedEdge(nowMs)) {
    startRecordingSession();
  }

  if (buttonC.pollPressedEdge(nowMs) && currentState == RECORDING) {
    stopRecordingSession("Stop button", true);
  }

  if (buttonB.pollPressedEdge(nowMs) && currentState == RECORDING) {
    displayEnabled = !displayEnabled;
    if (!displayEnabled) {
      display.clearDisplay();
      display.display();
    } else {
      lastDisplayUpdate = 0;
      updateDisplay();
    }
  }
}

void pollTouchInput() {
  if (!mprAvailable) {
    digitalWrite(TTL_OUT_PIN, LOW);
    return;
  }

  uint16_t currentTouched = cap.touched();
  bool pad0Touched = (currentTouched & _BV(0)) != 0;
  digitalWrite(TTL_OUT_PIN, pad0Touched ? HIGH : LOW);

  if (currentState != RECORDING) {
    lastTouched = currentTouched;
    return;
  }

  for (uint8_t i = 0; i < 12; i++) {
    bool wasTouched = lastTouched & _BV(i);
    bool isTouched = currentTouched & _BV(i);

    if (!wasTouched && isTouched) {
      touchCounts[i]++;
      logTouchEvent(i, "START");

      if (DEBUG_MODE) {
        Serial.print("Touch START electrode ");
        Serial.print(i);
        Serial.print(" (total: ");
        Serial.print(touchCounts[i]);
        Serial.println(")");
      }
    }

    if (wasTouched && !isTouched) {
      logTouchEvent(i, "STOP");

      if (DEBUG_MODE) {
        Serial.print("Touch STOP electrode ");
        Serial.println(i);
      }
    }
  }

  lastTouched = currentTouched;
}

// =======================
// Display Logic
// =======================

void updateDisplay() {
  uint64_t nowMs = millis64();
  display.clearDisplay();
  display.setCursor(0, 0);

  if (currentState != RECORDING && nowMs < noCardMessageUntil) {
    display.println("No SD card!");
    display.println("Insert card");
    display.display();
    return;
  }

  if (currentState == RECORDING) {
    uint64_t elapsed = (nowMs - recordStartTime) / 1000ULL;
    unsigned int hours = static_cast<unsigned int>(elapsed / 3600ULL);
    unsigned int minutes = static_cast<unsigned int>((elapsed % 3600ULL) / 60ULL);
    unsigned int seconds = static_cast<unsigned int>(elapsed % 60ULL);

    if (cardPresent) {
      display.print("REC ");
    } else {
      display.print("REC(NO SD) ");
    }
    if (hours < 10) display.print('0');
    display.print(hours);
    display.print(':');
    if (minutes < 10) display.print('0');
    display.print(minutes);
    display.print(':');
    if (seconds < 10) display.print('0');
    display.println(seconds);

    // Full per-pad labels in three columns: [0-3] [4-7] [8-11].
    for (uint8_t row = 0; row < 4; row++) {
      uint8_t y = 16 + (row * 10);
      for (uint8_t col = 0; col < 3; col++) {
        uint8_t idx = row + (col * 4);
        uint8_t x = col * 42;
        display.setCursor(x, y);
        display.print(idx);
        display.print(':');
        display.print(touchCounts[idx]);
      }
    }
  } else if (currentState == DEGRADED) {
    display.println("DEGRADED MODE");
    display.print("SD Card: ");
    display.println(cardPresent ? "Yes" : "No");
    display.print("RTC: ");
    display.println(rtcAvailable ? "OK" : "MISSING");
    display.print("MPR121: ");
    display.println(mprAvailable ? "OK" : "MISSING");
    display.println();
    display.println("Auto-retrying hw");
    display.println("Start waits for OK");
  } else {
    if (rtcAvailable) {
      DateTime now = rtc.now();
      char dateTimeBuf[20];
      snprintf(dateTimeBuf, sizeof(dateTimeBuf), "%04d-%02d-%02d %02d:%02d:%02d",
               now.year(), now.month(), now.day(),
               now.hour(), now.minute(), now.second());
      display.println(dateTimeBuf);
    } else {
      display.println("RTC_MISSING");
    }

    display.print("SD Card: ");
    display.println(cardPresent ? "Yes" : "No");
    display.print("RTC: ");
    display.println(rtcAvailable ? "OK" : "MISSING");
    display.print("MPR121: ");
    display.println(mprAvailable ? "OK" : "MISSING");
    display.println();
    display.println("Press A: Start Rec");
    display.println("Press B: Toggle Disp");
    display.println("Press C: Stop Rec");
  }

  display.display();
}

// =======================
// Setup
// =======================

void setup() {
  pinMode(TTL_OUT_PIN, OUTPUT);
  digitalWrite(TTL_OUT_PIN, LOW);

  delay(100);

  display.begin(0x3C, true);
  display.setRotation(1);
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SH110X_WHITE);
  display.setCursor(0, 0);
  display.println("Startup...");
  display.display();

  if (DEBUG_MODE) {
    Serial.begin(115200);

    display.clearDisplay();
    display.setCursor(0, 0);
    display.println("DEBUG MODE ON");
    display.println("Waiting serial...");
    display.println();
    display.println("Connect serial");
    display.println("or flash with");
    display.println("DEBUG_MODE false");
    display.display();

    while (!Serial) {
      yield();
      delay(10);
    }
    debugSerialReady = true;
  } else {
    debugSerialReady = false;
  }

  pinMode(SD_DETECT_PIN, INPUT_PULLUP);
  buttonA.begin();
  buttonB.begin();
  buttonC.begin();
  sessionLogger.setTimestampProvider(getRtcTimestampFields);

  mprAvailable = initializeMPR121();
  rtcAvailable = initializeRTC();

  if (cardInserted()) {
    cardPresent = initSDCard();
  }

  setState(mprAvailable ? IDLE : DEGRADED, "Setup complete");
  updateDisplay();
}

// =======================
// Main Loop
// =======================

void loop() {
  uint64_t nowMs64 = millis64();
  unsigned long debounceNow = millis();

  if (nowMs64 - lastCardCheck >= CARD_CHECK_INTERVAL_MS) {
    lastCardCheck = nowMs64;
    handleCardStatus(nowMs64);
  }

  attemptPeripheralRecovery(nowMs64);
  maybeRunMprSelfHeal(nowMs64);
  handleButtons(debounceNow);
  pollTouchInput();

  if (cardPresent) {
    sessionLogger.flushIfIdle(nowMs64, BUFFER_FLUSH_TIMEOUT_MS);
  }

  unsigned long displayInterval = (currentState == RECORDING)
                                    ? DISPLAY_RECORDING_INTERVAL_MS
                                    : DISPLAY_IDLE_INTERVAL_MS;

  if (nowMs64 - lastDisplayUpdate >= displayInterval) {
    lastDisplayUpdate = nowMs64;
    if (displayEnabled) {
      updateDisplay();
    }
  }
}
