#pragma once

#include "rak4631_config.h"

#include <cstddef>
#include <cstdint>
#include <string>

namespace Rak4631WebHandlers {

enum class Transition : uint8_t { NONE = 0, REBOOT, BLE_DFU };

struct Request {
    const char* route = nullptr;
    const char* contentType = nullptr;
    const char* body = nullptr;
    size_t bodyLength = 0;
    const char* origin = nullptr;
    const char* host = nullptr;
    Rak4631Config::Config current{};
    bool gpsSupported = false;
};

struct Response {
    int status = 500;
    std::string contentType = "application/json; charset=utf-8";
    std::string body = "{\"error\":\"internal error\"}";
    Transition transition = Transition::NONE;
};

using SaveCallback = bool (*)(const Rak4631Config::Config& config, void* context);

Response handlePost(const Request& request, SaveCallback save, void* context);

}  // namespace Rak4631WebHandlers
