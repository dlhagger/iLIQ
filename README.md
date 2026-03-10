# iLIQ individual Lick Instance Quantifier

Firmware for an RP2040-based lickometer with:
- MPR121 capacitive lick sensing (12 electrodes)
- SD card session logging (CSV)
- DS3231 RTC wall-time support
- SH1107 OLED status UI
- TTL pulse output on pad 0 touches
- Recovery behavior for SD and sensor faults

This project is designed for low-latency touch capture while remaining robust to removable media and peripheral faults.

## How It Works

The sketch runs a small state machine:
- `IDLE`: waiting to start a recording session
- `RECORDING`: logging touch edges and system events
- `DEGRADED`: MPR121 unavailable; auto-retry runs until recovery

Recording starts only when `A` is pressed. During recording:
- Touch edges (`START`/`STOP`) are logged per electrode
- Counts are shown on the OLED
- Buffered events flush to SD after inactivity

## Hardware Mapping

Pin definitions from the sketch:
- `SD_CS_PIN`: `23`
- `SD_DETECT_PIN`: `16` (`HIGH` means inserted)
- `BUTTON_A`: `9` (start)
- `BUTTON_B`: `6` (toggle display during recording)
- `BUTTON_C`: `5` (stop)
- `TTL_OUT_PIN`: `26` (`A0` on RP2040 Adalogger), HIGH while pad 0 is touched
- `MPR121_I2C_ADDR`: `0x5A`

## Controls

- `A`: start recording session
- `B`: toggle OLED on/off during recording
- `C`: stop recording session

## Session Logging

Each session writes one CSV file:
- Name format with RTC: `/log_YYYYMMDD_HHMMSS_mmm.csv`
- Collision fallback: adds `_NNN` suffix if needed
- Fallback without RTC: `/log_ms<millis64>.csv` (with `_NNN` collision suffix)

CSV format:
- Row 1: `SchemaVersion,1`
- Row 2: `Time(ms),WallTime,Electrode,Event`
- Data rows:
  - Touch edges: `E0..E11`, events `START`/`STOP`
  - System events: `E-`, events like `SESSION_START`, `SESSION_STOP`, `MPR121_SELF_HEAL`

Timing fields:
- `Time(ms)`: 64-bit monotonic milliseconds (`time_us_64()/1000`)
- `WallTime`: ISO-like RTC string for system events; touch edges leave wall-time empty for speed

## Buffering and SD Failure Handling

`SessionLogger` uses a RAM buffer with a hard cap:
- `MAX_BUFFER_BYTES = 48 * 1024`
- If full, oldest buffered lines are dropped and a marker row is emitted after recovery:
  - `BUFFER_OVERFLOW_DROPPED=<n>`

When SD is removed during recording:
- Recording continues in RAM (`REC(NO SD)` shown)
- File handle is closed safely
- Writes are retried after reinsertion

When SD is reinserted:
- Buffered events are flushed automatically
- Deferred close/finalization is attempted when not actively recording

## MPR121 Health and Self-Heal

The firmware considers MPR121 faulted when:
- `ECR == 0x00` (stopped), or
- OOR registers `0x02/0x03` are non-zero

During `RECORDING`, health checks run periodically. On fault:
- Reinitialize MPR121 (including autoconfig)
- If recovery succeeds, continue recording
- If recovery fails, stop session and enter `DEGRADED`

Peripheral auto-retry attempts are also made from `DEGRADED`.

## RTC and File Modified Times

File timestamps are stamped from RTC via `SessionLogger` during create/flush.
Timestamp metadata is best-effort; data durability takes priority.

## OLED UI

Idle screen shows:
- Current RTC date/time (or `RTC_MISSING`)
- Peripheral status (`SD`, `RTC`, `MPR121`)
- Button hints

Recording screen shows:
- `REC` or `REC(NO SD)` with elapsed time
- 3-column per-pad counts (`0..11`)

Degraded screen shows:
- Peripheral status
- Auto-retry message

## Debug Mode

`DEBUG_MODE` is set in the sketch:
- If `DEBUG_MODE == true`, boot blocks until serial is connected.
- OLED shows a waiting message in this mode.
- This intentionally prevents running recordings in debug mode without a host.

For standalone operation, set `DEBUG_MODE false` and reflash.

## Dependencies

Main libraries used:
- `SdFat`
- `Adafruit_MPR121`
- `RTClib`
- `Adafruit_SH110X`
- RP2040 SDK time API (`pico/time.h`)

## Project Layout

- `RP2040_Lickometer.ino`: main state machine, UI, hardware integration
- `SessionLogger.h/.cpp`: buffered CSV logger + SD recovery + timestamps
- `ButtonDebouncer.h`: non-blocking button edge debounce
- `tools/validate_log_csv.py`: CSV validator
- `TESTING_CHECKLIST.md`: pointer to test section in this README

## Validation and Test Checklist

Hardware smoke tests:
1. Boot with USB serial connected in debug mode; verify startup reaches `IDLE`.
2. Boot with SD inserted; verify card is detected on idle status screen.
3. Boot without SD; press `A`; verify no-start behavior and no crash.
4. Start (`A`), touch multiple electrodes, stop (`C`); verify CSV created.
5. Toggle display with `B` during recording; verify logging continues.
6. Remove SD during recording; verify recording continues with `REC(NO SD)`.
7. Reinsert SD; verify buffered events flush and session finalizes cleanly.

Peripheral recovery tests:
1. Boot with MPR121 disconnected; verify `DEGRADED`.
2. Reconnect MPR121; verify auto-recovery to `IDLE`.
3. Boot with RTC disconnected; verify logging still works (`RTC_UNAVAILABLE` wall-time fallback).
4. Reconnect RTC; verify retry recovery and proper wall-time on system events.

CSV checks:
```bash
python3 tools/validate_log_csv.py /path/to/log_directory
```

Expected:
1. `SchemaVersion,1` first row
2. Header row exactly `Time(ms),WallTime,Electrode,Event`
3. Monotonic non-negative `Time(ms)`
4. `SESSION_START` includes `WallTime`
5. At most one `SESSION_STOP` per file
