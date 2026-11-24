#pragma once
#include "../socket_port.h"
// Match Arduino/lwIP's public send boundary, preserving MSG_DONTWAIT.
#define send radioSocketSend
