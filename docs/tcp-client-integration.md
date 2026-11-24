# TCP clients and independent applications

openHop Modem is **not restricted to openHop Core clients**. Its TCP interface
controls a packet radio and carries opaque LoRa packet bytes. Another application's
radio backend can implement this interface without using openHop Core, provided
that application's on-air requirements fit the modem's supported PHY.

This guide describes the shared multi-radio development backend. Check the
installed board profile and firmware: source documentation does not make a
matching binary available or add a second radio to a single-radio board.

## One Duo, two applications

A Duo with both modules fitted and enabled can be assigned like this:

```text
openHop application + Core ── TCP duo.example:5055 ── RF1 / owner 0
Other application's adapter ─ TCP duo.example:5056 ── RF2 / owner 1
```

`duo.example` is a placeholder for the same modem's actual hostname or IP.
The other application could be a custom LoRa telemetry service or an adapted
mesh stack. This is a topology example, not a claim that unmodified third-party
software already supports the interface.

| Endpoint | Radio | Example controller |
|---|---|---|
| Same modem, TCP 5055 | RF1, permanent ID 0 | openHop Core |
| Same modem, TCP 5056 | RF2, permanent ID 1 | A different compatible application |

The applications may run on the same computer or different hosts. One application
may also open both endpoints. Either way, there is **one controlling client per
endpoint**, not a subscription/broadcast service for multiple TCP consumers of
one radio. Coordinate ownership; do not probe an occupied port as a takeover
mechanism.

