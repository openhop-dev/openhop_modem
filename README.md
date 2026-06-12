# pymc_modem — USB/TCP LoRa Modem for pymc_core

Firmware + Python driver that turns a supported ESP32 or nRF52 board
with an SX1262 front end into a "dumb" LoRa modem controlled from a
Raspberry Pi over USB-CDC, Wi-Fi/TCP, or (on boards with native
Ethernet) wired LAN.

**Supported boards** (one source tree, picked at compile time via
`-DBOARD_<name>` in `platformio.ini`):

| Board                                                                                                       | MCU                          | Front end                  | Networks |
|-------------------------------------------------------------------------------------------------------------|------------------------------|----------------------------|----------|
| **Heltec WiFi LoRa 32 V3**                                                                                  | ESP32-S3                     | bare SX1262                | Wi-Fi    |
| **Heltec WiFi LoRa 32 V4**                                                                                  | ESP32-S3                     | SX1262 + PA/LNA FEM        | Wi-Fi    |
| **Heltec Wireless Tracker V2**                                                                              | ESP32-S3                     | SX1262 + KCT8103L PA/FEM + TFT 160×80 | Wi-Fi |
| **Ikoka Stick** ([ndoo/ikoka-stick-meshtastic-device](https://github.com/ndoo/ikoka-stick-meshtastic-device))| XIAO ESP32-S3                | Ebyte E22-P868M30S, +30 dBm | Wi-Fi   |
| **Seeed XIAO Wio-SX1262**                                                                                   | XIAO ESP32-S3                | bare SX1262                | Wi-Fi    |
| **LilyGO T-LoRa T3-S3** v1.2/v1.3                                                                           | ESP32-S3                     | bare SX1262 + OLED         | Wi-Fi    |
| **RAK3112 WisMesh**                                                                                         | ESP32-S3 (module)            | SX1262 in-module           | Wi-Fi    |
| **WaveShare ESP32-P4-Nano**                                                                                 | ESP32-P4 (RISC-V) + ESP32-C6 | E22 (off-board, optional)  | **Ethernet *or* Wi-Fi** — runtime auto-select: cable plugged → Ethernet wins, no link → fall back to Wi-Fi via C6 SDIO bridge. Both at once is unstable with the radio attached, see [P4-Nano notes](#porting-to-another-esp32-p4-board) |
| **Heltec T114**                                                                                             | nRF52840                     | bare SX1262 + TFT 135×240  | **none** — USB-CDC + UART only |
| **Seeed XIAO nRF52840 + Wio-SX1262**                                                                        | XIAO nRF52840                | bare SX1262                | **none** — USB-CDC only |

Drop-in replacement for `SX1262Radio` in pymc_core — all MeshCore logic
(routing, encryption, retransmission) runs on the RPi. The modem handles
only the SX1262 physical layer: TX, RX, CAD, LoRa parameter configuration.

## Architecture

```
                          USB-CDC / WiFi-TCP
Raspberry Pi                                  pymc_modem modem
┌────────────────────┐                        ┌─────────────────┐
│ pymc_repeater      │◄ USB 921600 ────────►  │ LoRa Modem FW   │
│  └─ pymc_core      │                        │  └─ SX1262      │
│     ├─ USBLoRaRadio│──── OR ──────          │  └─ RadioLib    │
│     └─ TCPLoRaRadio│◄ TCP 5055 ─────────►   │  └─ OLED / TFT  │
│                    │                        │  └─ Wi-Fi STA*  │
└────────────────────┘                        └─────────────────┘
                                              * Wi-Fi on all boards
                                                except T114 (nRF52,
                                                USB-CDC + UART only).
```

- **USB mode** — cable, instant, no provisioning; ideal for single-board setups.
- **Wi-Fi/TCP mode** — no cable; modem can live anywhere on the LAN while the
  Pi sits elsewhere. Provisioned once via on-device AP portal (open AP
  `LoRa-Modem-XXXX` → `http://192.168.4.1`) or over USB with
  `USBLoRaRadio.set_wifi_credentials()`.

## Project layout

- **`firmware/`** — PlatformIO tree, ten envs sharing one source.
  Each board lives in `include/boards/<env>.h`; `platformio.ini` picks
  one via `-DBOARD_<NAME>`. Prebuilt artifacts (ESP32: `bootloader.bin
  / partitions.bin / firmware.bin`; nRF52 T114: `firmware.hex` +
  Adafruit DFU `firmware.zip`) live in `firmware/<env>/`.
- **`pymc_driver/`** — Python drivers `usb_radio.py` / `tcp_radio.py` +
  shared `protocol_constants.py`. **Since 2026-05-13 these ship in
  upstream pyMC_core `dev`** ([PR #68](https://github.com/pyMC-dev/pyMC_core/pull/68));
  newer pymc_core installs pick them up automatically. `test_modem.py`
  is a standalone pyserial probe that runs without pymc_core.
- **`patches/`** — reference copies of files vendored into upstreams,
  kept here so they stay in lockstep with the firmware. Needed only
  for pyMC_core releases that predate PR #68 and for the pending
  pyMC_Repeater `radio_type: pymc_tcp / pymc_usb` branch
  ([pyMC_Repeater #240](https://github.com/pyMC-dev/pyMC_Repeater/pull/240)).
- **`scripts/install.sh`** — one-shot installer; idempotent.
- **`docker/`** + `docker-compose.yml` — Linux container running
  pymc_repeater that talks to the modem over LAN-TCP by default.

## Installation

Native install, Docker deployment, firmware flashing (esptool / PlatformIO /
OTA), Wi-Fi provisioning and the full pymc_core integration steps are
documented in [INSTALL.md](INSTALL.md).

## Firmware asset builds

The `Build Firmware Assets` GitHub workflow uses
`firmware/tools/build_firmware_assets.py` to build PlatformIO envs and stage
flasher-ready outputs in `firmware/<env>/`.  Pull requests do not build
firmware.  Pushes to `main` build affected envs and commit updated
`firmware/<env>/` binaries, manifests, and SHA256 sums back to `main` without
uploading Actions artifacts.  Manual dispatch can build `auto`, `all`, or
specific envs from any branch; manual runs upload Actions artifacts for review
and only commit generated files when `commit_artifacts=true`.

## Per-board pin map

All board-specific GPIOs and policies live in
`firmware/include/boards/<name>.h` — pick the closest existing one
when adding a new carrier and edit the few fields that differ.

Per-board highlights (full pin numbers in the headers, mDNS prefix is
`BoardConfig.mdns_prefix`, hostname `<prefix>-<mac3>.local`):

- **Heltec V3** — onboard SSD1306, bare SX1262, max 22 dBm.
- **Heltec V4** — onboard SSD1306, SX1262 + V4.x PA/LNA front-end, native USB-CDC, max 22 dBm SX1262 command power.
- **Heltec Wireless Tracker V2** — ESP32-S3 + SX1262 + KCT8103L PA/FEM, ST7735 TFT 160×80, native USB-CDC, max 22 dBm SX1262 command power.
- **Ikoka Stick** — XIAO ESP32-S3 + E22-P868M30S, EN-held + DIO2-as-RF-switch, max 30 dBm chip / +10 dB PA, external OLED.
- **XIAO Wio-SX1262** — Seeed XIAO ESP32-S3 + bare SX1262, no OLED.
- **LilyGO T3-S3** — bare SX1262 + onboard SSD1306, native USB-CDC.
- **RAK3112 WisMesh** — SX1262 inside the RAK3112 module, no OLED.
- **WaveShare ESP32-P4-Nano** — RISC-V P4 + C6 + IP101GRI Ethernet PHY + off-board E22, runtime ETH-or-Wi-Fi (never both, see below).
- **Heltec T114** — nRF52840 + bare SX1262 + ST7789 TFT 135×240, **no Wi-Fi/TCP/network OTA**; USB-CDC + UART transport only, OTA via Adafruit nRF52 DFU (USB) or in-app `CMD_OTA_*` over the protocol transport.
- **Seeed XIAO nRF52840 + Wio-SX1262** (SKU 102010710) — XIAO nRF52840 + bare SX1262 on the Wio-SX1262 carrier, BLE 5.0 hardware unused, **no Wi-Fi/TCP/network OTA**, no display; native USB-CDC transport only, OTA via Adafruit nRF52 DFU (UF2 disk on double-click reset) or in-app `CMD_OTA_*`.

### E22-P RF switch (Ikoka, P4-Nano + E22P)

E22P truth table from the datasheet: `EN=1, T/R CTRL=1` → TX,
`EN=1, T/R CTRL=0` → RX, `EN=0` → off. Firmware drives `EN` LOW for
5 s at boot (LDO/PA settle), then HIGH for life. `T/R CTRL` is not
wired to the MCU — the carrier ties it to SX1262 DIO2, and firmware
enables `setDio2AsRfSwitch(true)` so SX1262 toggles it on TX
automatically. A board with two MCU-driven enable lines instead just
sets `rx_pin` / `tx_pin` in `RfSwitchPolicy`.

## Porting to another ESP32-P4 board

The WaveShare ESP32-P4-Nano is the reference; copy
`firmware/include/boards/esp32_p4_nano.h` and adjust pins. Quirks
that differ from the ESP32-S3 family:

- **GPIO35 is the boot strap** (not GPIO0). Many P4 boards wire their
  BOOT button there, but GPIO35 is also RMII TXD1 — when Ethernet is
  up, the EMAC drives it as a 25 MHz output and the button reads the
  bitstream. Set `pin_user_button = -1`; firmware then auto-cycles the
  OLED screens every 4 s instead.
- **High-numbered GPIOs (49+) sit on a separate LDO domain.** RMII
  TX_EN/CLK fall there but work on the Nano; if PHY init fails on a
  different carrier, suspect this first.
- **Wi-Fi (C6 SDIO) + Ethernet (RMII) + radio together is unstable** —
  the C6 esp_hosted bridge falls off the SDIO bus every ~25 s and the
  SoC's RTC watchdog reboots. Fix: leave both `has_wifi = true` and
  `ethernet.enabled = true` in the board header and let `setup()`
  pick at runtime — Ethernet is tried first, EMAC is torn down with
  `EthernetManager::end()` if there's no link, then Wi-Fi takes over.
  Either alone with the radio is fine; both at once isn't.
- **Ethernet** is configured via `BoardConfig::EthernetConfig`
  (MDC, MDIO, RST, addr, clock direction). Static-IP fields are
  optional — leave `use_static_ip = false` for DHCP. Add new PHY
  models by extending the `EthernetPhy` enum + the mapping in
  `ethernet_manager.cpp`.
- **Debug serial** — keep `ARDUINO_USB_CDC_ON_BOOT=0`; the
  pioarduino USB-Serial-JTAG path mangles `Serial.printf` output on
  ESP32-P4. Use the second USB-C port (CH343P → UART0) for printf
  debug, or rely on TCP / `CMD_GET_DEBUG`.
- **OLED is optional** — `pin_i2c_sda = -1` short-circuits the entire
  Wire/SSD1306 path.

## Network exposure: LAN-only by design

On Wi-Fi / Ethernet boards both TCP services — the protocol on 5055
and OTA HTTP on 80 — refuse clients whose source address is outside
RFC1918 (`10/8`, `172.16/12`, `192.168/16`), link-local (`169.254/16`)
or loopback (`127/8`). The check runs at `accept()` time before any
frame parsing or auth. NAT port-forwards / Internet tunnels with a
public source IP are dropped unconditionally — TCP closes the socket,
OTA returns 403. Hard firmware policy; lifting it means editing
`firmware/include/net_filter.h` and re-flashing. Operators who need
remote access run a VPN whose tunnel address (WireGuard / Tailscale
in 100.64/10 → Tailscale subnet route, or any `10/8` overlay) is
inside the LAN range from the modem's point of view.

T114 has no IP stack at all — the only paths in are USB-CDC and the
secondary UART, and updates are either Adafruit DFU over USB or the
in-app `OTA_*` commands carried over the same transport.

### Web UI / OTA / JSON API authentication (v0.8+)

From v0.8 the HTTP surface (web management page, OTA `/update`, and
the `/api/*` JSON endpoints) is gated by HTTP Basic Auth. Defaults on
first boot:

- **user:** `admin`
- **password:** `password`

Change the password via the **Change HTTP password** form in the web
UI; it is persisted in NVS under `http_pass`. ArduinoOTA (espota) uses
the same password as its `--auth` token. Examples:

```bash
# Open the web UI (browser)         → http://<host>/         (admin / password)
# Pull live stats                   → curl -u admin:password http://<host>/api/stats
# Flash a new firmware over HTTP    → curl -u admin:password \
#       -F firmware=@firmware/<env>/firmware.bin http://<host>/update
# Flash over espota via PlatformIO  → pio run -e <env> -t upload \
#       --upload-port <host> --upload-flags="--auth=password"
```

Pre-v0.8 firmware used `heltec:<tcp_token>` on `/update` only — that
scheme is gone, the same credential pair now covers every HTTP path.

## Wire protocol v0.7

*(Full command list in `firmware/include/protocol.h`; the section below is
summarised. Reported firmware version is `v0.8.0-<BoardConfig.fw_suffix>`,
e.g. `v0.8.0-heltec_t114`.)*

### Frame format

```
┌──────┬──────┬───────┬──────────┬───────┐
│ SYNC │ CMD  │  LEN  │ PAYLOAD  │  CRC  │
│ 0xAA │ 1B   │ 2B LE │  0-255B  │ 2B LE │
└──────┴──────┴───────┴──────────┴───────┘
CRC-16/CCITT (poly 0x1021, init 0xFFFF) over CMD+LEN+PAYLOAD.
```

### Host → Modem

| CMD  | Name              | Payload                               |
|------|-------------------|---------------------------------------|
| 0x01 | TX_REQUEST        | Raw LoRa data (1–255 B)               |
| 0x10 | SET_CONFIG        | `RadioConfig` (14 B)                  |
| 0x11 | GET_CONFIG        | —                                     |
| 0x20 | STATUS_REQ        | —                                     |
| 0x22 | NOISE_REQ         | —                                     |
| 0x30 | CAD_REQUEST       | — (Listen Before Talk)                |
| 0x31 | RX_START          | — (restart RX continuous mode)        |
| 0x34 | SET_CAD_PARAMS    | 4 B: symNum / detPeak / detMin / exit |
| 0x40 | RADIO_STANDBY     | — (v0.7; chip → standby, frees the bus)|
| 0x41 | SET_WIFI          | ssid+pass+port+token (variable)       |
| 0x42 | RADIO_RESUME      | — (v0.7; chip → RX continuous)        |
| 0x48 | SET_DISPLAY_NAME  | utf-8 bytes (v0.7; persisted to NVS)  |
| 0x4A | SET_AUTO_CAD      | 1 B (v0.7; T114 auto-CAD before TX)   |
| 0x50 | AUTH              | token bytes (TCP only)                |
| 0x60 | WIFI_RESET        | —                                     |
| 0x61 | GET_WIFI          | —                                     |
| 0x70 | GET_VERSION       | —                                     |
| 0x72 | GET_DEBUG         | — (reset reason / heap / max-loop time)|
| 0x74 | ENTER_BOOTLOADER  | — (v0.7; nRF52 → Adafruit DFU)        |
| 0x90 | OTA_BEGIN         | size + sha256 (v0.7; in-app OTA)      |
| 0x92 | OTA_CHUNK         | offset + data (v0.7)                  |
| 0x94 | OTA_VERIFY        | — (v0.7)                              |
| 0x96 | OTA_APPLY         | — (v0.7; commit + reboot)             |
| 0x98 | OTA_ABORT         | — (v0.7)                              |
| 0xFF | PING              | —                                     |

### Modem → Host

| CMD  | Name              | Payload                               |
|------|-------------------|---------------------------------------|
| 0x02 | TX_DONE           | `airtime_us` (4 B LE)                 |
| 0x03 | TX_FAIL           | —                                     |
| 0x04 | RX_PACKET         | RSSI(2) + SNR(2) + sigRSSI(2) + data  |
| 0x12 | CONFIG_RESP       | `RadioConfig` (14 B)                  |
| 0x21 | STATUS_RESP       | `StatusResp` (24 B)                   |
| 0x23 | NOISE_RESP        | int16 LE (dBm × 10)                   |
| 0x32 | CAD_RESP          | 1 B (0=clear, 1=busy)                 |
| 0x33 | RX_STARTED        | —                                     |
| 0x35 | CAD_PARAMS_RESP   | echoes the 4-byte config              |
| 0x44 | RADIO_STANDBY_RESP| — (v0.7)                              |
| 0x46 | RADIO_RESUME_RESP | — (v0.7)                              |
| 0x49 | SET_DISPLAY_NAME_RESP | — (v0.7)                          |
| 0x4B | SET_AUTO_CAD_RESP | — (v0.7)                              |
| 0x51 | AUTH_OK           | —                                     |
| 0x62 | WIFI_STATUS       | mode + ip + port + ssid + hostname    |
| 0x71 | VERSION_RESP      | ASCII version string                  |
| 0x73 | DEBUG_RESP        | reset(1) + uptime_ms(4) + heap(4) + min_heap(4) + max_loop_us(4) |
| 0x80 | LOG_MSG           | async log line (v0.7; level + utf-8)  |
| 0x91 | OTA_BEGIN_RESP    | — (v0.7)                              |
| 0x93 | OTA_CHUNK_RESP    | — (v0.7)                              |
| 0x95 | OTA_VERIFY_RESP   | — (v0.7)                              |
| 0x97 | OTA_APPLY_RESP    | — (v0.7)                              |
| 0xFE | ERROR             | error code (1 B; `0x0B` = `ERR_NO_RADIO` for boards without LoRa hardware) |
| 0xFF | PONG              | —                                     |

## Default radio parameters

Firmware boots into the MeshCore **EU Narrow / Switzerland** preset; the host
overrides these via `CMD_SET_CONFIG` at `begin()`:

| Parameter    | Value          |
|--------------|----------------|
| Frequency    | 869.618 MHz    |
| Bandwidth    | 62.5 kHz       |
| SF           | 8              |
| CR           | 4/8            |
| TX Power     | 22 dBm         |
| Sync Word    | 0x12 (private) |
| Preamble     | 16 symbols     |
| Header       | Explicit       |
| CRC          | CRC-8          |
| IQ           | Standard       |
| LDRO         | Auto           |

## On-device display

OLED boards (Heltec V3, Ikoka, LilyGO T3-S3) and the T114 TFT all run
the same screen state machine: boot splash for ≥5 s while `setup()`
runs in parallel, then **STATUS** (RX/TX, SSID/IP or USB-tag, state,
fw version) → **RADIO** (freq/SF/BW/CR/power/sync/preamble) →
**DIAGNOSTICS** (uptime, TCP client, USB idle, RX/TX/CRC). Short PRG
tap cycles them; the panel sleeps after 30 s of idle. Boards without
a usable button (P4-Nano, where BOOT shares GPIO35 with RMII TXD1)
auto-cycle every 4 s and never sleep.

Long PRG hold (≥3 s at boot) = factory reset (wipes Wi-Fi NVS,
reboots into AP portal on Wi-Fi boards). Without the button, use
`CMD_WIFI_RESET` or `esptool erase_flash`. T114 has no Wi-Fi NVS to
wipe — factory reset just clears the modem's persistent settings
(display name, auto-CAD flag) via the same flow.
