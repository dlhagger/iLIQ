#pragma once

#include <Arduino.h>
#include <SdFat.h>

class SessionLogger {
 public:
  static constexpr size_t MAX_BUFFER_BYTES = 48 * 1024;
  using TimestampProvider = bool (*)(uint16_t* year, uint8_t* month, uint8_t* day,
                                     uint8_t* hour, uint8_t* minute, uint8_t* second);

  explicit SessionLogger(SdFat& sd);
  void setTimestampProvider(TimestampProvider provider);

  bool startSession(const char* filename);
  void appendEvent(uint64_t nowMs, const char* wallTime, const char* electrode, const char* eventName);
  void flushIfIdle(uint64_t nowMs, unsigned long flushTimeoutMs);
  bool flush();
  bool closeSession();
  void handleCardRemoval();

  bool isSessionOpen() const { return sessionOpen_; }
  bool hasBufferedData() const { return buffer_.length() > 0; }
  const char* currentFilename() const { return filename_; }
  size_t bufferedBytes() const { return buffer_.length(); }
  uint32_t droppedEventCount() const { return droppedEvents_; }

 private:
  bool applyTimestamp(uint8_t flags);
  bool writeHeader();

  SdFat& sd_;
  FsFile file_;
  char filename_[40] = "/log.csv";
  String buffer_;
  uint64_t lastEventMs_ = 0;
  uint32_t droppedEvents_ = 0;
  bool sessionOpen_ = false;
  TimestampProvider timestampProvider_ = nullptr;
};
