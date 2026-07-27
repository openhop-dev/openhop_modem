#pragma once

#include "http_request_parser.h"

namespace W5100sHttpServer {

enum class RouteAction : unsigned char {
    ROOT_HTML = 0,
    STATS_HTML,
    TEMP_JSON,
    SYSTEM_JSON,
    RADIO_JSON,
    NETWORK_JSON,
    STATS_JSON,
    CONFIG_JSON,
    GPS_JSON,
    MANAGEMENT_POST,
    NOT_FOUND,
};

RouteAction classifyRoute(HttpRequest::Method method, const char* normalizedRoute);
void begin();
void loop();
void end();

}  // namespace W5100sHttpServer