RF2 is optional and disabled on first use. An operator must verify that the
second module and antenna/load are fitted, then enable it in **Settings →
Save & Reboot**. This reboots the whole modem and interrupts both applications.
Ports and owner IDs do not compact when a slot is disabled. A declared `present`
slot is not proof that a module is physically installed; inspect `enabled`,
`ready` and `tx_ready` separately through [GET /api/radios](../API.md#get-apiradios).

Each endpoint controls its own LoRa settings and can use a different supported
profile. Peer devices must match that profile, the fitted RF band and applicable
regional rules. Two software configurations do not make a 915 MHz module a
universal-band radio.

## What is independent, and what is shared?

**Per radio/endpoint:** RF configuration and readiness, RX packets, counters,
TX/CAD/backoff state, parser, authentication state, controlling client IP and
connection generation. Radio commands go to the owner selected by the TCP port;
replies and RX events do not migrate to the other port.

**Device-wide:** MCU, network address/interface, HTTP management, persistent
network/security configuration, configured TCP token, reboot and OTA. Both
listeners use the same configured token, but each connection must authenticate
separately. The HTTP password is a different credential and is not TCP AUTH.
Separate ports are not an authorization boundary between mutually untrusted
applications: clients can reach shared device commands. A reboot or update affects
both radios.

USB/UART remains primary-radio-only and does not expose RF2 or a radio-selector
command. USB and TCP access to RF1 are not two independent radios; controllers
must coordinate rather than competing for the same owner's state.

The modem does not bridge packets between RF1 and RF2, merge their traffic or
choose an application for a packet. Forwarding, routing, application encryption,
retries and mesh/network roles belong to the consuming host applications.

## The protocol an adapter must implement

This is a **binary framed modem protocol**, not HTTP/JSON, a Meshtastic protobuf
API, KISS/RNode framing, a byte-for-byte serial tunnel, remote SPI or an I/Q stream.
Opening a TCP socket alone does not make another application's radio driver
compatible. [API.md](../API.md) documents the separate HTTP management surface.

The wire authority is [protocol.h](../firmware/include/protocol.h); the
[README command reference](../README.md#wire-protocol-v08) gives the broader list.
The existing frame has no radio ID or request ID: the socket selects the radio.

```text
SYNC 0xAA | CMD (1 byte) | LEN (uint16 little-endian) | PAYLOAD | CRC (uint16 LE)
```

CRC-16/CCITT uses polynomial `0x1021`, initial value `0xFFFF`, over
`CMD + LEN + PAYLOAD`, excluding SYNC. TCP reads can split a frame anywhere or
combine several frames. Buffer incrementally, enforce bounded lengths and check
CRC; never assume one `recv()` is one frame or treat `0xAA` inside a payload as a
new frame.

A zero-payload PING frame is `AA FF 00 00 FF 03`. If a token is configured,
authenticate first; sending PING before AUTH fails authentication.

### Minimum useful command set

| Request/event | Command | Expected reply or handling |
|---|---|---|
| `CMD_AUTH` | `0x50` | Raw configured token bytes; `CMD_AUTH_OK` (`0x51`). Required before other commands when a token is set; a failed attempt closes that session. |
| `CMD_PING` | `0xFF` | `CMD_PONG` (`0xFF`); transport health, not RF qualification. |
| `CMD_GET_VERSION` | `0x70` | `CMD_VERSION_RESP` (`0x71`); identify firmware, not a substitute for capability probes. |
| `CMD_GET_CONFIG` | `0x11` | `CMD_CONFIG_RESP` (`0x12`); read this radio's current configuration. |
| `CMD_SET_CONFIG` | `0x10` | Packed 14-byte `RadioConfig`; successful `CMD_CONFIG_RESP` (`0x12`) establishes explicit host TX configuration. |
| `CMD_GET_RF_CAPS` | `0x14` | `CMD_RF_CAPS_RESP` (`0x15`); probe optional RF controls instead of guessing from a version. |
| `CMD_TX_REQUEST` | `0x01` | Raw LoRa packet, 1–255 bytes; wait for `CMD_TX_DONE` (`0x02`), `CMD_TX_FAIL` (`0x03`) or `CMD_ERROR` (`0xFE`). |
| `CMD_RX_PACKET` | `0x04` | Unsolicited RX event; six metadata bytes precede the actual LoRa packet. |
| `CMD_STATUS_REQ` | `0x20` | `CMD_STATUS_RESP` (`0x21`); per-radio counters/state plus shared telemetry. |
| `CMD_CAD_REQUEST` | `0x30` | `CMD_CAD_RESP` (`0x32`); clear/busy, or an error. |
| `CMD_RX_START` | `0x31` | `CMD_RX_STARTED` (`0x33`); does not clear intentional standby. |
| `CMD_RADIO_RESUME` | `0x42` | `CMD_RADIO_RESUME_RESP` (`0x46`), status zero on success; clear intentional standby and reapply the existing profile. |

Handle `CMD_ERROR` and busy/channel-busy/invalid-config/fault responses explicitly.
`CMD_TX_FAIL` remains defined, but the current shared runtime reports RF failures
through `CMD_ERROR`; waiting only for `0x03` would miss them.
Do not pipeline unbounded mutations. The simplest adapter serializes commands on
each socket while continuing to process unsolicited events. There is no request
ID to disambiguate arbitrary concurrent same-command requests.

`RadioConfig` is packed little-endian: frequency Hz (`uint32`), bandwidth Hz
(`uint32`), SF (`uint8`), CR (`uint8`, 5–8 meaning 4/5–4/8), chip drive dBm
(`int8`), syncword (`uint16`) and preamble length (`uint8`). The equivalent Python
struct format is `<IIBBbHB`, size 14. Use legal, board-appropriate values; do not
send a native padded C struct. Requested chip drive is not calibrated external
PA output/EIRP.

`CMD_RX_PACKET` metadata is RSSI (`int16 LE`, dBm), SNR (`int16 LE`, dB × 10)
and signal RSSI (`int16 LE`, dBm), followed by the packet bytes. The current
shared runtime fills the signal-RSSI field from the same RSSI sample. A maximum
RX frame payload is **261 bytes**: 255 LoRa bytes plus six metadata bytes. Do not
apply the 255-byte TX-packet ceiling to all received frame payloads or pass the
metadata into the application's on-air packet decoder.

`CMD_TX_DONE` contains a four-byte little-endian airtime in microseconds. It
reports local radio completion, **not** acknowledgment or decoding by a distant
peer. A successful TCP write only enqueues bytes; it is not TX completion either.

## Connection and recovery lifecycle

1. Confirm that the chosen physical slot is fitted, enabled, ready and available
   to this controller. Discover status through the management API if needed;
   do not assume the primary-only legacy status fields describe RF2.
2. Connect to that slot's fixed port and send AUTH first when required. Keep
   token values out of logs; TCP framing/authentication does not provide TLS.
   Existing LAN source filtering still applies to non-openHop clients.
3. Identify the firmware, read the current profile and probe needed capabilities.
   Handle an unsupported optional command without treating it as universal
   feature support or losing framing synchronization.
4. Apply the application's desired legal `SET_CONFIG` and await success before
   transmitting. Duo boots receive-only; a boot preset, `GET_CONFIG`, PING,
   `RX_START` or `RADIO_RESUME` is not a substitute for explicit TX configuration.
5. If entering from intentional standby, use `RADIO_RESUME` and check its success
   status to restore listening. `RX_START` alone does not clear that state.
6. Continuously drain/parse RX and other asynchronous frames, even while waiting
   for a command reply or TX result. A slow reader can fill the bounded output
   queue and cause only that TCP session to be disconnected.
7. Reconnect with bounded backoff, authenticate again and read/reapply desired
   runtime settings. Disconnect does not reboot the radio or guarantee that its
   previous owner's profile/standby state was cleared.
8. Treat a disconnect during TX as an uncertain delivery result. Firmware may
   finish a packet already on air but will not send its stale reply to a new
   connection. Do not blindly replay a transmit; retries/deduplication are the
   host protocol's responsibility. Other radio endpoints keep their own sessions.

Keep per-endpoint sockets, parsers, pending replies, configuration and recovery
state separate in an adapter managing both radios. Do not use a global
"currently selected radio" or reconnect RF1 simply because RF2 disconnects.

## Compatibility and physical limits

Changing the consuming application's radio backend can be sufficient when its
packet format and PHY fit the exposed controls. It is **not** a promise that any
LoRa application works unchanged. The current runtime uses LoRa with explicit
headers, PHY CRC enabled, non-inverted IQ and automatic LDRO. Frequency,
bandwidth, spreading factor, coding rate, syncword, preamble and chip drive are
configurable; arbitrary chip-register access, implicit-header selection, IQ
inversion, FSK and application-specific timing features are not exposed by this
wire contract. Requirements outside it need separately scoped firmware work,
not an invented host command or an unsupported compatibility claim.

Each SX1262 is half-duplex. Independent owner state and ports do not guarantee
RF isolation, safe overlapping maximum-power TX, receiver performance while the
peer transmits, unlimited throughput or independent power/reboot domains. Both
applications must account for antennas, fitted bands, supply budget, duty cycle
and regulatory/EIRP limits. Qualification of an external application and its
on-air peers is separate from endpoint isolation.

The bounded development acceptance proved two real client hosts on Duo,
port-assignment swaps and independent disconnects, plus sequential RF probes.
It did not certify an arbitrary third-party stack or overlapping dual TX. See
[multi-radio validation boundaries](multi-radio.md#validation-boundaries).

## Source map and agent handoff

| Source | Integration authority |
|---|---|
| [protocol.h](../firmware/include/protocol.h) | Command/error values, packed structures, CRC and payload limits |
| [frame_parser.cpp](../firmware/src/frame_parser.cpp) | Incremental frame acceptance and CRC/length checks |
| [tcp_server.cpp](../firmware/src/tcp_server.cpp) | Per-port client/authentication, bounded input/output and session lifetime |
| [radio_runtime.cpp](../firmware/src/radio_runtime.cpp) | Opaque packet TX/RX, PHY configuration, readiness and RF completion |
| [main.cpp](../firmware/src/main.cpp) | Dispatch/reply/event routing and shared device commands |
| [ethermesh_duo.h](../firmware/include/boards/ethermesh_duo.h) | Permanent RF1/RF2 ports, IDs and first-use selection |

This source map and integration checklist are also the handoff for agents
adapting another host application. Keep that adapter in the consuming project;
openHop Core's `TCPLoRaRadio` is an existing client, not a required dependency for
all clients.
For an integration handoff, record the exact firmware/source/profile, consuming
application/adapter revision, endpoint assignments, supported PHY, ownership and
reconnect behavior, and actually observed peer RX/TX results. Do not report a
socket connection or PING as proof that the other application's RF/network
protocol works.
