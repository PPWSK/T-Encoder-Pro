#pragma once
#include <Arduino.h>
#include <vector>
#include <string>
#include <NimBLEAdvertisedDevice.h>

// One row for the UI table
struct MoonDeviceInfo {
    std::string mac;     // device address
    std::string name;    // friendly name (or mac)
    int         rssi;    // last seen RSSI
    bool        connected; // true if this is the current link
};

// ---- Init BLE once ----
void initMoonBle();

// ---- Async scan (non-blocking) ----
void startScanAsync(uint32_t seconds);  // screen stays responsive
void stopScan();
bool isScanning();

// ---- Snapshot & logs for the UI ----
bool bleCopySnapshot(std::vector<MoonDeviceInfo>& out); // returns true when changed
bool blePopUiLog(String& out);                           // single-line status feed

// ---- Connection state ----
bool connectToAddress(const std::string& addr);
bool isConnected();
std::string connectedMac();

// ---- UART JSON command once connected ----
bool sendMoonCommand(const String& jsonCmd);
