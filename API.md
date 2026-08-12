# HTTP API

This firmware exposes a small HTTP API alongside the web UI.

All API routes:
- are LAN-only
- use the same HTTP Basic Auth as the web UI
- use username `admin`
- return JSON on success

Base URL examples:
- `http://192.168.1.42`
- `http://heltec-ab12cd.local`

Example auth:

```bash
curl -u admin:YOUR_PASSWORD http://192.168.1.42/api/stats
```

## Endpoints

### `GET /api/temp`

Returns only the die temperature plus basic identity.

Example:

```json
{
  "die_temperature_c": 24,
  "firmware": "v0.8.0-heltec_v3",
  "hostname": "heltec-ab12cd"
}
```

### `GET /api/system`

Returns basic device identity and live host connection info.

Example:

```json
{
  "board": "Heltec WiFi LoRa 32 V3",
  "firmware": "v0.8.0-heltec_v3",
  "hostname": "heltec-ab12cd",
  "mdns": "heltec-ab12cd.local",
  "interface": "Wi-Fi",
  "current_ip": "192.168.1.42",
  "connected_client_ip": "192.168.1.10",
  "uptime_sec": 42,
  "uptime": "00:00:42",
  "die_temperature_c": 24
}
```

### `GET /api/radio`

Returns the current live radio settings.

Example:

```json
{
  "state": "RX/Idle",
  "standby": false,
  "auto_cad_enabled": false,
  "frequency_hz": 910525000,
  "frequency_mhz": 910.525,
  "bandwidth_hz": 62500,
  "bandwidth_khz": 62.5,
  "spreading_factor": 7,
  "coding_rate": 5,
  "tx_power_dbm": 22,
  "syncword": "0x3444",
  "syncword_value": 13380,
  "preamble_len": 17
}
```

### `GET /api/network`

Returns live network status plus the saved network configuration.

Example:

```json
{
  "mode": "static",
  "use_static_ip": true,
  "interface": "Wi-Fi",
  "live": true,
  "current_ip": "192.168.1.42",
  "subnet": "255.255.255.0",
  "gateway": "192.168.1.1",
  "dns1": "1.1.1.1",
  "dns2": "8.8.8.8",
  "tcp_port": 5055,
  "pymc_token_set": true,
  "saved": {
    "static_ip": "192.168.1.42",
    "subnet": "255.255.255.0",
    "gateway": "192.168.1.1",
    "dns1": "1.1.1.1",
    "dns2": "8.8.8.8"
  }
}
```

### `GET /api/stats`

Returns the combined system, radio, counters, and network state in one response.

Top-level keys:
- `system` — board, firmware, hostname, uptime, die temperature, and battery voltage only when the board variant defines battery sensing (`battery_voltage_mv`, `battery_voltage_v`; otherwise `null`)
- `radio` — live/on-air radio settings. In TCP multiplex mode it also contains `multiplexed`, `active_slot`, and `slots`; `slots` is ordered by slot number and contains only connected, authorized TCP clients
- `counters`
- `network`

`counters.suppressed_rx` is the number of exact copies of the most recently
transmitted packet discarded during the configured TX echo suppression window.
Suppressed packets are not included in `counters.rx_packets` and are not
sent to radio slots.

In multiplex mode, each `radio.slots[]` entry contains the TCP slot identity and
all per-slot radio/CAD settings in a stable order:

```json
{
  "slot": 0,
  "port": 5055,
  "active": true,
  "on_air": true,
  "standby": false,
  "client_ip": "192.168.1.10",
  "recieve_mirroing": [1],
  "auto_cad_enabled": false,
  "cad_custom": true,
  "cad_sym_num": 1,
  "cad_det_peak": 22,
  "cad_det_min": 10,
  "cad_exit_mode": 0,
  "frequency_hz": 869618000,
  "frequency_mhz": 869.618,
  "bandwidth_hz": 62500,
  "bandwidth_khz": 62.5,
  "spreading_factor": 8,
  "coding_rate": 8,
  "tx_power_dbm": 22,
  "syncword": "0x12",
  "syncword_value": 18,
  "preamble_len": 16
}
```

