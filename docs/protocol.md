# Binary modem protocol

This reference describes the existing v0.8 wire contract, not a v0.8 firmware
release requirement. Command constants and packed structures live in
[protocol.h](../firmware/include/protocol.h); actual board support and handlers
remain authoritative. Probe capabilities rather than comparing version strings.

For a complete connection/recovery lifecycle or a non-openHop adapter, start
with [TCP client integration](tcp-client-integration.md). A TCP port selects the
radio; frames contain neither a radio selector nor a request ID. USB/UART is
primary-radio-only.

## Deferred RF controls

FEM and RX-boost GET/SET requests share a four-entry queue. Setters wait for
reception to finish; queued replies and validation errors retain arrival order.
The existing full-queue exception remains: `ERR_RADIO_BUSY` is returned
immediately when all four slots are occupied.
Queued TCP requests belong to the connection that submitted them. Disconnect,
interface invalidation, or server restart cancels pending work from that
connection without applying it or forwarding its reply to a replacement client.
Canceled entries are reclaimed even while another request is waiting for RX.
USB/UART requests are unaffected. Reconnecting hosts must submit their desired
runtime settings again; no wire-format or NVS behavior changes.

On W5100S, deferred requests also wait until the previous TCP response has
finished sending before applying the next request. This preserves response
order without overflowing the transport's single asynchronous frame buffer.

## Frame format

```
┌──────┬──────┬───────┬──────────┬───────┐
│ SYNC │ CMD  │  LEN  │ PAYLOAD  │  CRC  │
│ 0xAA │ 1B   │ 2B LE │ variable │ 2B LE │
└──────┴──────┴───────┴──────────┴───────┘
CRC-16/CCITT (poly 0x1021, init 0xFFFF) over CMD+LEN+PAYLOAD, excluding SYNC.
```

TX packet data is limited to 255 bytes. An RX frame adds six metadata bytes, so
its frame payload can be 261 bytes. Parse the length incrementally, validate CRC
and keep asynchronous RX/log events separate from command replies.

## Host to modem

| CMD | Name | Payload |
| ------ | ------------------- | --------------------------------------- |
| 0x01 | TX_REQUEST | Raw LoRa data (1–255 B) |
| 0x10 | SET_CONFIG | `RadioConfig` (14 B) |
| 0x11 | GET_CONFIG | — |
| 0x14 | GET_RF_CAPS | — (v0.8) |
| 0x16 | SET_AGC_INTERVAL | uint16 LE seconds, 0 disables (v0.8) |
| 0x17 | GET_AGC_INTERVAL | — (v0.8) |
| 0x19 | SET_FEM_STATE | apply mask + value mask (v0.8) |
| 0x1A | GET_FEM_STATE | — (v0.8) |
| 0x1C | SET_RX_BOOST | 1 B: 0 = power-saving, 1 = boosted |
| 0x1D | GET_RX_BOOST | — (v0.8) |
| 0x20 | STATUS_REQ | — |
| 0x22 | NOISE_REQ | — |
| 0x30 | CAD_REQUEST | — (Listen Before Talk) |
| 0x31 | RX_START | — (restart RX continuous mode) |
| 0x34 | SET_CAD_PARAMS | 4 B: symNum / detPeak / detMin / exit |
| 0x40 | RADIO_STANDBY | — (v0.7; chip → standby, frees the bus) |
| 0x41 | SET_WIFI | ssid+pass+port+token (variable) |
| 0x42 | RADIO_RESUME | — (v0.7; chip → RX continuous) |
| 0x48 | SET_DISPLAY_NAME | display-name bytes; primary owner only |
| 0x4A | SET_AUTO_CAD | 1 B: enable/disable auto-CAD before TX |
| 0x50 | AUTH | token bytes (TCP only) |
| 0x60 | WIFI_RESET | — |
| 0x61 | GET_WIFI | — |
| 0x70 | GET_VERSION | — |
| 0x72 | GET_DEBUG | — (reset reason / heap / max-loop time) |
| 0x74 | ENTER_BOOTLOADER | — (v0.7; nRF52 → Adafruit DFU) |
| 0x90 | OTA_BEGIN | declared size + sha256; unsupported |
| 0x92 | OTA_CHUNK | declared offset + data; no staging writer |
| 0x94 | OTA_VERIFY | — (v0.7) |
| 0x96 | OTA_APPLY | reserved; no activation implemented |
| 0x98 | OTA_ABORT | — (v0.7) |
| 0xFF | PING | — |

