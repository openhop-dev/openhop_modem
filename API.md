# HTTP API

This firmware exposes a small HTTP API alongside the web UI.

ESP32 builds expose every endpoint documented below. The RAK4631 W5100S
nRF52 build intentionally keeps the HTTP implementation small and exposes
only `GET /api/stats`; configuration is handled by the HTML forms on `/`.
It does not provide network firmware upload.

All available API routes:
- are LAN-only
- use the same HTTP Basic Auth as the web UI
- use username `admin`
- return JSON on success

Base URL examples:
- `http://192.168.1.42`
- `http://heltec-ab12cd.local` (mDNS-capable ESP32 builds)

The RAK4631 hostname is status-only; use its IPv4 address.

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
On RAK4631 this is the only JSON endpoint and uses a compact nRF52-specific
shape with `board`, `firmware`, `uptime_sec`, `network`, and `radio` keys.

ESP32 `/api/stats` top-level keys:
- `system` — board, firmware, hostname, uptime, die temperature, and battery voltage only when the board variant defines battery sensing (`battery_voltage_mv`, `battery_voltage_v`; otherwise `null`)
- `radio`
- `counters`
- `network`

### `GET /api/config`

Returns the saved editable configuration.

Example:

```json
{
  "hostname": "heltec-ab12cd",
  "effective_hostname": "heltec-ab12cd",
  "tcp_token": "your-token",
  "tcp_port": 5055,
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
- `use_static_ip`
- `network`

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
