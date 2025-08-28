#include <Arduino.h>
#include <NimBLEDevice.h>
#include <NimBLEAdvertisedDevice.h>
#include <unordered_map>
#include <algorithm>
#include "moon_encoder_ble.h"

// -----------------------------
// UART UUIDs (adjust if MOON differs from Nordic NUS)
// -----------------------------
static const NimBLEUUID NUS_SERVICE_UUID("6E400001-B5A3-F393-E0A9-E50E24DCCA9E");
static const NimBLEUUID NUS_TX_CHAR_UUID("6E400002-B5A3-F393-E0A9-E50E24DCCA9E");
static const NimBLEUUID NUS_RX_CHAR_UUID("6E400003-B5A3-F393-E0A9-E50E24DCCA9E");

// -----------------------------
// BLE link + state
// -----------------------------
static NimBLEClient*               s_client = nullptr;
static NimBLERemoteCharacteristic* s_tx     = nullptr;
static NimBLERemoteCharacteristic* s_rx     = nullptr;
static std::string                 s_connectedMac;
static std::string                 s_connectedName;
static bool                        s_connectBusy = false; // guard against double taps


struct Rec { std::string name; int rssi = -127; bool moon=false; };
static std::unordered_map<std::string, Rec> s_map;   // key=MAC

static bool     s_bleInited   = false;
static bool     s_scanning    = false;
static uint32_t s_scanEnd     = 0;
static volatile bool s_devicesDirty = false;

// UI log handoff (status line)
static volatile bool s_uiLogDirty = false;
static String s_uiLine;
static inline void uiLog(const String& msg){ Serial.println(msg); s_uiLine = msg; s_uiLogDirty = true; }
bool blePopUiLog(String& out){ if(!s_uiLogDirty) return false; out = s_uiLine; s_uiLogDirty=false; return true; }
bool isConnecting(){ return s_connectBusy;}

// -----------------------------
// Init
// -----------------------------
void initMoonBle(){
    if (s_bleInited) return;
    Serial.begin(115200);
    Serial.println("[BLE] init");
    NimBLEDevice::init("T-Encoder-Pro");
    NimBLEDevice::setPower(ESP_PWR_LVL_P9);
    s_bleInited = true;
    Serial.println("[BLE] init done");
}

// -----------------------------
// Filter: accept only MOON devices
// -----------------------------
static bool looksLikeMoon(NimBLEAdvertisedDevice* d){
    if (d->haveName()){
        String nm = d->getName().c_str(); nm.toUpperCase();
        if (nm.startsWith("MOON")) return true;
    }
    if (d->haveManufacturerData()){
        const std::string md = d->getManufacturerData(); // by value
        for(size_t i=0;i+3<md.size();++i){
            char m=md[i], o1=md[i+1], o2=md[i+2], n=md[i+3];
            if ((m=='M'||m=='m')&&(o1=='O'||o1=='o')&&(o2=='O'||o2=='o')&&(n=='N'||n=='n'))
                return true;
        }
    }
    return false;
}

// -----------------------------
// Scan callbacks (declare BEFORE startScanAsync)
// -----------------------------
class CB : public NimBLEAdvertisedDeviceCallbacks {
    void onResult(NimBLEAdvertisedDevice* d) override {
        const std::string mac = d->getAddress().toString();
        Rec &r = s_map[mac];

        if (d->haveName()) r.name = d->getName();
        if (d->getRSSI() > r.rssi) r.rssi = d->getRSSI();

        if (!s_connectedMac.empty() && s_connectedMac == mac && !r.name.empty()) {
            s_connectedName = r.name;
        }

        bool wasMoon = r.moon;
        r.moon = looksLikeMoon(d);

        if (r.moon && !wasMoon) {
            uiLog(String("MOON ") + mac.c_str() + " (" + (r.name.empty()? "" : r.name.c_str()) + ")");
        }
        if (r.moon) s_devicesDirty = true;
    }
};

// Optional client callbacks for logging
class ClientCB : public NimBLEClientCallbacks {
    void onConnect(NimBLEClient* c) override { uiLog("Connected"); }
    void onDisconnect(NimBLEClient* c) override { uiLog("Disconnected"); s_connectedMac.clear(); }
};

// -----------------------------
// Scan control (non-blocking)
// -----------------------------
static void finishIfTimedOut() {
    if (!s_scanning) return;
    if ((int32_t)(millis() - s_scanEnd) >= 0){
        NimBLEDevice::getScan()->stop();
        s_scanning = false;
        uiLog("Scan complete");
    }
}

void startScanAsync(uint32_t seconds){
    initMoonBle();

    NimBLEScan* scan = NimBLEDevice::getScan();
    static CB cb;

    // Prepare fresh DB
    scan->stop();
    scan->clearResults();
    s_map.clear();
    s_devicesDirty = true;

    scan->setAdvertisedDeviceCallbacks(&cb, /*wantDuplicates=*/false);
    scan->setActiveScan(true);
    scan->setInterval(80); // ~50ms
    scan->setWindow(30);   // ~19ms
    scan->setDuplicateFilter(true);
    scan->setMaxResults(150);

    uiLog("Scanning…");
    s_scanning = true;
    s_scanEnd  = millis() + seconds*1000UL;

    // Non-blocking start: returns immediately
    scan->start(seconds, /*is_continue=*/true);
}

