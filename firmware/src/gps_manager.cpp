#include "gps_manager.h"

#include "board_config.h"

#ifdef ARDUINO_ARCH_ESP32
#include <HardwareSerial.h>
#endif

namespace GPSManager {

static Snapshot current;
static String line;
static constexpr unsigned long CONFIG_SETTLE_MS = 250;
static constexpr unsigned long COMMAND_DELAY_MS = 40;
static constexpr size_t CONFIG_COMMAND_COUNT = 2;
static unsigned long nextConfigCommandAtMs = 0;
static size_t nextConfigCommand = CONFIG_COMMAND_COUNT;

#ifdef ARDUINO_ARCH_ESP32
static HardwareSerial& gpsSerial = Serial1;
#endif

static String jsonEscape(const String& value) {
    String out;
    out.reserve(value.length() + 8);
    for (size_t i = 0; i < value.length(); ++i) {
        switch (value[i]) {
            case '\"': out += F("\\\""); break;
            case '\\': out += F("\\\\"); break;
            case '\n': out += F("\\n"); break;
            case '\r': out += F("\\r"); break;
            case '\t': out += F("\\t"); break;
            default: out += value[i]; break;
        }
    }
    return out;
}

static String jsonQuote(const String& value) {
    return String("\"") + jsonEscape(value) + "\"";
}

static bool hasGpsPins() {
    return BOARD.pin_gps_uart_rx >= 0 && BOARD.pin_gps_uart_tx >= 0 && BOARD.gps_uart_baud > 0;
}

#ifdef ARDUINO_ARCH_ESP32
static void scheduleAtgm336hConfig() {
    nextConfigCommand = 0;
    nextConfigCommandAtMs = millis() + CONFIG_SETTLE_MS;
}

static void runAtgm336hConfig() {
    static const char* const configCommands[CONFIG_COMMAND_COUNT] = {
        "$PCAS02,1000*2E",              // 1 Hz fixes.
        "$PCAS03,1,0,0,1,1,0,0,0*03",  // GGA + GSV + RMC.
    };

    if (nextConfigCommand >= CONFIG_COMMAND_COUNT) return;
    if ((long)(millis() - nextConfigCommandAtMs) < 0) return;

    gpsSerial.print(configCommands[nextConfigCommand++]);
    gpsSerial.print("\r\n");
    current.configCommandCount++;
    nextConfigCommandAtMs = millis() + COMMAND_DELAY_MS;
}
#endif

static bool checksumOk(const String& sentence) {
    int star = sentence.indexOf('*');
    if (star < 0) return false;
    if (star + 2 >= (int)sentence.length()) return false;

    uint8_t sum = 0;
    int start = sentence.startsWith("$") ? 1 : 0;
    for (int i = start; i < star; ++i) {
        sum ^= (uint8_t)sentence[i];
    }
    char expected[3] = { sentence[star + 1], sentence[star + 2], '\0' };
    char* end = nullptr;
    unsigned long parsed = strtoul(expected, &end, 16);
    return end && *end == '\0' && parsed == sum;
}

static String field(const String& payload, uint8_t index) {
    int start = 0;
    for (uint8_t i = 0; i < index; ++i) {
        start = payload.indexOf(',', start);
        if (start < 0) return String();
        ++start;
    }
    int end = payload.indexOf(',', start);
    if (end < 0) end = payload.length();
    return payload.substring(start, end);
}

static float parseCoordinate(const String& value, const String& hemi) {
    if (value.length() < 4 || hemi.length() == 0) return 0.0f;
    int dot = value.indexOf('.');
    if (dot < 0) dot = value.length();
    int degreeDigits = dot - 2;
    if (degreeDigits <= 0) return 0.0f;
    float degrees = value.substring(0, degreeDigits).toFloat();
    float minutes = value.substring(degreeDigits).toFloat();
    float out = degrees + minutes / 60.0f;
    char h = hemi[0];
    if (h == 'S' || h == 's' || h == 'W' || h == 'w') out *= -1.0f;
    return out;
}

static String formatUtcTime(const String& raw) {
    if (raw.length() < 6) return String();
    String hh = raw.substring(0, 2);
    String mm = raw.substring(2, 4);
    String ss = raw.substring(4);
    return hh + ":" + mm + ":" + ss + "Z";
}

static String formatDate(const String& raw) {
    if (raw.length() != 6) return String();
    int day = raw.substring(0, 2).toInt();
    int month = raw.substring(2, 4).toInt();
    int yy = raw.substring(4, 6).toInt();
    int year = yy < 80 ? 2000 + yy : 1900 + yy;
    char buf[16];
    snprintf(buf, sizeof(buf), "%04d-%02d-%02d", year, month, day);
    return String(buf);
}

static void updateDatetime() {
    if (current.date.length() == 0 || current.utcTime.length() == 0) return;
    String time = current.utcTime;
    if (time.endsWith("Z")) time.remove(time.length() - 1);
    current.datetimeUtc = current.date + "T" + time + "+00:00";
}

static String sentenceType(const String& payload) {
    String talkerType = field(payload, 0);
    if (talkerType.length() >= 3) return talkerType.substring(talkerType.length() - 3);
    return talkerType;
}

static void parseGga(const String& payload) {
    String utc = field(payload, 1);
    String lat = field(payload, 2);
    String ns = field(payload, 3);
    String lon = field(payload, 4);
    String ew = field(payload, 5);
    int quality = field(payload, 6).toInt();
    int sats = field(payload, 7).toInt();
    String altitude = field(payload, 9);

    if (utc.length()) current.utcTime = formatUtcTime(utc);
    current.fixQuality = (uint8_t)max(0, min(255, quality));
    current.fixValid = quality > 0;
    current.satellitesUsed = (uint8_t)max(0, min(255, sats));
    if (lat.length() && lon.length()) {
        current.latitude = parseCoordinate(lat, ns);
        current.longitude = parseCoordinate(lon, ew);
    }
    if (altitude.length()) {
        current.altitudeM = altitude.toFloat();
        current.hasAltitude = true;
    }
    updateDatetime();
}

static void parseRmc(const String& payload) {
    String utc = field(payload, 1);
    String status = field(payload, 2);
    String lat = field(payload, 3);
    String ns = field(payload, 4);
    String lon = field(payload, 5);
    String ew = field(payload, 6);
    String speedKnots = field(payload, 7);
    String course = field(payload, 8);
    String date = field(payload, 9);

    if (utc.length()) current.utcTime = formatUtcTime(utc);
    if (date.length()) current.date = formatDate(date);
    current.fixValid = (status == "A" || status == "a");
    if (lat.length() && lon.length()) {
        current.latitude = parseCoordinate(lat, ns);
        current.longitude = parseCoordinate(lon, ew);
    }
    if (speedKnots.length()) {
        current.speedKmh = speedKnots.toFloat() * 1.852f;
        current.hasSpeed = true;
    }
    if (course.length()) {
        current.courseDegrees = course.toFloat();
        current.hasCourse = true;
    }
    updateDatetime();
}

static void parseGsv(const String& payload) {
    int messageNumber = field(payload, 2).toInt();
    int satellitesInView = field(payload, 3).toInt();
    if (satellitesInView >= 0) {
        current.satellitesInViewCount = (uint8_t)max(0, min(255, satellitesInView));
    }
    if (messageNumber == 1) {
        current.satellitesInViewStored = 0;
    }

    for (uint8_t idx = 4; idx < 20; idx += 4) {
        String prn = field(payload, idx);
        if (prn.length() == 0) continue;
        if (current.satellitesInViewStored >= 32) break;

        SatelliteInView& sat = current.satellitesInView[current.satellitesInViewStored++];
        sat.prn = prn;
        sat.elevationDegrees = (int16_t)field(payload, idx + 1).toInt();
        sat.azimuthDegrees = (int16_t)field(payload, idx + 2).toInt();

        String snr = field(payload, idx + 3);
        sat.hasSnr = snr.length() > 0;
        sat.snrDb = sat.hasSnr ? snr.toFloat() : -1.0f;
    }
}

static void parseSentence(String sentence) {
    sentence.trim();
    if (sentence.length() == 0) return;
    if (!sentence.startsWith("$")) return;
    if (!checksumOk(sentence)) {
        current.invalidChecksumCount++;
        return;
    }

    int start = sentence.startsWith("$") ? 1 : 0;
    int star = sentence.indexOf('*');
    if (star < 0) star = sentence.length();
    String payload = sentence.substring(start, star);
    String type = sentenceType(payload);
    if (type.length() == 0) return;
    for (size_t i = 0; i < type.length(); ++i) {
        char c = type[i];
        if (!((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9'))) {
            current.invalidChecksumCount++;
            return;
        }
    }
    current.lastSentenceType = type;
    current.seen = true;
    current.validSentenceCount++;
    current.lastUpdateMs = millis();

    if (type == "GGA") {
        parseGga(payload);
    } else if (type == "GSV") {
        parseGsv(payload);
    } else if (type == "RMC") {
        parseRmc(payload);
    }
}

void begin() {
    current = Snapshot{};
    current.enabled = hasGpsPins();
    line.reserve(128);
#ifdef ARDUINO_ARCH_ESP32
    if (!current.enabled) return;
    gpsSerial.begin(BOARD.gps_uart_baud, SERIAL_8N1, BOARD.pin_gps_uart_rx, BOARD.pin_gps_uart_tx);
    Serial.printf("[GPS] UART GPS enabled on RX=%d TX=%d baud=%lu\n",
                  BOARD.pin_gps_uart_rx, BOARD.pin_gps_uart_tx,
                  (unsigned long)BOARD.gps_uart_baud);
    scheduleAtgm336hConfig();
#else
    current.enabled = false;
#endif
}

void loop() {
#ifdef ARDUINO_ARCH_ESP32
    if (!current.enabled) return;
    while (gpsSerial.available()) {
        char c = (char)gpsSerial.read();
        current.rawByteCount++;
        if (c == '\n') {
            parseSentence(line);
            line = String();
        } else if (c != '\r') {
            if (line.length() < 128) {
                line += c;
            } else {
                line = String();
            }
        }
    }
    runAtgm336hConfig();
#endif
}

Snapshot snapshot() {
    return current;
}

String buildJson() {
    Snapshot snap = snapshot();
    String body;
    body.reserve(2048);
    body += F("{\"enabled\":");
    body += snap.enabled ? F("true") : F("false");
    body += F(",\"seen\":");
    body += snap.seen ? F("true") : F("false");
    body += F(",\"fix\":{");
    body += F("\"valid\":");
    body += snap.fixValid ? F("true") : F("false");
    body += F(",\"quality\":");
    body += String(snap.fixQuality);
    body += F("},\"position\":{");
    body += F("\"latitude\":");
    body += snap.fixValid ? String(snap.latitude, 6) : String("null");
    body += F(",\"longitude\":");
    body += snap.fixValid ? String(snap.longitude, 6) : String("null");
    body += F(",\"altitude_m\":");
    body += snap.hasAltitude ? String(snap.altitudeM, 1) : String("null");
    body += F("},\"satellites\":{");
    body += F("\"used_count\":");
    body += String(snap.satellitesUsed);
    body += F(",\"in_view_count\":");
    body += String(snap.satellitesInViewCount);
    body += F(",\"in_view\":[");
    for (uint8_t i = 0; i < snap.satellitesInViewStored; ++i) {
        const SatelliteInView& sat = snap.satellitesInView[i];
        if (i > 0) body += ',';
        body += F("{\"prn\":");
        body += jsonQuote(sat.prn);
        body += F(",\"elevation_degrees\":");
        body += sat.elevationDegrees >= 0 ? String(sat.elevationDegrees) : String("null");
        body += F(",\"azimuth_degrees\":");
        body += sat.azimuthDegrees >= 0 ? String(sat.azimuthDegrees) : String("null");
        body += F(",\"snr_db\":");
        body += sat.hasSnr ? String(sat.snrDb, 1) : String("null");
        body += F("}");
    }
    body += F("]");
    body += F("},\"time\":{");
    body += F("\"utc_time\":");
    body += snap.utcTime.length() ? jsonQuote(snap.utcTime) : String("null");
    body += F(",\"date\":");
    body += snap.date.length() ? jsonQuote(snap.date) : String("null");
    body += F(",\"datetime_utc\":");
    body += snap.datetimeUtc.length() ? jsonQuote(snap.datetimeUtc) : String("null");
    body += F("},\"motion\":{");
    body += F("\"speed_kmh\":");
    body += snap.hasSpeed ? String(snap.speedKmh, 2) : String("null");
    body += F(",\"course_degrees\":");
    body += snap.hasCourse ? String(snap.courseDegrees, 2) : String("null");
    body += F("},\"nmea\":{");
    body += F("\"last_sentence_type\":");
    body += snap.lastSentenceType.length() ? jsonQuote(snap.lastSentenceType) : String("null");
    body += F(",\"valid_sentence_count\":");
    body += String(snap.validSentenceCount);
    body += F(",\"invalid_checksum_count\":");
    body += String(snap.invalidChecksumCount);
    body += F(",\"raw_byte_count\":");
    body += String(snap.rawByteCount);
    body += F(",\"config_command_count\":");
    body += String(snap.configCommandCount);
    body += F(",\"uart_rx_pin\":");
    body += String(BOARD.pin_gps_uart_rx);
    body += F(",\"uart_tx_pin\":");
    body += String(BOARD.pin_gps_uart_tx);
    body += F(",\"uart_baud\":");
    body += String(BOARD.gps_uart_baud);
    body += F(",\"age_ms\":");
    body += snap.lastUpdateMs ? String((uint32_t)(millis() - snap.lastUpdateMs)) : String("null");
    body += F("}}");
    return body;
}

} // namespace GPSManager
