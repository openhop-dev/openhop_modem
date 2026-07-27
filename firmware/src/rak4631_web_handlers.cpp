#include "rak4631_web_handlers.h"

#include "http_request_parser.h"

#include <cctype>
#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <vector>

namespace Rak4631WebHandlers {
namespace {

using Rak4631Config::Config;
using Rak4631Config::IPv4Address;
constexpr const char* FORM = "application/x-www-form-urlencoded";
constexpr const char* JSON = "application/json";

Response error(int status, const char* message) {
    return {status, "application/json; charset=utf-8",
            std::string("{\"error\":\"") + message + "\"}", Transition::NONE};
}

Response success(const char* body, Transition transition) {
    return {200, "application/json; charset=utf-8", body, transition};
}

bool exactContentType(const Request& request, const char* expected) {
    return request.contentType && std::strcmp(request.contentType, expected) == 0;
}

bool sameOriginOrNonBrowser(const Request& request) {
    if (!request.origin || !*request.origin) return true;
    if (!request.host || !*request.host) return false;
    const std::string expected = std::string("http://") + request.host;
    return expected == request.origin;
}

bool emptyActionBody(const Request& request) {
    return request.bodyLength == 0 &&
        (!request.contentType || !*request.contentType ||
         std::strcmp(request.contentType, FORM) == 0);
}

bool bodyIsText(const Request& request) {
    return request.body && std::strlen(request.body) == request.bodyLength;
}

bool copyText(char (&destination)[Rak4631Config::MAX_TEXT_LENGTH + 1], const char* value) {
    if (!value) return false;
    const size_t length = std::strlen(value);
    if (length > Rak4631Config::MAX_TEXT_LENGTH) return false;
    std::memset(destination, 0, sizeof(destination));
    std::memcpy(destination, value, length);
    return true;
}

bool parsePort(const char* text, uint16_t& output) {
    if (!text || !*text) return false;
    uint32_t value = 0;
    for (const char* cursor = text; *cursor; ++cursor) {
        if (*cursor < '0' || *cursor > '9') return false;
        value = value * 10U + static_cast<uint32_t>(*cursor - '0');
        if (value > 65535U) return false;
    }
    if (value == 0 || value == 80) return false;
    output = static_cast<uint16_t>(value);
    return true;
}

bool parseIp(const char* text, IPv4Address& output, bool allowBlank) {
    output = {};
    if (!text || !*text) return allowBlank;
    const char* cursor = text;
    for (size_t part = 0; part < 4; ++part) {
        if (!std::isdigit(static_cast<unsigned char>(*cursor))) return false;
        unsigned value = 0;
        size_t digits = 0;
        while (std::isdigit(static_cast<unsigned char>(*cursor))) {
            value = value * 10U + static_cast<unsigned>(*cursor - '0');
            if (++digits > 3 || value > 255) return false;
            ++cursor;
        }
        output.octets[part] = static_cast<uint8_t>(value);
        if (part < 3) {
            if (*cursor++ != '.') return false;
        } else if (*cursor != '\0') {
            return false;
        }
    }
    return true;
}

bool allowedFields(const HttpRequest::FormData& form,
                   const char* const* allowed, size_t allowedCount) {
    for (size_t i = 0; i < form.count; ++i) {
        bool found = false;
        for (size_t j = 0; j < allowedCount; ++j) {
            if (std::strcmp(form.fields[i].key, allowed[j]) == 0) found = true;
        }
        if (!found) return false;
        for (size_t j = 0; j < i; ++j) {
            if (std::strcmp(form.fields[i].key, form.fields[j].key) == 0) return false;
        }
    }
    return true;
}

template <size_t N>
bool parseFormRequest(const Request& request, HttpRequest::FormData& form,
                      const char* const (&allowed)[N]) {
    return exactContentType(request, FORM) && bodyIsText(request) &&
           HttpRequest::parseForm(request.body, form) && allowedFields(form, allowed, N);
}

Response persist(Config& config, SaveCallback save, void* context, bool reboot = true) {
    if (Rak4631Config::validateAndNormalize(config) != Rak4631Config::ValidationStatus::OK)
        return error(400, "invalid configuration");
    if (!save || !save(config, context)) return error(500, "configuration save failed");
    return reboot
        ? success("{\"status\":\"saved\",\"rebooting\":true}", Transition::REBOOT)
        : success("{\"status\":\"saved\",\"rebooting\":false}", Transition::NONE);
}

class JsonReader {
public:
    JsonReader(const char* data, size_t length) : cursor_(data), end_(data + length) {}

