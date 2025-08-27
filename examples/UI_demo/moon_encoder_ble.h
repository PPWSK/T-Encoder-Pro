// moon_encoder_ble.h
//
// Header declarations for BLE scanning and communication with MOON devices.
// This header exposes the data structures and functions defined in
// moon_encoder_ble.cpp so that other modules (e.g. UI screens) can
// trigger scans, connect to a device and send commands.  It also
// exposes a hook for forwarding log messages to an LVGL label on the
// screen.

#pragma once

#include <vector>
#include <Arduino.h>

// Forward declare LVGL types to avoid including the full LVGL header
// unless needed. If your compile unit already includes lvgl.h you
// can ignore this forward declaration.
struct _lv_obj_t;
typedef struct _lv_obj_t lv_obj_t;

// Structure to hold discovered device information. See
// moon_encoder_ble.cpp for implementation details.
struct MoonDeviceInfo {
    NimBLEAdvertisedDevice* device;
    int rssi;
};

// Global container of all discovered devices. After calling
// scanForMoonDevices() this vector will contain a snapshot of the
// advertised MOON devices found during the scan. Entries are not
// automatically freed – if you repeatedly call scanForMoonDevices()
// remember to free or reuse the previous devices as appropriate.
extern std::vector<MoonDeviceInfo> foundDevices;

// Scan for MOON devices for a given number of seconds. This is a
// blocking call – it will scan for ``scanTimeSeconds`` seconds and
// populate ``foundDevices``. A log message describing the scan
// progress and results is written to Serial and to the registered
// LVGL log label (see setBleLogLabel below).
void scanForMoonDevices(uint32_t scanTimeSeconds = 5);

// Attempt to connect to the MOON device with the strongest RSSI from
// ``foundDevices``. Returns true on success. If no devices have been
// scanned yet, returns false and logs a message. On a successful
// connection the UART service and characteristics are cached for
// subsequent calls to sendMoonCommand().
bool connectToBestMoon();

// Send a JSON command to the connected MOON device. The string should
// be a valid JSON object. Returns true if the write succeeds.
bool sendMoonCommand(const String& jsonCmd);

// Register a log label for BLE status messages. When non-null,
// moon_encoder_ble.cpp will update this label with text whenever a
// log message is generated (e.g. during scans or connection attempts).
void setBleLogLabel(lv_obj_t* label);
