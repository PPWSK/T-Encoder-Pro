// Moon Encoder BLE Client Example
// This sketch shows how to discover nearby MOON devices over BLE, connect to
// the one with the strongest signal, and send JSON commands to control
// brightness, color temperature or moon phase.
//
// It uses the NimBLE-Arduino library, which is included with recent
// ESP32 Arduino cores. Make sure to select the ESP32‑S3 board and install
// the NimBLE library if needed.

#include <Arduino.h>
#include <NimBLEDevice.h>
#include <NimBLEAdvertisedDevice.h>
#include "moon_encoder_ble.h"
// Pull in LVGL for on-screen logging.  If you don't need to log to
// the screen you can remove this include; however setBleLogLabel()
// and updateBleLog() rely on lv_label_set_text().
#include "lvgl.h"

// Nordic UART Service UUIDs (commonly used for BLE UART). Use NimBLEUUID
// objects rather than const char* so that they can be passed directly
// into NimBLE API functions without implicit conversions. Using
// NimBLEUUID avoids ambiguous overload resolution errors when
// interacting with NimBLE functions such as isAdvertisingService().
static const NimBLEUUID NUS_SERVICE_UUID("6E400001-B5A3-F393-E0A9-E50E24DCCA9E");
static const NimBLEUUID NUS_TX_CHAR_UUID("6E400002-B5A3-F393-E0A9-E50E24DCCA9E");
static const NimBLEUUID NUS_RX_CHAR_UUID("6E400003-B5A3-F393-E0A9-E50E24DCCA9E");

// Note: The MoonDeviceInfo structure is defined in moon_encoder_ble.h. We do
// not redefine it here to avoid duplicate definition errors.

// Global container defined here; declared extern in moon_encoder_ble.h
std::vector<MoonDeviceInfo> foundDevices;
static NimBLEClient* moonClient = nullptr;
static NimBLERemoteCharacteristic* txCharacteristic = nullptr;
static NimBLERemoteCharacteristic* rxCharacteristic = nullptr;

// Pointer to an LVGL label for displaying log messages. When set via
// setBleLogLabel() this label will be updated on each log event.
static lv_obj_t* bleLogLabel = nullptr;

// Forward declaration of helper so we can call it from different
// functions. This function updates both Serial output and the LVGL
// label if available.
static void updateBleLog(const String& msg);

// Callback class for advertised devices
class MoonAdvertisedDeviceCallbacks : public NimBLEAdvertisedDeviceCallbacks {
public:
    void onResult(NimBLEAdvertisedDevice* advertisedDevice) override {
        // Filter by name or service UUID to find MOON devices. Here we
        // check if the UART service UUID is advertised or if the name
        // contains "MOON".
        bool match = false;
        if (advertisedDevice->haveServiceUUID() && advertisedDevice->isAdvertisingService(NUS_SERVICE_UUID)) {
            match = true;
        }
        if (advertisedDevice->haveName() && advertisedDevice->getName().find("MOON") != std::string::npos) {
            match = true;
        }
        if (match) {
            // Copy device to store; we must clone because the device
            // pointer goes out of scope after scanning stops.
            NimBLEAdvertisedDevice* copy = new NimBLEAdvertisedDevice(*advertisedDevice);
            foundDevices.push_back({copy, advertisedDevice->getRSSI()});
            // Log discovery to Serial and UI
            String line = String("Found device: ") + (copy->haveName() ? copy->getName().c_str() : copy->toString().c_str()) +
                          String(" RSSI=") + String(advertisedDevice->getRSSI());
            updateBleLog(line);
        }
    }
};

// Notification callback for data received from the MOON device
void onRxNotify(NimBLERemoteCharacteristic* pRemoteCharacteristic, uint8_t* pData, size_t length, bool isNotify) {
    // Print incoming data to Serial and update UI log
    String line = "RX: ";
    for (size_t i = 0; i < length; ++i) {
        line += (char)pData[i];
    }
    updateBleLog(line);
}

