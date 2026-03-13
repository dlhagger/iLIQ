#include "SessionLogger.h"
#include <string.h>

SessionLogger::SessionLogger(SdFat& sd) : sd_(sd) {
  buffer_.reserve(512);
}

void SessionLogger::setTimestampProvider(TimestampProvider provider) {
  timestampProvider_ = provider;
}

bool SessionLogger::applyTimestamp(uint8_t flags) {
  if (!file_ || !timestampProvider_) {
    return true;
  }

  uint16_t year = 0;
  uint8_t month = 0;
  uint8_t day = 0;
  uint8_t hour = 0;
  uint8_t minute = 0;
  uint8_t second = 0;
  if (!timestampProvider_(&year, &month, &day, &hour, &minute, &second)) {
    return true;
  }

  if (year < 1980) {
    year = 1980;
  } else if (year > 2099) {
    year = 2099;
  }

  return file_.timestamp(flags, year, month, day, hour, minute, second);
}

bool SessionLogger::startSession(const char* filename) {
  if (!closeSession()) {
    return false;
  }

  strncpy(filename_, filename, sizeof(filename_) - 1);
  filename_[sizeof(filename_) - 1] = '\0';

  file_ = sd_.open(filename_, O_WRONLY | O_CREAT | O_TRUNC);
  if (!file_) {
    sessionOpen_ = false;
    return false;
  }

  sessionOpen_ = true;
  buffer_ = "";
  lastEventMs_ = 0;
  droppedEvents_ = 0;
  if (!writeHeader()) {
    if (file_) {
      file_.close();
    }
    sessionOpen_ = false;
    return false;
  }
  // Timestamp metadata is best-effort. Do not fail session start if it cannot be applied.
  if (applyTimestamp(T_CREATE | T_WRITE | T_ACCESS)) {
    file_.sync();
  }

  return true;
}

bool SessionLogger::writeHeader() {
  if (!sessionOpen_) {
    return false;
  }

  file_.println("SchemaVersion,1");
  file_.println("Time(ms),WallTime,Electrode,Event");
  return file_.sync();
}

void SessionLogger::appendEvent(uint64_t nowMs, const char* wallTime, const char* electrode, const char* eventName) {
  if (!sessionOpen_) {
    return;
  }

  char msBuf[24];
  snprintf(msBuf, sizeof(msBuf), "%llu", static_cast<unsigned long long>(nowMs));

  String logLine;
  logLine.reserve(64);
  logLine += msBuf;
  logLine += ",";
  logLine += wallTime;
  logLine += ",";
  logLine += electrode;
  logLine += ",";
  logLine += eventName;
  logLine += "\n";

  size_t lineLen = logLine.length();
  if (lineLen > MAX_BUFFER_BYTES) {
    droppedEvents_++;
    lastEventMs_ = nowMs;
    return;
  }

  while ((buffer_.length() + lineLen) > MAX_BUFFER_BYTES) {
    int newlinePos = buffer_.indexOf('\n');
    if (newlinePos < 0) {
      buffer_ = "";
      droppedEvents_++;
      break;
    }
    buffer_.remove(0, newlinePos + 1);
    droppedEvents_++;
  }

  buffer_ += logLine;

  lastEventMs_ = nowMs;
}

void SessionLogger::flushIfIdle(uint64_t nowMs, unsigned long flushTimeoutMs) {
  if (!sessionOpen_ || buffer_.length() == 0) {
    return;
  }

  if ((nowMs - lastEventMs_) >= flushTimeoutMs) {
    flush();
  }
}

bool SessionLogger::flush() {
  if (!sessionOpen_ || buffer_.length() == 0) {
    return true;
  }

  if (!file_) {
    file_ = sd_.open(filename_, FILE_WRITE);
    if (!file_) {
      return false;
    }
  }

  if (droppedEvents_ > 0) {
    char overflowMsBuf[24];
    snprintf(overflowMsBuf, sizeof(overflowMsBuf), "%llu", static_cast<unsigned long long>(lastEventMs_));

    String overflowLine;
    overflowLine.reserve(64);
    overflowLine += overflowMsBuf;
    overflowLine += ",,E-,BUFFER_OVERFLOW_DROPPED=";
    overflowLine += String(droppedEvents_);
    overflowLine += "\n";

    size_t overflowWritten = file_.print(overflowLine);
    if (overflowWritten != overflowLine.length()) {
      file_.close();
      return false;
    }
  }

  size_t written = file_.print(buffer_);
  if (written != buffer_.length()) {
    file_.close();
    return false;
  }

  if (!file_.sync()) {
    file_.close();
    return false;
  }

  // Timestamp metadata is best-effort. Data durability is guaranteed by the sync() above.
  if (applyTimestamp(T_WRITE | T_ACCESS)) {
    file_.sync();
  }

  buffer_ = "";
  droppedEvents_ = 0;
  return true;
}

bool SessionLogger::closeSession() {
  if (!sessionOpen_) {
    return true;
  }

  if (!flush()) {
    return false;
  }

  if (file_) {
    file_.close();
  }
  sessionOpen_ = false;
  buffer_ = "";
  return true;
}

void SessionLogger::handleCardRemoval() {
  if (file_) {
    file_.close();
  }
}