`radio.slots[].recieve_mirroing` lists the other connected, non-standby slots
that can receive the same packets as this slot. Matching compares only receive
parameters: frequency, bandwidth, spreading factor, coding rate, and syncword.
CAD settings, TX power, preamble length, status, TCP port, and client identity
are ignored. Each packet/profile pair is delivered at most once to each ready
slot during the duplicate-suppression window, so a client echo cannot cause the
same packet to be mirrored back and forth. The field is an empty array when no
other slot mirrors the receive profile.

`radio.active_slot` is the slot currently selected by the receive scheduler,
or `null` while no slot is on air. When no TCP client is connected, `radio.slots` is an empty array and the
existing live radio fields remain unchanged.

### `GET /api/config`

Returns the saved editable configuration.

Example:

```json
{
  "hostname": "heltec-ab12cd",
  "effective_hostname": "heltec-ab12cd",
  "tcp_token": "your-token",
  "tcp_port": 5055,
  "rx_slot_ms": 100,
  "activity_hold_ms": 2000,
  "tx_echo_hold_multiplier": 1,
  "use_static_ip": true,
  "static_ip": "192.168.1.42",
  "subnet": "255.255.255.0",
  "gateway": "192.168.1.1",
  "dns1": "1.1.1.1",
  "dns2": "8.8.8.8"
}
```

### `POST /api/config`

Updates saved config and reboots the modem.

Accepted top-level fields:
- `hostname`
- `tcp_token`
- `tcp_port`
- `rx_slot_ms`
- `activity_hold_ms`
- `tx_echo_hold_multiplier`
- `use_static_ip`
- `network`

Multiplexing fields:
- `rx_slot_ms` — receive-slot dwell in milliseconds; minimum `5`
- `activity_hold_ms` — how long to retain a slot after CAD detects activity or
  an RF packet is received; queued TX requests wait for the hold to expire, and
  `0` disables the hold
- `tx_echo_hold_multiplier` — suppress an exact RF copy of the most recently
  completed TX for `activity_hold_ms × tx_echo_hold_multiplier`; default `1`,
  and `0` disables TX reflection suppression

`network` fields:
- `use_static_ip`
- `static_ip`
- `subnet`
- `gateway`
- `dns1`
- `dns2`

Notes:
- fields you omit are left unchanged
- set `hostname` to `""` to reset to the default MAC-derived hostname
- set `tcp_token` to `""` to clear the openHop token
- if `use_static_ip` is `true`, `static_ip`, `subnet`, and `gateway` must be valid
- a successful request always reboots the modem

Example:

```bash
curl -u admin:YOUR_PASSWORD \
  -H 'Content-Type: application/json' \
  -d '{
    "hostname": "heltec-ab12cd",
    "tcp_token": "meshpass",
    "rx_slot_ms": 100,
    "activity_hold_ms": 2000,
    "tx_echo_hold_multiplier": 1,
    "network": {
      "use_static_ip": true,
      "static_ip": "192.168.1.42",
      "subnet": "255.255.255.0",
      "gateway": "192.168.1.1",
      "dns1": "1.1.1.1",
      "dns2": "8.8.8.8"
    }
  }' \
  http://192.168.1.42/api/config
```

Success response:

```json
{
  "status": "saved",
  "rebooting": true,
  "config": {
    "...": "updated values"
  }
}
```

Error response:

```json
{
  "error": "message here"
}
```

### `POST /api/reboot`

Reboots the modem immediately.

Example:

```bash
curl -u admin:YOUR_PASSWORD -X POST http://192.168.1.42/api/reboot
```

Response:

```json
{
  "status": "rebooting"
}
```
