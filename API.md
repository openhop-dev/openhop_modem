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

Returns basic device identity and live host connection info. Station G3 also
includes its onboard INA219 power-monitor state and readings. INA219 values are
`null` when the monitor is not detected or its latest read failed.

Example:

```json
{
  "board": "BQ Voyage Station G3",
  "firmware": "v1.0.1-station_g3",
  "hostname": "station-g3-ab12cd",
  "mdns": "station-g3-ab12cd.local",
  "interface": "Wi-Fi",
  "current_ip": "192.168.1.42",
  "connected_client_ip": "192.168.1.10",
  "uptime_sec": 42,
  "uptime": "00:00:42",
  "die_temperature_c": 24,
  "station_g3_power_monitor_available": true,
  "station_g3_input_voltage_v": 12.412,
  "station_g3_current_ma": 146.3,
  "station_g3_power_w": 1.816,
  "station_g3_minimum_input_voltage_v": 12.287,
  "station_g3_maximum_current_ma": 891.2
}
```

### `GET /api/radio`

Returns the current live radio settings.

Station G3 example:

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
  "tx_power_dbm": 19,
  "syncword": "0x3444",
  "syncword_value": 13380,
  "preamble_len": 17,
  "pa_high_power_enabled": false,
  "station_g3_external_lna_enabled": true
}
```

`pa_high_power_enabled` and `station_g3_external_lna_enabled` are Station G3-only.
The LNA setting controls receive mode; firmware always bypasses the LNA before TX.

### `GET /api/radios`

On ESP32 network builds, returns one entry per declared radio slot, including
disabled or failed owners.
Each entry has `id`, `label`, `tcp_port`, `present`, `enabled`, `ready`,
`tx_ready`, `connected_client_ip`, `radio` and `counters`. `present` describes
board capability, not physical module detection. IDs and ports never compact
when a slot is disabled. A single-radio board returns one entry.

`connected_client_ip` belongs to that endpoint's current TCP client, not a
device-wide last-client value. Different hosts can own different radio ports;
each endpoint authenticates independently. For example, the client-identity
fields of a two-radio response can be:

```json
[
  {"id": 0, "label": "RF1", "tcp_port": 5055, "connected_client_ip": "192.0.2.10"},
  {"id": 1, "label": "RF2", "tcp_port": 5056, "connected_client_ip": "192.0.2.20"}
]
```

This excerpt omits the readiness, radio and counter fields. A disconnected
endpoint reports `null`; disconnecting it does not transfer its requests,
replies or identity to the other radio or a replacement client.

Multi-radio `/api/stats` also adds `radios`. Existing scalar `radio`, `counters`
and connection projections remain primary-radio views, not aggregates. Fixed
board-declared ports cannot be changed by saving a conflicting scalar port.

### `POST /api/radios`

On a supported, explicitly opted-in ESP32 multi-radio board, saves the next
boot's enabled selection. The object must contain only `enabled`, with exactly
one Boolean per declared slot. A four-slot example is:

```json
{"enabled":[true,false,true,true]}
```

An all-false selection is valid. A successful checked save returns a saved/
rebooting JSON acknowledgment and schedules reboot; it does not mutate live
owners before reboot. Invalid schemas, unsupported boards and failed storage
operations do not change the active selection or reboot. Use this endpoint,
not ignored fields on `/api/config`, for radio selection.

The native Settings form posts to `/radios`. Its client makes one save, then
bounded authenticated readiness reads; it never automatically repeats a save.
Software disable skips radio work but does not remove electrical power.

See [multi-radio behavior and platform limits](docs/multi-radio.md).

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
  "base_gateway": null,
  "dhcp_routes_active": false,
  "dhcp_routes": [],
  "dns1": "1.1.1.1",
  "dns2": "8.8.8.8",
  "tcp_port": 5055,
  "token_set": true,
  "saved": {
    "static_ip": "192.168.1.42",
    "subnet": "255.255.255.0",
    "gateway": "192.168.1.1",
    "dns1": "1.1.1.1",
    "dns2": "8.8.8.8"
  }
}
```

