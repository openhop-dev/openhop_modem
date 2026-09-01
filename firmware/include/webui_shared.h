#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace WebUiShared {

struct Capabilities {
    bool wifi = false;
    bool ethernet = false;
    bool mdns = false;
    bool wifiReset = false;
    bool wifiAntennaSelection = false;
    bool heltecV43Controls = false;
    bool gps = false;
    bool battery = false;
    bool radio = false;
    bool updateAvailable = false;
    bool httpFirmwareUpload = false;
    bool writableManagement = false;
    bool exposeTcpToken = false;
    bool bleDfu = false;
};

struct NetworkModel {
    std::string interfaceName = "Offline";
    bool live = false;
    std::string currentIp;
    std::string subnet;
    std::string gateway;
    std::string dns1;
    std::string dns2;
    bool hasWifiRssi = false;
    int32_t wifiRssiDbm = 0;
    std::string linkState;
    std::string mac;
    std::string tcpStatus;
};

struct ConfigModel {
    std::string hostname;
    bool useStaticIp = false;
    std::string staticIp;
    std::string subnet;
    std::string gateway;
    std::string dns1;
    std::string dns2;
    uint16_t tcpPort = 5055;
    bool tcpTokenSet = false;
    std::string tcpToken;
    bool wifiExternalAntenna = false;
    bool wifiPowerSave = true;
    bool gpsEnabled = false;
    bool heltecV43ExternalLnaEnabled = false;
    bool heltecV43FemLnaBypassed = false;
    uint16_t agcResetIntervalSec = 0;
};

struct BatteryModel {
    bool available = false;
    bool voltageValid = false;
    uint16_t voltageMv = 0;
    bool chargeRateAvailable = false;
    bool chargeRateValid = false;
    float chargeRatePctPerHour = 0.0f;
};

struct GpsSatelliteModel {
    std::string prn;
    bool elevationValid = false;
    int16_t elevationDegrees = 0;
    bool azimuthValid = false;
    int16_t azimuthDegrees = 0;
    bool snrValid = false;
    float snrDb = 0.0f;
};

struct GpsModel {
    bool available = false;
    bool enabled = false;
    bool seen = false;
    bool fixValid = false;
    uint8_t fixQuality = 0;
    uint8_t satellitesUsed = 0;
    uint8_t satellitesInView = 0;
    std::vector<GpsSatelliteModel> satellites;
    double latitude = 0.0;
    double longitude = 0.0;
    bool altitudeValid = false;
    double altitudeM = 0.0;
    bool speedValid = false;
    double speedKmh = 0.0;
    bool courseValid = false;
    double courseDegrees = 0.0;
    std::string utcTime;
    std::string date;
    std::string datetimeUtc;
    std::string lastSentenceType;
    uint32_t validSentenceCount = 0;
    uint32_t invalidChecksumCount = 0;
    uint32_t rawByteCount = 0;
    uint32_t configCommandCount = 0;
    int16_t uartRxPin = -1;
    int16_t uartTxPin = -1;
    uint32_t uartBaud = 0;
    int16_t enablePin = -1;
    int16_t resetPin = -1;
    bool ageValid = false;
    uint32_t ageMs = 0;
};

struct RadioModel {
    bool available = false;
    std::string state = "unknown";
    bool standby = false;
    bool autoCadEnabled = false;
    uint32_t frequencyHz = 0;
    uint32_t bandwidthHz = 0;
    uint8_t spreadingFactor = 0;
    uint8_t codingRate = 0;
    int8_t txPowerDbm = 0;
    uint8_t syncword = 0;
    uint16_t preambleLength = 0;
};

struct CountersModel {
    uint32_t rxPackets = 0;
    uint32_t txPackets = 0;
    uint32_t crcErrors = 0;
    int16_t lastRssiDbm = 0;
    float lastSnrDb = 0.0f;
    float noiseFloorDbm = 0.0f;
};

struct Model {
    std::string board;
    std::string firmware;
    std::string hostname;
    std::string connectedClientIp;
    std::string dfuBluetoothAddress;
    uint32_t uptimeSec = 0;
    int8_t dieTemperatureC = 0;
    Capabilities capabilities;
    NetworkModel network;
    ConfigModel config;
    BatteryModel battery;
    GpsModel gps;
    RadioModel radio;
    CountersModel counters;
    std::string updateUnavailableReason;
};

struct WifiNetwork {
    std::string ssid;
    int32_t rssiDbm = 0;
};

struct SetupModel {
    std::vector<WifiNetwork> networks;
    std::string savedSsid;
    std::string password;
    bool wifiAntennaSelection = false;
    bool wifiExternalAntenna = false;
    bool heltecV43Controls = false;
    bool heltecV43ExternalLnaEnabled = false;
    uint16_t agcResetIntervalSec = 0;
    bool useStaticIp = false;
    std::string staticIp;
    std::string gateway;
    std::string subnet;
    std::string dns1;
    std::string dns2;
    std::string hostname;
    uint16_t tcpPort = 5055;
    std::string tcpToken;
};

std::string htmlEscape(const std::string& value);
std::string jsonEscape(const std::string& value);
std::string renderSetupPage(const SetupModel& model);
std::string renderRootPage(const Model& model);
std::string renderStatsPage(const Model& model);
std::string renderSystemJson(const Model& model);
std::string renderRadioJson(const Model& model);
std::string renderCountersJson(const Model& model);
std::string renderNetworkJson(const Model& model);
std::string renderConfigJson(const Model& model);
std::string renderGpsJson(const Model& model);
std::string renderStatsJson(const Model& model);
std::string renderTempJson(const Model& model);

}  // namespace WebUiShared