// Scan for MOON devices for a given number of seconds
void scanForMoonDevices(uint32_t scanTimeSeconds) {
    foundDevices.clear();
    NimBLEScan* pScan = NimBLEDevice::getScan();
    pScan->setAdvertisedDeviceCallbacks(new MoonAdvertisedDeviceCallbacks(), true);
    pScan->setActiveScan(true);
    updateBleLog("Scanning for MOON devices...");
    // Start scanning (blocking until complete when second argument is false)
    pScan->start(scanTimeSeconds, false);
    // Compose summary message
    String summary = String("Scan complete, found ") + foundDevices.size() + " device" + (foundDevices.size() == 1 ? "" : "s");
    updateBleLog(summary);
}

// Connect to the MOON device with the strongest signal
bool connectToBestMoon() {
    if (foundDevices.empty()) {
        updateBleLog("No MOON devices found");
        return false;
    }
    // Sort by RSSI descending
    std::sort(foundDevices.begin(), foundDevices.end(), [](const MoonDeviceInfo& a, const MoonDeviceInfo& b) {
        return a.rssi > b.rssi;
    });
    NimBLEAdvertisedDevice* device = foundDevices.front().device;
    updateBleLog(String("Connecting to ") + (device->haveName() ? device->getName().c_str() : device->toString().c_str()) + "...");
    moonClient = NimBLEDevice::createClient();
    if (!moonClient->connect(device)) {
        updateBleLog("Failed to connect");
        return false;
    }
    updateBleLog("Connected to MOON device");
    // Obtain the UART service
    NimBLERemoteService* pService = moonClient->getService(NUS_SERVICE_UUID);
    if (!pService) {
        updateBleLog("UART service not found");
        moonClient->disconnect();
        return false;
    }
    txCharacteristic = pService->getCharacteristic(NUS_TX_CHAR_UUID);
    rxCharacteristic = pService->getCharacteristic(NUS_RX_CHAR_UUID);
    if (!txCharacteristic || !rxCharacteristic) {
        updateBleLog("UART characteristics not found");
        moonClient->disconnect();
        return false;
    }
    // Subscribe to RX notifications to receive acknowledgements
    if (rxCharacteristic->canNotify()) {
        rxCharacteristic->subscribe(true, onRxNotify);
    }
    return true;
}

// Send a JSON command to the connected MOON device
bool sendMoonCommand(const String& jsonCmd) {
    if (!moonClient || !moonClient->isConnected() || !txCharacteristic) {
        updateBleLog("Not connected to MOON device");
        return false;
    }
    std::string str = jsonCmd.c_str();
    bool success = txCharacteristic->writeValue((uint8_t*)str.data(), str.size(), false);
    updateBleLog(String("Sent command: ") + jsonCmd);
    return success;
}

// Register a UI label to display log messages. Pass nullptr to
// disable on-screen logging.
void setBleLogLabel(lv_obj_t* label) {
    bleLogLabel = label;
}

// Helper to update Serial and UI log simultaneously
static void updateBleLog(const String& msg) {
    Serial.println(msg);
    if (bleLogLabel) {
        // Copy the message into an LVGL compatible buffer. LVGL expects
        // null-terminated C strings.
        lv_label_set_text(bleLogLabel, msg.c_str());
    }
}

/*
 * Stand‑alone demonstration entry points. These functions allow this
 * module to be compiled as an example sketch on its own. When
 * integrated into the UI demo firmware the UI code will provide its
 * own setup() and loop() functions, so these definitions should be
 * excluded. To enable the stand‑alone example define
 * MOON_ENCODER_BLE_STANDALONE before including this file.
 */
#ifdef MOON_ENCODER_BLE_STANDALONE

void setup() {
    Serial.begin(115200);
    Serial.println("Moon Encoder BLE Client starting...");
    NimBLEDevice::init("");
    // optional: set initial power level
    NimBLEDevice::setPower(ESP_PWR_LVL_N9);
    scanForMoonDevices(5);
    if (connectToBestMoon()) {
        // Example: send get_info request once connected
        sendMoonCommand("{\"get_info\":true}");
    }
}

void loop() {
    // Example: send periodic commands or handle UI input
    // Here we just keep the connection alive and process notifications
    if (moonClient && !moonClient->isConnected()) {
        Serial.println("Disconnected, rescanning...");
        scanForMoonDevices(5);
        connectToBestMoon();
    }
    delay(1000);
}

#endif // MOON_ENCODER_BLE_STANDALONE