`gateway` is the effective default gateway shown by the WebUI. With valid
classless routes it is the advertised `/0` router, or `null` when no classless
default exists (option 3 is ignored). Without classless routes it is the DHCP
option-3 router, or the configured gateway in static mode. `base_gateway`
exposes the cached option-3 router as diagnostic lease data; it is not a
fallback when option 121 is authoritative. It is `null` before an accepted
DHCP lease and in static-address mode.

`dhcp_routes_active` reports whether valid classless routing is installed for
the current default interface. Option 121 takes precedence over 249; repeated
options concatenate up to 512 bytes per code. Up to eight nondefault routes
plus one default are retained. New accepted leases replace the entire table,
including removal when their ACK omits classless routes. Rejected packets and
OFFERs never publish routes. Malformed/oversized route options on an accepted
lease install no classless table rather than retaining previous-lease routes.
Link/address/admin loss and removal retire only the affected interface.

`dhcp_routes` lists the entries as
`{destination, prefix, gateway}` objects. A DHCP-advertised default
route is included as
`destination: "0.0.0.0"`, `prefix: 0`. The array is empty on static
addresses and on networks without option 121. Example with two routed
prefixes and a default route:

```json
{
  "mode": "dhcp",
  "gateway": "192.168.1.1",
  "base_gateway": "192.168.1.1",
  "dhcp_routes_active": true,
  "dhcp_routes": [
    { "destination": "10.20.0.0", "prefix": 16, "gateway": "192.168.1.10" },
    { "destination": "172.20.0.0", "prefix": 16, "gateway": "192.168.1.10" },
    { "destination": "0.0.0.0", "prefix": 0, "gateway": "192.168.1.1" }
  ]
}
```

### `GET /api/stats`

Returns the combined system, radio, counters, network, and GPS state in one response.

Top-level keys:
- `battery_voltage_mv`, `battery_voltage_v` — battery voltage when the board defines battery sensing; otherwise `null`
- `bus_voltage_v`, `current_ma`, `power_mw` — Station G3 INA219 input-power readings when the monitor is available; values are `null` when its latest sample failed
- `system` — board, firmware, hostname, uptime, die temperature, battery voltage only when the board variant defines battery sensing (`battery_voltage_mv`, `battery_voltage_v`; otherwise `null`), and Station G3-only INA219 power readings
- `radio`
- `counters`
- `network`
- `gps`
- `environment` — Station G2/G3 only; cached external Grove BME280 readings,
  separate from `system.die_temperature_c` and `/api/temp`

Station G2 and G3 probe the Grove I2C bus at `0x76` (the Seeed default), then
`0x77`, and verify the BME280 chip ID. One sensor is supported; `0x76` takes
precedence when both addresses contain working BME280s. Other sensor types are
not detected.

Example `environment` object in `/api/stats`:

```json
{
  "sensor": "bme280",
  "available": true,
  "temperature_c": 22.8,
  "humidity_pct": 54.2,
  "pressure_hpa": 1014.7
}
```

Temperature is in degrees Celsius, humidity in percent relative humidity, and
pressure in hPa (absolute station pressure, not adjusted to sea level).
Measurements are requested approximately every five seconds. HTTP requests use
the cache and do not access the sensor. `available` means a successful complete
sample less than 15 seconds old. Disconnection is detected on the next sensor
transaction; until then the previous sample may still be returned.

Before the first successful sample, after a failed transaction, or when the
cache expires, the object is:

```json
{
  "sensor": null,
  "available": false,
  "temperature_c": null,
  "humidity_pct": null,
  "pressure_hpa": null
}
```

Missing or failed sensors do not prevent startup. Detection is retried every
30 seconds while offline, allowing reconnection without a reboot. This field is
omitted on other boards. Existing `/api/stats` fields and `/api/temp` are unchanged.

Station G2 and G3 periodically reset the SX1262 AGC to mitigate a reported
receiver "deafness" condition: the apparent noise floor can rise by roughly
20–30 dB while packet reception stops until the radio is reinitialized. This
is a time-based workaround, not a noise-floor threshold trigger. It runs only
when RX is idle, calls RadioLib's `resetAGC()`, and resumes continuous receive;
a reset briefly interrupts listening.