v0.8 frame commands probe and set the AGC reset interval on every radio board, the FEM RX LNA on Heltec V4.3 and Station G3, the FEM TX PA on Station G3, and SX1262 boosted RX gain on every radio board; other boards report those bits clear, reject the matching set with `ERR_UNSUPPORTED` (`0x0F`), and these frame writes are not stored in NVS.

The `CMD_OTA_*` constants describe reserved compatibility commands, not an
implemented update path. The current device dispatcher returns unsupported or
failure statuses for BEGIN/CHUNK/VERIFY/APPLY; ABORT returns PONG without staging
an image. Do not use this table as evidence of a working in-app flash writer.
Use board-supported HTTP OTA on ESP32 or the matching nRF52 bootloader flow in
[INSTALL.md](../INSTALL.md#1d-ota-updates-after-the-first-flash).

The RAK4631 Ethernet target additionally has no HTTP `/update` activation route.
Its supported remote update path is Bluetooth DFU with `firmware.zip`; keep USB
DFU available for recovery. Safe staged activation would require a separately
verified bootloader design.

## Modem to host

| CMD | Name | Payload |
| ------ | ------------------- | --------------------------------------- |
| 0x02 | TX_DONE | `airtime_us` (4 B LE) |
| 0x03 | TX_FAIL | — |
| 0x04 | RX_PACKET | RSSI(2) + SNR(2) + sigRSSI(2) + data |
| 0x12 | CONFIG_RESP | `RadioConfig` (14 B) |
| 0x15 | RF_CAPS_RESP | uint32 LE capability bits (v0.8) |
| 0x18 | AGC_INTERVAL_RESP | uint16 LE effective seconds (v0.8) |
| 0x1B | FEM_STATE_RESP | capability mask + value mask (v0.8) |
| 0x1E | RX_BOOST_RESP | 1 B: 0 = power-saving, 1 = boosted |
| 0x21 | STATUS_RESP | `StatusResp` (26 B) |
| 0x23 | NOISE_RESP | int16 LE (dBm × 10) |
| 0x32 | CAD_RESP | 1 B (0=clear, 1=busy) |
| 0x33 | RX_STARTED | — |
| 0x35 | CAD_PARAMS_RESP | echoes the 4-byte config |
| 0x44 | RADIO_STANDBY_RESP | 1 B status: 0=ok, 1=fail |
| 0x46 | RADIO_RESUME_RESP | 1 B status: 0=ok, 1=fail |
| 0x49 | SET_DISPLAY_NAME_RESP | 1 B status |
| 0x4B | SET_AUTO_CAD_RESP | 1 B status: 0=ok |
| 0x51 | AUTH_OK | — |
| 0x62 | WIFI_STATUS | mode + ip + port + ssid + hostname |
| 0x71 | VERSION_RESP | ASCII version string |
| 0x73 | DEBUG_RESP | reset(1) + uptime_ms(4) + heap(4) + min_heap(4) + max_loop_us(4) |
| 0x80 | LOG_MSG | async log line (v0.7; level + utf-8) |
| 0x91 | OTA_BEGIN_RESP | 1 B status; 3=unsupported |
| 0x93 | OTA_CHUNK_RESP | 1 B status; no active staging session |
| 0x95 | OTA_VERIFY_RESP | status(1) + hash(32); current handler fails |
| 0x97 | OTA_APPLY_RESP | 1 B status; current handler fails |
| 0xFE | ERROR | error code (1 B; `0x0B` = `ERR_NO_RADIO`, `0x0F` = `ERR_UNSUPPORTED`, `0x10` = `ERR_RADIO_FAULT`) |
| 0xFF | PONG | — |

`TX_FAIL` remains defined for compatibility; the current shared runtime reports
RF failures through `ERROR`. `TX_DONE` means local completion, not a peer ACK.
`RADIO_RESUME` clears intentional standby; `RX_START` alone does not.

## Default radio parameters

The common receive preset uses 869.618 MHz, 62.5 kHz bandwidth, SF8, CR4/8,
syncword `0x12` and a 16-symbol preamble. Boards may override the boot receive
frequency; Duo declares 915 MHz for its fitted band. The nominal configuration
power field is 22 dBm, with board-specific chip-drive limits applied separately.
A receive preset is not TX authorization. Explicit legal `SET_CONFIG` must
succeed before transmitting. Do not assume one global frequency or PA output
for all boards.

The shared runtime uses explicit LoRa headers, enabled PHY CRC, standard
(non-inverted) IQ and automatic LDRO. These PHY choices are not additional
host-selectable controls. See [RadioConfig](../firmware/include/protocol.h) and
[the runtime application path](../firmware/src/radio_runtime.cpp) for the exact
packed configuration and setter behavior.
