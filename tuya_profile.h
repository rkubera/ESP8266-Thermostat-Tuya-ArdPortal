#pragma once
// Candidate ME81H profile. Confirm with /diagnostics before hardware use.
// A DP is writable only after the MCU reports the expected type.
// PCB V1.4: Blakadder's C3 GPIO3 occupies the same side pad as ESP-12E GPIO14.
// Derived from module footprints; verify continuity before mounting.
// Set -1 if the status pad is not connected in your installation.
constexpr int TUYA_STATUS_PIN = 14;
constexpr bool TUYA_STATUS_INVERTED = true;
constexpr uint8_t DP_POWER = 1;
constexpr uint8_t DP_OPERATION_MODE = 2; // enum 1 = manual, 0 = schedule
constexpr uint8_t DP_TARGET = 16;
constexpr uint8_t DP_CURRENT = 24;
// Assumed calibration scale: signed integer degrees Celsius.
constexpr uint8_t DP_CALIBRATION = 27;
constexpr uint8_t DP_SCHEDULE = 38;
// Enum order from ME81H product schema: 00,52,61,70; verify panel mapping.
constexpr uint8_t DP_PROGRAM_MODE = 102;
constexpr uint8_t DP_LOCK = 40;
constexpr uint8_t DP_SENSOR = 43; // enum 0 = air, 1 = floor, 2 = air + floor limit
constexpr float TARGET_SCALE = 1.0f;
constexpr float CURRENT_SCALE = 0.1f;
// ME81H UART field reports: DP36 enum 0=heating, 1=idle.
// Confirm on this device; no energy is estimated until a valid report arrives.
constexpr uint8_t DP_RELAY_REPORT = 36;
constexpr uint8_t RELAY_REPORT_TYPE = 4;
constexpr int32_t RELAY_ON_VALUE = 0;
constexpr int32_t RELAY_OFF_VALUE = 1;
// Highest allowed target in the current profile. Match the thermostat's
// configured limit; the MCU's advanced maximum is not exposed by a verified DP.
constexpr float HEAT_TARGET_MAX = 40.0f;