    bool objectBegin() { return token('{'); }
    bool objectEnd() { return token('}'); }
    bool arrayOrScalarUnsupported() const {
        const char value = peek();
        return value == '[' || value == 'n' || value == '-';
    }
    bool comma() { return token(','); }
    bool colon() { return token(':'); }
    bool done() { skip(); return cursor_ == end_; }
    char peek() const {
        const char* cursor = cursor_;
        while (cursor < end_ && std::isspace(static_cast<unsigned char>(*cursor))) ++cursor;
        return cursor == end_ ? '\0' : *cursor;
    }
    bool string(std::string& output) {
        skip();
        if (cursor_ == end_ || *cursor_++ != '"') return false;
        output.clear();
        while (cursor_ != end_) {
            unsigned char value = static_cast<unsigned char>(*cursor_++);
            if (value == '"') return true;
            if (value < 0x20) return false;
            if (value != '\\') { output.push_back(static_cast<char>(value)); continue; }
            if (cursor_ == end_) return false;
            const char escape = *cursor_++;
            switch (escape) {
                case '"': case '\\': case '/': output.push_back(escape); break;
                case 'b': output.push_back('\b'); break;
                case 'f': output.push_back('\f'); break;
                case 'n': output.push_back('\n'); break;
                case 'r': output.push_back('\r'); break;
                case 't': output.push_back('\t'); break;
                case 'u': {
                    unsigned code = 0;
                    for (int i = 0; i < 4; ++i) {
                        if (cursor_ == end_) return false;
                        const char c = *cursor_++;
                        code <<= 4;
                        if (c >= '0' && c <= '9') code += static_cast<unsigned>(c - '0');
                        else if (c >= 'a' && c <= 'f') code += static_cast<unsigned>(c - 'a' + 10);
                        else if (c >= 'A' && c <= 'F') code += static_cast<unsigned>(c - 'A' + 10);
                        else return false;
                    }
                    if (code == 0 || code > 0x7f || (code >= 0xd800 && code <= 0xdfff)) return false;
                    output.push_back(static_cast<char>(code));
                    break;
                }
                default: return false;
            }
        }
        return false;
    }
    bool boolean(bool& output) {
        skip();
        if (remaining("true")) { cursor_ += 4; output = true; return true; }
        if (remaining("false")) { cursor_ += 5; output = false; return true; }
        return false;
    }
    bool unsignedInteger(uint32_t& output) {
        skip();
        if (cursor_ == end_ || !std::isdigit(static_cast<unsigned char>(*cursor_))) return false;
        uint64_t value = 0;
        do {
            value = value * 10U + static_cast<unsigned>(*cursor_++ - '0');
            if (value > std::numeric_limits<uint32_t>::max()) return false;
        } while (cursor_ != end_ && std::isdigit(static_cast<unsigned char>(*cursor_)));
        if (cursor_ != end_ && (*cursor_ == '.' || *cursor_ == 'e' || *cursor_ == 'E')) return false;
        output = static_cast<uint32_t>(value);
        return true;
    }

private:
    void skip() { while (cursor_ != end_ && std::isspace(static_cast<unsigned char>(*cursor_))) ++cursor_; }
    bool token(char expected) { skip(); if (cursor_ == end_ || *cursor_ != expected) return false; ++cursor_; return true; }
    bool remaining(const char* text) const {
        const size_t length = std::strlen(text);
        return static_cast<size_t>(end_ - cursor_) >= length && std::memcmp(cursor_, text, length) == 0;
    }
    const char* cursor_;
    const char* end_;
};

bool seen(std::vector<std::string>& fields, const std::string& field) {
    for (const auto& prior : fields) if (prior == field) return true;
    fields.push_back(field);
    return false;
}

bool parseNetwork(JsonReader& json, Config& config) {
    if (!json.objectBegin()) return false;
    std::vector<std::string> fields;
    bool first = true;
    while (json.peek() != '}') {
        if (!first && !json.comma()) return false;
        first = false;
        std::string key;
        if (!json.string(key) || seen(fields, key) || !json.colon()) return false;
        if (key == "use_static_ip") {
            if (!json.boolean(config.useStaticIP)) return false;
        } else if (key == "static_ip" || key == "subnet" || key == "gateway" ||
                   key == "dns1" || key == "dns2") {
            std::string value;
            if (!json.string(value)) return false;
            IPv4Address* address = key == "static_ip" ? &config.staticIP :
                key == "subnet" ? &config.subnet : key == "gateway" ? &config.gateway :
                key == "dns1" ? &config.dns1 : &config.dns2;
            if (!parseIp(value.c_str(), *address, true)) return false;
        } else {
            return false;
        }
    }
    return json.objectEnd();
}

bool parseJsonConfig(const Request& request, Config& config) {
    if (!exactContentType(request, JSON) || !request.body || request.bodyLength == 0) return false;
    JsonReader json(request.body, request.bodyLength);
    if (!json.objectBegin()) return false;
    std::vector<std::string> fields;
    bool first = true;
    while (json.peek() != '}') {
        if (!first && !json.comma()) return false;
        first = false;
        std::string key;
        if (!json.string(key) || seen(fields, key) || !json.colon()) return false;
        if (key == "hostname" || key == "tcp_token") {
            std::string value;
            if (!json.string(value) || value.size() > Rak4631Config::MAX_TEXT_LENGTH) return false;
            if (key == "hostname") {
                if (!copyText(config.hostname, value.c_str())) return false;
            } else if (!copyText(config.tcpToken, value.c_str())) return false;
        } else if (key == "tcp_port") {
            uint32_t value = 0;
            if (!json.unsignedInteger(value) || value == 0 || value > 65535 || value == 80) return false;
            config.tcpPort = static_cast<uint16_t>(value);
        } else if (key == "use_static_ip") {
            if (!json.boolean(config.useStaticIP)) return false;
        } else if (key == "gps_enabled") {
            if (!request.gpsSupported || !json.boolean(config.gpsEnabled)) return false;
        } else if (key == "network") {
            if (!parseNetwork(json, config)) return false;
        } else {
            return false;
        }
    }
    return json.objectEnd() && json.done();
}

Response handleHostname(const Request& request, SaveCallback save, void* context) {
    static const char* const allowed[] = {"hostname"};
    HttpRequest::FormData form;
    if (!parseFormRequest(request, form, allowed)) return error(400, "invalid form fields");
    Config config = request.current;
    if (!copyText(config.hostname, form.get("hostname"))) return error(400, "invalid hostname");
    return persist(config, save, context);
}

Response handleNetwork(const Request& request, SaveCallback save, void* context) {
    static const char* const allowed[] = {"static", "ip", "sn", "gw", "dns1", "dns2", "port"};
    HttpRequest::FormData form;
    if (!parseFormRequest(request, form, allowed)) return error(400, "invalid form fields");
    Config config = request.current;
    const char* port = form.get("port");
    if (!parsePort(port, config.tcpPort)) return error(400, "invalid TCP port");
    config.useStaticIP = form.get("static") != nullptr;
    if (!config.useStaticIP) {
        config.staticIP = config.subnet = config.gateway = config.dns1 = config.dns2 = {};
    } else {
        if (!parseIp(form.get("ip"), config.staticIP, false) ||
            !parseIp(form.get("sn"), config.subnet, false) ||
            !parseIp(form.get("gw"), config.gateway, false) ||
            !parseIp(form.get("dns1"), config.dns1, true) ||
            !parseIp(form.get("dns2"), config.dns2, true)) {
            return error(400, "invalid static network");
        }
    }
    return persist(config, save, context);
}

Response handleToken(const Request& request, SaveCallback save, void* context) {
    static const char* const allowed[] = {"token", "confirm"};
    HttpRequest::FormData form;
    if (!parseFormRequest(request, form, allowed)) return error(400, "invalid form fields");
    const char* token = form.get("token");
    const char* confirm = form.get("confirm");
    if (!token || !confirm || std::strcmp(token, confirm) != 0) return error(400, "token confirmation mismatch");
    Config config = request.current;
    if (!copyText(config.tcpToken, token)) return error(400, "invalid token");
    return persist(config, save, context);
}

Response handleAuth(const Request& request, SaveCallback save, void* context) {
    static const char* const allowed[] = {"password", "confirm"};
    HttpRequest::FormData form;
    if (!parseFormRequest(request, form, allowed)) return error(400, "invalid form fields");
    const char* password = form.get("password");
    const char* confirm = form.get("confirm");
    if (!password || !*password || !confirm || std::strcmp(password, confirm) != 0)
        return error(400, "password confirmation mismatch");
    Config config = request.current;
    if (!copyText(config.httpPassword, password)) return error(400, "invalid password");
    return persist(config, save, context, false);
}

Response handleGps(const Request& request, SaveCallback save, void* context) {
    static const char* const allowed[] = {"gps_enabled"};
    HttpRequest::FormData form;
    if (!request.gpsSupported || !parseFormRequest(request, form, allowed))
        return error(400, "unsupported GPS configuration");
    const char* enabled = form.get("gps_enabled");
    if (enabled && std::strcmp(enabled, "1") != 0) return error(400, "invalid GPS value");
    Config config = request.current;
    config.gpsEnabled = enabled != nullptr;
    return persist(config, save, context);
}

}  // namespace

Response handlePost(const Request& request, SaveCallback save, void* context) {
    if (!request.route) return error(404, "not found");
    if (!sameOriginOrNonBrowser(request)) return error(403, "cross-origin request rejected");
    if (std::strcmp(request.route, "/dfu/ble") == 0) {
        if (!emptyActionBody(request))
            return error(400, "BLE DFU request must not include a body");
        return success("{\"status\":\"entering_ble_dfu\",\"advertises_as\":\"bootloader-defined\"}",
                       Transition::BLE_DFU);
    }
    if (std::strcmp(request.route, "/reboot") == 0 ||
        std::strcmp(request.route, "/api/reboot") == 0) {
        if (!emptyActionBody(request))
            return error(400, "reboot request must not include a body");
        return success("{\"status\":\"rebooting\"}", Transition::REBOOT);
    }
    const bool formRoute = std::strcmp(request.route, "/hostname") == 0 ||
        std::strcmp(request.route, "/network") == 0 ||
        std::strcmp(request.route, "/token") == 0 ||
        std::strcmp(request.route, "/auth") == 0 ||
        std::strcmp(request.route, "/gps") == 0;
    if (formRoute && !exactContentType(request, FORM))
        return error(415, "unsupported content type");
    if (std::strcmp(request.route, "/hostname") == 0) return handleHostname(request, save, context);
    if (std::strcmp(request.route, "/network") == 0) return handleNetwork(request, save, context);
    if (std::strcmp(request.route, "/token") == 0) return handleToken(request, save, context);
    if (std::strcmp(request.route, "/auth") == 0) return handleAuth(request, save, context);
    if (std::strcmp(request.route, "/gps") == 0) return handleGps(request, save, context);
    if (std::strcmp(request.route, "/api/config") == 0) {
        if (!exactContentType(request, JSON)) return error(415, "unsupported content type");
        Config config = request.current;
        if (!parseJsonConfig(request, config)) return error(400, "invalid JSON configuration");
        Response response = persist(config, save, context);
        if (response.status == 200) {
            response.body = std::string("{\"status\":\"saved\",\"rebooting\":true,\"config\":{") +
                "\"hostname\":\"" + config.hostname + "\",\"tcp_port\":" +
                std::to_string(config.tcpPort) + ",\"tcp_token_set\":" +
                (config.tcpToken[0] ? "true" : "false") + ",\"gps_enabled\":" +
                (config.gpsEnabled ? "true" : "false") + "}}";
        }
        return response;
    }
    return error(404, "not found");
}

}  // namespace Rak4631WebHandlers