void stopScan(){
    NimBLEDevice::getScan()->stop();
    s_scanning = false;
    uiLog("Scan stopped");
}

bool isScanning(){
    finishIfTimedOut();
    return s_scanning;
}

// JSON line feed (assumes MOON replies one JSON per notify, or newline-terminated)
static String s_lastJson;
static volatile bool s_jsonDirty = false;

bool blePopLastJson(String& out){
    if (!s_jsonDirty) return false;
    out = s_lastJson;
    s_jsonDirty = false;
    return true;
}


// -----------------------------
// Snapshot for UI table (only MOON rows)
// -----------------------------
bool bleCopySnapshot(std::vector<MoonDeviceInfo>& out){
    out.clear();
    bool haveConnected = false;

    for (auto &kv : s_map){
        const auto &mac = kv.first;
        const auto &r   = kv.second;
        if (!r.moon) continue;

        MoonDeviceInfo di;
        di.mac       = mac;
        di.name      = r.name.empty()? mac : r.name;
        di.rssi      = r.rssi;
        di.connected = (!s_connectedMac.empty() && s_connectedMac == mac);
        if (di.connected) haveConnected = true;
        out.push_back(di);
    }

    if (!s_connectedMac.empty() && !haveConnected) {
        MoonDeviceInfo di;
        di.mac       = s_connectedMac;
        di.name      = s_connectedName.empty()? s_connectedMac : s_connectedName;
        di.rssi      = 0;
        di.connected = true;
        out.push_back(di);
    }

    std::sort(out.begin(), out.end(),
        [](const MoonDeviceInfo& a, const MoonDeviceInfo& b){
            if (a.connected != b.connected) return a.connected; // connected first
            return a.rssi > b.rssi;
        });

    s_devicesDirty = false;
    return true; // always refresh after connect/disconnect
}

// -----------------------------
// Connect & send commands
// -----------------------------
bool isConnected(){ return s_client && s_client->isConnected(); }
std::string connectedMac(){ return s_connectedMac; }

bool connectToAddress(const std::string& addr){
    if (s_connectBusy) return false;
    s_connectBusy = true;

    if (s_scanning) stopScan();

    // Switching device? Disconnect first.
    if (s_client && s_client->isConnected() && s_connectedMac != addr) {
        uiLog(String("Disconnecting… ") + s_connectedMac.c_str());
        s_client->disconnect();
        delay(150);
        s_connectedMac.clear();
        s_connectedName.clear();
    }

    uiLog(String("Connecting… ") + addr.c_str());

    if (!s_client) s_client = NimBLEDevice::createClient();
    static ClientCB ccb; s_client->setClientCallbacks(&ccb, false);
    s_client->setConnectTimeout(10);

    // Retry more times with small backoff
    const int kMaxAttempts = 3;
    bool linked = false;
    for (int attempt=1; attempt<=kMaxAttempts; ++attempt) {
        if (s_client->connect(NimBLEAddress(addr))) { linked = true; break; }
        uiLog(String("Connect failed (try ") + attempt + ")"); 
        delay(150 + attempt*100); // backoff
    }
    if (!linked) { s_connectBusy = false; return false; }

    // Make sure services are fresh (prevents stale handles)
    s_client->discoverAttributes();

    NimBLERemoteService* svc = s_client->getService(NUS_SERVICE_UUID);
    if (!svc) {
        uiLog("UART service not found (UUIDs?)");
        auto* svcs = s_client->getServices(true);
        if (svcs) for (auto &s : *svcs)
            Serial.printf("  svc %s\n", s->getUUID().toString().c_str());
        s_client->disconnect();
        s_connectBusy = false;
        return false;
    }

    s_tx = svc->getCharacteristic(NUS_TX_CHAR_UUID);
    s_rx = svc->getCharacteristic(NUS_RX_CHAR_UUID);
    if (!s_tx || !s_rx) {
        uiLog("UART chars missing (UUIDs?)");
        s_client->disconnect();
        s_connectBusy = false;
        return false;
    }

    if (s_rx->canNotify()){
        s_rx->subscribe(
            true,
            [](NimBLERemoteCharacteristic* /*chr*/, uint8_t* p, size_t n, bool /*isNotify*/){
                // Build a String (one-per-notify or one-per-line per your MOON firmware)
                String line; line.reserve(n + 4);
                for (size_t i = 0; i < n; ++i) line += (char)p[i];

                Serial.println("RX: " + line);   // keep serial logging
                s_lastJson = line;               // make last JSON available to UI
                s_jsonDirty = true;
            }
        );
    }

    s_connectedMac = addr;
    auto it = s_map.find(addr);
    s_connectedName = (it != s_map.end() && !it->second.name.empty()) ? it->second.name : addr;

    s_devicesDirty = true;
    uiLog("Connected ✓");

    // Ask device for info right away
    sendMoonCommand("{\"get_info\":true}");

    s_connectBusy = false;
    return true;
}


bool sendMoonCommand(const String& jsonCmd){
    if (!isConnected() || !s_tx){ uiLog("Not connected"); return false; }
    std::string s = jsonCmd.c_str();
    bool ok = s_tx->writeValue((uint8_t*)s.data(), s.size(), false);
    Serial.println(String("Sent: ")+jsonCmd);
    return ok;
}
