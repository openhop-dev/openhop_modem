// =============================================================
// tcp_server.h — TCP LoRa-modem protocol server (Wi-Fi STA)
// Accepts one client per listener; optional shared-token auth is
// required before non-AUTH commands are processed.
// =============================================================
#pragma once

#include <Arduino.h>
#include <stdint.h>

namespace TCPServer {

constexpr uint8_t MAX_TCP_CLIENTS = 4;

// Start (or restart) the server. Call after WiFi STA is up. Returns false
// when the requested base port cannot accommodate every listener slot.
// If token.length() > 0, clients must send CMD_AUTH with matching
// payload before any other command is accepted.
bool begin(uint16_t port, const String& token);

// Stop accepting connections and drop the current client.
void end();

// Service accepts + incoming bytes. Call every loop().
void loop();

// True when a client is connected AND (authenticated OR no token required).
bool isClientReady();

// Current command slot while dispatching, otherwise the first ready slot
// (or first connected slot when none are ready), or 0xFF when no client exists.
uint8_t activeSlot();
bool isSlotReady(uint8_t slot);
uint32_t getSlotGeneration(uint8_t slot);

// Dotted quad of the connected client, or empty string when no client.
String getClientIP();
String getSlotIP(uint8_t slot);

// Queue bytes to the current command client, or all ready clients otherwise.
void write(const uint8_t* data, size_t len);
void writeToSlot(uint8_t slot, const uint8_t* data, size_t len);
void writeToReadySlots(const uint8_t* data, size_t len);

// Returns the listener slot associated with the current frame callback.
uint8_t currentCommandSlot();

} // namespace TCPServer