On Station G2 and G3, `radio` includes `agc_reset_interval_sec` (default `0`),
`agc_reset_count` (successful reset and RX-restart sequences since boot), and
`last_agc_reset_ms_ago` (`null` until the first successful reset). Compare
these with `counters.noise_floor_dbm`, `counters.rx_packets`, and
`counters.crc_errors` when investigating reception.

Periodic maintenance is disabled by default because each attempt briefly
interrupts listening. Set `agc_reset_interval_sec` to `4` to enable the
recommended four-second recovery interval.

### `GET /api/config`

Returns the saved editable configuration.

Station G3 example:

```json
{
  "hostname": "station-g3-ab12cd",
  "effective_hostname": "station-g3-ab12cd",
  "tcp_token": "your-token",
  "tcp_port": 5055,
  "use_static_ip": true,
  "static_ip": "192.168.1.42",
  "subnet": "255.255.255.0",
  "gateway": "192.168.1.1",
  "dns1": "1.1.1.1",
  "dns2": "8.8.8.8",
  "agc_reset_interval_sec": 0,
  "pa_high_power_enabled": false,
  "station_g3_external_lna_enabled": true
}
```

The PA and Station G3 LNA fields are omitted on unsupported variants.

RAK4631 WisMesh Ethernet security differences:

- `tcp_token` is never returned. The response contains `tcp_token_set` instead.
- When serial GPS support is explicitly compiled in, the response also contains
  `gps_enabled` and `gps_available`. Ordinary RAK builds leave GPS unavailable.
- The HTTP password is never returned on any platform.

### `POST /api/config`

Updates saved configuration. Radio front-end settings, including
`agc_reset_interval_sec`, apply immediately. Changes to network, identity,
Wi-Fi, or GPS settings reboot the modem.

Accepted top-level fields:
- `hostname`
- `tcp_token`
- `tcp_port`
- `use_static_ip`
- `wifi_power_save` — Wi-Fi boards only; `false` disables Wi-Fi modem
  power-save for lower latency at higher power draw. Applies after the
  post-save reboot.
- `agc_reset_interval_sec` — Station G2, Station G3, and Heltec V4.3 only;
  integer seconds from `0` to `3600`, with `0` disabling periodic maintenance.
  The supported boards default to `0`. The setting persists across reboots.
- `network`
- `pa_high_power_enabled` — Station G3 only; `false` selects the lower GPIO9
  mode and `true` selects the higher mode. The setting applies immediately and
  persists across reboots.
- `station_g3_external_lna_enabled` — Station G3 only; enables or bypasses the
  receive-only external LNA on GPIO10. The setting applies immediately,
  persists across reboots, and never enables the LNA during TX.
- `gps_enabled` on RAK builds where GPS support was explicitly compiled in

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
- unsupported variants reject Station G3 PA/LNA fields rather than ignoring them
- the response's `rebooting` field reports whether the submitted changes
  require a reboot
- RAK rejects unsupported Wi-Fi antenna, Heltec LNA, GPIO, and GPS-mode fields
  instead of silently discarding them
- RAK responses report only `tcp_token_set`; they never echo the submitted token

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
curl -u admin:YOUR_PASSWORD -X POST -d '' http://192.168.1.42/api/reboot
```

Response:

```json
{
  "status": "rebooting"
}
```

### `POST /dfu/ble` (RAK4631 only)

Enters the installed Nordic Bluetooth DFU bootloader. The request must have an
empty body and is protected by the same Basic authentication and same-origin
browser checks as other management actions. The transition is not executed until
the complete HTTP response is acknowledged and the W5100S socket is closed.

This endpoint does not accept firmware data and does not enable WebUI/Ethernet
`/update`. Supporting safe staged activation and recovery over Ethernet would
require a custom openHop Modem bootloader, which is not currently planned.
Instead, this working Bluetooth DFU flow is the supported OTA update path for a
deployed gateway: after the RAK disconnects, use a Nordic-compatible DFU app to
select the bootloader's Bluetooth device and upload the nRF52 `firmware.zip`.
The BLE name is defined by the installed bootloader and must not be assumed.

```bash
curl -u admin:YOUR_PASSWORD -X POST -d '' http://192.168.1.42/dfu/ble
```

```json
{
  "status": "entering_ble_dfu",
  "advertises_as": "bootloader-defined"
}
```
