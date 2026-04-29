# pymc_usb — USB/TCP LoRa Modem for pymc_core

Firmware + Python driver that turns an ESP32 board with an SX1262 front
end into a "dumb" LoRa modem controlled from a Raspberry Pi over USB-CDC,
Wi-Fi/TCP, or (on boards with native Ethernet) wired LAN.

**Supported boards** (one source tree, picked at compile time via
`-DBOARD_<name>` in `platformio.ini`):

| Board                                                                                                       | MCU            | Front end                    | Networks      |
|-------------------------------------------------------------------------------------------------------------|----------------|------------------------------|---------------|
| **Heltec WiFi LoRa 32 V3**                                                                                  | ESP32-S3       | bare SX1262                  | Wi-Fi         |
| **Ikoka Stick** ([ndoo/ikoka-stick-meshtastic-device](https://github.com/ndoo/ikoka-stick-meshtastic-device))| XIAO ESP32-S3  | Ebyte E22-P868M30S, +30 dBm  | Wi-Fi         |
| **LilyGO T-LoRa T3-S3** v1.2/v1.3                                                                           | ESP32-S3       | bare SX1262                  | Wi-Fi         |
| **RAK3112 WisMesh**                                                                                         | ESP32-S3 (module) | SX1262 in-module          | Wi-Fi         |
| **WaveShare ESP32-P4-Nano**                                                                                 | ESP32-P4 (RISC-V) + ESP32-C6 | E22 (off-board, optional) | **Ethernet *or* Wi-Fi** — runtime auto-select: cable plugged → Ethernet wins, no link → fall back to Wi-Fi via C6 SDIO bridge. Both at once is unstable with the radio attached, see [P4-Nano notes](#porting-to-another-esp32-p4-board) |

Drop-in replacement for `SX1262Radio` in pymc_core — all MeshCore logic
(routing, encryption, retransmission) runs on the RPi. The modem handles
only the SX1262 physical layer: TX, RX, CAD, LoRa parameter configuration.

## Architecture

```
                          USB-CDC / WiFi-TCP
Raspberry Pi                                  Heltec V3
┌────────────────────┐                        ┌─────────────────┐
│ pymc_repeater      │◄ USB 921600 ────────►  │ LoRa Modem FW   │
│  └─ pymc_core      │                        │  └─ SX1262      │
│     ├─ USBLoRaRadio│──── OR ──────          │  └─ RadioLib    │
│     └─ TCPLoRaRadio│◄ TCP 5055 ─────────►   │  └─ OLED status │
│                    │                        │  └─ Wi-Fi STA   │
└────────────────────┘                        └─────────────────┘
```

- **USB mode** — cable, instant, no provisioning; ideal for single-board setups.
- **Wi-Fi/TCP mode** — no cable; modem can live anywhere on the LAN while the
  Pi sits elsewhere. Provisioned once via on-device AP portal (open AP
  `LoRa-Modem-XXXX` → `http://192.168.4.1`) or over USB with
  `USBLoRaRadio.set_wifi_credentials()`.

## Project layout

```
pymc_usb/
├── firmware/                      # Shared firmware tree (PlatformIO)
│   ├── platformio.ini             # five envs: heltec_v3, ikoka_stick,
│   │                              # lilygo_t3s3, rak3112_wismesh,
│   │                              # esp32_p4_nano (pioarduino fork)
│   ├── build_release.sh           # builds every env + copies binaries below
│   ├── <env>/                     # prebuilt: bootloader/partitions/firmware.bin
│   ├── include/
│   │   ├── protocol.h             # Binary protocol (shared FW ↔ Python)
│   │   ├── board_config.h         # BoardConfig + RfSwitchPolicy + EthernetConfig
│   │   ├── boards/
│   │   │   ├── heltec_v3.h
│   │   │   ├── ikoka_stick.h
│   │   │   ├── lilygo_t3s3.h
│   │   │   ├── rak3112_wismesh.h
│   │   │   └── esp32_p4_nano.h    # pin map + Ethernet PHY config per board
│   │   ├── oled_display.h
│   │   ├── splash_logo.h          # 64×64 pyMC boot bitmap (PROGMEM)
│   │   ├── wifi_manager.h
│   │   ├── ethernet_manager.h     # RMII Ethernet (no-op on chips without EMAC)
│   │   ├── tcp_server.h
│   │   ├── config_portal.h
│   │   ├── frame_parser.h
│   │   └── ota_manager.h
│   └── src/                       # All .cpp counterparts + main.cpp
│
├── pymc_driver/                   # Python drivers for pymc_core
│   ├── __init__.py
│   ├── usb_radio.py               # USBLoRaRadio — LoRaRadio over USB-CDC
│   ├── tcp_radio.py               # TCPLoRaRadio — LoRaRadio over WiFi/TCP
│   └── test_modem.py              # Standalone test (pyserial only)
│
├── patches/                       # Files applied by scripts/install.sh
│   ├── common.py                  # → pymc_core examples/common.py
│   ├── hardware__init__.py        # → pymc_core/hardware/__init__.py
│   ├── radio-settings-additions.json  # merged into pymc_repeater radio-settings.json
│   ├── pymc_tcp_endpoints.py      # 3 CherryPy methods injected into api_endpoints.py
│   ├── pymc_tcp_panel.html        # pymc_tcp config panel served at /api/pymc_tcp
│   └── pymc_tcp_setup_panel.js    # /setup wizard inline host/port/token block
│
├── scripts/
│   └── install.sh                 # one-shot: copy drivers + patch pymc_repeater
│
├── docker/                        # Container deployment (Wi-Fi/TCP by default)
│   ├── Dockerfile                 # build with: docker compose build
│   ├── entrypoint.sh              # config seed + env-var overrides
│   └── config.yaml                # baked-in /etc/pymc_repeater/config.yaml.default
│
├── docker-compose.yml             # one-shot: `docker compose up -d --build`
├── config.yaml.example            # example /etc/pymc_repeater/config.yaml
├── README.md
├── LICENSE
└── INSTALL.md
```

## Installation

Native install, Docker deployment, firmware flashing (esptool / PlatformIO /
OTA), Wi-Fi provisioning and the full pymc_core integration steps are
documented in [INSTALL.md](INSTALL.md).

## Per-board pin map

All board-specific GPIOs live in `firmware/include/boards/<name>.h`.
Adding a new SX1262 carrier board is a one-file job — copy one of the
existing headers and edit pins / RF-switch policy.

| | Heltec V3 | Ikoka Stick | LilyGO T3-S3 | RAK3112 WisMesh | ESP32-P4-Nano |
|--|--|--|--|--|--|
| MCU | ESP32-S3 | XIAO ESP32-S3 | ESP32-S3 | ESP32-S3 (RAK3112 module) | ESP32-P4 (RISC-V) + C6 |
| LoRa front end | bare SX1262 | Ebyte E22-P868M30S | bare SX1262 | SX1262 in-module | E22-P868M30S (off-board) |
| LoRa SPI NSS / RST / BUSY / DIO1 | 8 / 12 / 13 / 14 | 5 / 3 / 4 / 2 | 7 / 8 / 34 / 33 | 24 / 25 / 21 / 20 | 45 / 20 / 23 / 22 |
| OLED | onboard 0.96″ SSD1306 | external SSD1306 | none | none | external SSD1306 |
| User button (PRG) | GPIO 0 | GPIO 1 | GPIO 0 | GPIO 0 | none ([conflict](#porting-to-another-esp32-p4-board)) |
| Max TX power | 22 dBm | **30 dBm** | 22 dBm | 22 dBm | **30 dBm** |
| RF switch policy | DIO2 → SX1262 internal | EN held HIGH + DIO2 → external TXEN | DIO2 internal | DIO2 internal | EN held HIGH + DIO2 internal |
| Network | Wi-Fi | Wi-Fi | Wi-Fi | Wi-Fi | **Ethernet or Wi-Fi** (runtime auto-select; never both) |
| mDNS hostname | `heltec-<MAC3>.local` | `ikoka-<MAC3>.local` | `lilygo-t3s3-<MAC3>.local` | `rak3112-<MAC3>.local` | `p4nano-<MAC3>.local` |

The full pin numbers (incl. SCK/MISO/MOSI, OLED RST, VEXT enable, EN
hold, etc.) are in each `boards/*.h` — the table above is a digest.

### E22-P RF switch handling (Ikoka & future Ebyte boards)

The E22-P series exposes two control pins per the [E22 datasheet §4.2
truth table](_incoming/E22P-xxxMxxS_UserManual_FR_v1.1.pdf):

| EN | T/R CTRL | Mode |
|---|---|---|
| 1 | 1 | TX |
| 1 | 0 | RX |
| 0 | × | CLOSE |

On Ikoka the firmware drives them like this:

- **EN (module pin 6, GPIO 6)** — held LOW for 5 s at boot so the LDOs
  and PA bias settle, then latched HIGH for the rest of the device's
  lifetime. Never toggled by TX/RX.
- **T/R CTRL (module pin 7)** — not wired to MCU. The Ikoka PCB has
  a trace from module pin 8 (SX1262 DIO2) to pin 7, and firmware
  enables `radio.setDio2AsRfSwitch(true)` so the SX1262 toggles it
  HIGH on TX automatically.

The full policy is captured per-board in `RfSwitchPolicy`
(see `firmware/include/board_config.h`); a future board with two
separate MCU-driven enable lines just sets `rx_pin` / `tx_pin` and
RadioLib's `setRfSwitchPins` takes care of toggling.

## Porting to another ESP32-P4 board

The WaveShare ESP32-P4-Nano is the reference; its profile lives at
`firmware/include/boards/esp32_p4_nano.h`. Most non-Nano P4 boards use
the same architecture (RISC-V P4 + ESP32-C6 over SDIO + RMII Ethernet
PHY), so adding one is mostly a matter of copying that header and
adjusting pins. The PlatformIO env in `platformio.ini` pins the
[pioarduino fork](https://github.com/pioarduino/platform-espressif32)
because vanilla `platformio/espressif32` doesn't ship ESP32-P4 support
yet — keep that line when you copy the env block.

A few quirks differ from the ESP32-S3 family and are worth checking
against the schematic of your specific carrier:

- **Boot strap is GPIO35**, not GPIO0 (which is just a regular GPIO on
  P4). On many P4 boards the BOOT button is wired to GPIO35 — but
  GPIO35 is also one of the RMII data lines (TXD1) used by the EMAC.
  When `ethernet.enabled = true`, the chip drives GPIO35 as a 25 MHz
  RMII output and the button is unusable as an input (`digitalRead`
  sees the TX bitstream). Either set `pin_user_button = -1` and live
  without the screen-cycle button (the firmware then auto-cycles every
  4 s), or wire your own button to a free GPIO (P1/P2 headers expose
  20-23, 45-48, 54).

- **GPIOs 49 and above sit on a separate LDO domain** that can be
  flaky if the carrier doesn't bring it up the way the framework
  expects. The Arduino-ESP32 `pins_arduino.h` for `esp32p4` even
  carries a comment to that effect. RMII TX_EN (49) and RMII_CLK (50)
  fall in that range, but they Just Work on the WaveShare reference;
  if you're seeing PHY init fail on a different carrier, look there.

- **Wi-Fi (via the on-board ESP32-C6 over SDIO) and Ethernet cannot
  both run while the radio is active.** The combination is unstable
  on this carrier — the C6 esp_hosted bridge falls off the SDIO bus
  every ~25 s and the SoC's RTC watchdog reboots the whole chip. The
  fix is **runtime auto-selection in `setup()`**: with both
  `has_wifi = true` and `ethernet.enabled = true` the firmware brings
  Ethernet up first; if a cable is plugged (link inside ~5 s) it keeps
  EMAC and skips Wi-Fi, otherwise it tears EMAC back down (releases
  the RMII GPIOs) and falls back to Wi-Fi. Either single network works
  fine with the radio; both together don't. Boards that solder the
  SX1262 onto a shared RF-clean PCB may be able to run both — re-test
  on your hardware before relying on it.

- **Ethernet** is plumbed through `BoardConfig::EthernetConfig`. For
  the IP101GRI on the Nano: `pin_mdc = 31`, `pin_mdio = 52`,
  `pin_phy_reset = 51`, `phy_addr = 1`, `rmii_clock_input = true`
  (50 MHz reference comes from the PHY). Other RMII PHYs (LAN8720,
  RTL8201, KSZ8081) just need a new `EthernetPhy` enum value plus the
  matching `eth_phy_type_t` mapping in `ethernet_manager.cpp`.
  Static IP fields are optional — leave `use_static_ip = false` and
  the firmware does DHCP.

- **Debug serial:** ESP32-P4's native USB-CDC (USB-Serial-JTAG) on the
  USB-C port works for esptool flashing but the Arduino `Serial`
  routed through it produces corrupted bytes on the
  pioarduino release we pin. Keep `ARDUINO_USB_CDC_ON_BOOT=0` and use
  the second USB-C port (CH343P → UART0) for `Serial.printf` debug.
  TCP via Ethernet is the practical observability channel.

- **OLED** is optional. Set `pin_i2c_sda = -1, pin_i2c_scl = -1` and
  `oled_display.cpp` short-circuits — `Wire.begin()` is skipped, every
  `show*()` call returns immediately, the firmware runs as a headless
  bridge.

## Network exposure: LAN-only by design

Both TCP-facing services on the modem — the binary protocol on port
5055 and the OTA HTTP listener on port 80 — refuse connections whose
remote source address is outside the **private RFC1918 / link-local /
loopback** ranges. The check happens at `accept()` time (TCP) and on
the first HTTP handler call (OTA), before any frame parsing or
authentication.

Accepted ranges (IPv4):

```
10.0.0.0/8        Class A private
172.16.0.0/12     Class B private
192.168.0.0/16    Class C private
169.254.0.0/16    link-local (APIPA / IPv4LL)
127.0.0.0/8       loopback
```

A NAT port-forward from a public address, or any tunnel that exposes
the modem to the public Internet with a public source IP, is rejected
before the protocol server sees the bytes. The OTA endpoint returns
HTTP 403; the protocol port simply drops the socket. Both events are
logged to UART (`[TCP] rejecting non-LAN client …` / `[OTA] reject
non-LAN client …`) for diagnostics.

This is a **hard policy of the firmware** — the modem is intended for
local pymc_repeater control and is not designed to bridge LoRa traffic
across the public Internet. The check lives in
`firmware/include/net_filter.h`; lifting it requires editing that file
and re-flashing.

For remote access where it's actually appropriate (the operator's own
LAN-segment over a VPN they control, etc.) put the VPN tunnel on the
modem's network and let the LAN address show up as the source — the
WireGuard / Tailscale / Zerotier address you'll see is RFC1918 already
and passes the filter.

## Wire protocol v0.5.11

*(Full command list in `firmware/include/protocol.h`; the section below is
summarised.)*

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
| 0x41 | SET_WIFI          | ssid+pass+port+token (variable)       |
| 0x50 | AUTH              | token bytes (TCP only)                |
| 0x60 | WIFI_RESET        | —                                     |
| 0x61 | GET_WIFI          | —                                     |
| 0x70 | GET_VERSION       | —                                     |
| 0x72 | GET_DEBUG         | — (reset reason / heap / max-loop time)|
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
| 0x51 | AUTH_OK           | —                                     |
| 0x62 | WIFI_STATUS       | mode + ip + port + ssid + hostname    |
| 0x71 | VERSION_RESP      | ASCII version string                  |
| 0x73 | DEBUG_RESP        | reset(1) + uptime_ms(4) + heap(4) + min_heap(4) + max_loop_us(4) |
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

## OLED screens

Boot sequence:

1. **Splash** — pyMC logo (64×64 bitmap from `firmware/include/splash_logo.h`),
   held for ≥5 s while the rest of `setup()` (Wi-Fi connect, Ethernet bring-up,
   radio init) runs in parallel.
2. **STATUS** — RX/TX counters, SSID *or* `ethernet`, IP, state tag
   (`WiFi` / `AP` / `ETH` / `ETHL` / `…`), firmware version
3. **RADIO** — current radio config (freq, SF, BW, CR, power, preamble, sync)
4. **DIAGNOSTICS** — uptime, TCP client IP, USB idle time, RX/TX/CRC counters

On boards with a working PRG/BOOT button, short tap cycles between the
three runtime screens and a short idle goes back to **SLEEP** (panel off
after 30 s). On boards where the button is unavailable
(e.g. ESP32-P4-Nano — see [P4-Nano notes](#porting-to-another-esp32-p4-board))
the firmware **auto-cycles** STATUS → RADIO → DIAGNOSTICS every 4 s and
keeps the panel awake.

Long PRG hold (≥3 s at boot) = factory reset: wipes Wi-Fi NVS and reboots into
AP configuration mode. Boards without a usable button: use `CMD_WIFI_RESET`
over the protocol or erase NVS via `esptool erase_flash`.
