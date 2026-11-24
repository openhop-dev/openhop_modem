// =============================================================
// tcp_server.h — TCP LoRa-modem protocol server for ESP32 Wi-Fi
// and native-Ethernet targets. One independently authenticated client per
// listener; the port-array index is the owning radio ID.
// =============================================================
#pragma once

#include <Arduino.h>
#include <stdint.h>
#include <IPAddress.h>
#include "response_route.h"

namespace TCPServer {

// ESP32 listener bank, serviced from the cooperative main loop. Count must be
// 1..MAX_RADIOS; ports must be unique, nonzero and not HTTP/OTA port 80. Reject
// invalid definitions or allocation failure before changing an existing bank.
// Arduino's void WiFiServer::begin() retains its platform bind-failure policy.
static constexpr size_t MAX_RADIOS = 4;
// ESP32 output policy: fixed storage per session (four MAX_FRAME_SIZE frames),
// one nonblocking send of at most MAX_FRAME_SIZE bytes per session service.
// Writers try an empty queue immediately; otherwise they retain the complete
// frame/suffix in order. No sleeps or retry-to-drain loops.
static constexpr size_t OUTPUT_QUEUE_FRAMES = 4;
static constexpr uint32_t OUTPUT_CONGESTION_TIMEOUT_MS = 2000;
bool beginRadios(const uint16_t* ports, size_t count, const String& token,
                 RadioCommandHandler handler);
// Deferred commands must validate the complete route before mutating RF. Both
// reply checks require a ready/authenticated client and the exact generation.
// canQueueResponse additionally requires a free maximum-sized output slot.
bool isRouteCurrent(const ResponseRoute& route);
bool canQueueResponse(const ResponseRoute& route);
void writeTo(const ResponseRoute& route, const uint8_t* data, size_t len);
// Unsolicited radio events go only to that radio's ready/authenticated session;
// invalid, unauthenticated and disconnected IDs are dropped, never redirected.
void writeRadioEvent(uint8_t radio, const uint8_t* data, size_t len);
bool isClientReady(uint8_t radio);
String getClientIP(uint8_t radio);

// Optional radio-local parse-error accounting. nullptr restores the external
// legacy noteTransportFrameError callback. The hook persists across restarts.
using RadioFrameErrorHandler = void (*)(uint8_t radio, uint8_t error);
void setRadioFrameErrorHandler(RadioFrameErrorHandler handler);

// Legacy primary-only API. Start (or restart) one listener for radio zero.
// If token.length() > 0, clients must send CMD_AUTH with matching
// payload before any other command is accepted.
void begin(uint16_t port, const String& token);

// Stop accepting connections and drop the current client.
void end();

// Drop only a session bound to an invalidated interface address, not listeners
// or a session on another (e.g. Ethernet) interface. Main-loop context only.
void invalidateInterface(const IPAddress& address);

// Service accepts + bounded incoming/outgoing bytes. Call every loop().
// Input pauses when output is full. Any frame still queued 2000 ms after its
// original enqueue (even with partial progress) disconnects only its session.
// A failed pre-auth command closes input immediately and drains its bounded
// error before disconnect, with the same deadline if the reader stays blocked.
void loop();

// True when a client is connected AND (authenticated OR no token required).
bool isClientReady();

// Nonzero identity of the current ready connection; zero if disconnected or
// unauthenticated. Deferred work must retain this identity, not just TCP.
uint32_t connectionGeneration();
bool isConnectionCurrent(uint32_t generation);

// Whether one complete maximum-sized response fits in transport storage.
// W5100S retains its separate one-slot implementation; ESP32 uses the bounded
// session FIFO above. This is capacity, not a socket-writability prediction.
bool canQueueResponse();

// Dotted quad of the connected client, or empty string when no client.
String getClientIP();

// Primary-only legacy raw writer, including pre-auth compatibility. All ESP32
// writers accept at most MAX_FRAME_SIZE bytes per call. A valid connected
// session that cannot queue the whole write is disconnected (not silently
// dropped); callers holding deferred completions should check canQueueResponse.
// Disconnect/restart clears all queued bytes; stale routes never target a new
// generation. No-op if disconnected; legacy writes cannot reopen failed auth.
void write(const uint8_t* data, size_t len);

} // namespace TCPServer
