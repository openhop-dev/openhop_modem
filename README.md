# openHop Modem — USB/TCP LoRa modem for openHop Core

Firmware that turns a supported ESP32 or nRF52 board with an SX1262
front end into a dedicated LoRa modem controlled by an openHop Core host
over USB-CDC, Wi-Fi/TCP, or (on boards with native Ethernet) wired LAN.
The transport is host-platform agnostic; platform support depends on the
openHop application and its access to USB or the modem's network.

**Supported boards** (one source tree, picked at compile time via
`-DBOARD_<name>` in `platformio.ini`):

| Board                                                                                                       | MCU                          | Front end                  | Networks |
|-------------------------------------------------------------------------------------------------------------|------------------------------|----------------------------|----------|
| **Heltec WiFi LoRa 32 V3**                                                                                  | ESP32-S3                     | bare SX1262                | Wi-Fi    |
| **Heltec WiFi LoRa 32 V4**                                                                                  | ESP32-S3                     | SX1262 + PA/LNA FEM        | Wi-Fi    |
| **Heltec WiFi LoRa 32 V4.2**                                                                                | ESP32-S3                     | SX1262 + GC1109 FEM        | Wi-Fi    |
| **Heltec WiFi LoRa 32 V4.3**                                                                                | ESP32-S3                     | SX1262 + KCT8103L FEM      | Wi-Fi    |
| **Heltec Wireless Tracker V2**                                                                              | ESP32-S3                     | SX1262 + KCT8103L PA/FEM + TFT 160×80 | Wi-Fi |
| **Ikoka Stick** ([ndoo/ikoka-stick-meshtastic-device](https://github.com/ndoo/ikoka-stick-meshtastic-device))| XIAO ESP32-S3                | Ebyte E22P868M30S, +30 dBm | Wi-Fi   |
| **Seeed XIAO Wio-SX1262**                                                                                   | XIAO ESP32-S3                | bare SX1262                | Wi-Fi    |
| **MeshSmith Photon-1W ESP32-C6**                                                                            | XIAO ESP32-C6                | SX1262/E22P class 1 W      | Wi-Fi    |
| **LilyGO T-LoRa T3-S3** v1.2/v1.3                                                                           | ESP32-S3                     | bare SX1262 + OLED         | Wi-Fi    |
| **LilyGO T-Beam-S3 Supreme**                                                                                | ESP32-S3                     | bare SX1262 + L76K GNSS + 1.3" OLED, AXP2101 PMU | Wi-Fi |
| **RAK3112 WisMesh**                                                                                         | ESP32-S3 (module)            | SX1262 in-module           | Wi-Fi    |
| **B&Q Consulting Station G2**                                                                                | ESP32-S3                     | SX1262 + 35 dBm PA/LNA     | Wi-Fi    |
| **BQ Voyage Station G3**                                                                                     | ESP32-S3 daughterboard       | SX1262 + selectable PA/LNA | Wi-Fi    |
| **WaveShare ESP32-P4-Nano**                                                                                 | ESP32-P4 (RISC-V) + ESP32-C6 | E22 (off-board, optional)  | **Ethernet *or* Wi-Fi** — runtime auto-select: cable plugged → Ethernet wins, no link → fall back to Wi-Fi via C6 SDIO bridge. Both at once is unstable with the radio attached, see [P4-Nano notes](#porting-to-another-esp32-p4-board) |
| **MeshSmith EtherMesh-1W**                                                                                   | ESP32-P4 (RISC-V)            | E22P/SX1262 class 1 W      | **Ethernet** |
| **Heltec T114**                                                                                             | nRF52840                     | bare SX1262 + TFT 135×240  | **none** — USB-CDC + UART only |
| **RAK4631 USB**                                                                                             | nRF52840 (RAK4631)           | SX1262 in-module           | **none** — USB-CDC only |
| **RAK4631 WisMesh Ethernet**                                                                                | nRF52840 (RAK4631) + RAK13800/W5100S | SX1262 in-module     | **Ethernet** (W5100S) — TCP port 5055, USB-CDC fallback |
| **RAK3401**                                                                                                 | nRF52840 (RAK4631 core) + RAK13302   | SX1262 + SKY66122 ~1 W FEM | **none** — USB-CDC only |
| **Seeed XIAO nRF52840 + Wio-SX1262**                                                                        | XIAO nRF52840                | bare SX1262                | **none** — USB-CDC only |

openHop Core connects to the modem through `USBLoRaRadio` or
`TCPLoRaRadio`. Routing, encryption, retransmission, and other MeshCore
logic stay on the host; the modem handles the SX1262 physical layer:
TX, RX, CAD, and LoRa parameter configuration.

## Architecture

```
                         USB-CDC / Wi-Fi-TCP / Ethernet-TCP
Host running openHop Core                         openHop Modem
┌──────────────────────────┐                      ┌─────────────────┐
│ openHop application      │                      │ Modem firmware  │
│  └─ openHop Core         │◄ USB 921600 ───────►│  ├─ RadioLib    │
│     ├─ USBLoRaRadio      │                      │  ├─ SX1262      │
│     └─ TCPLoRaRadio      │◄ TCP 5055 ─────────►│  ├─ OLED / TFT  │
│                          │                      │  └─ Wi-Fi / ETH │
└──────────────────────────┘                      └─────────────────┘
```

The host can be any platform supported by the openHop application and
its USB or network transport. Wi-Fi is available on supported ESP32
boards; wired Ethernet is available on P4-Nano, EtherMesh-1W, and the
RAK4631 WisMesh Ethernet target. T114, RAK4631 USB, RAK3401, and XIAO nRF52
Wio use USB-CDC only.

- **USB mode** — cable, instant, no provisioning; ideal for single-board setups.
- **Network TCP mode** — Wi-Fi/TCP on ESP32 boards, or Ethernet/TCP on wired
  targets (P4-Nano, EtherMesh-1W, RAK4631). Wi-Fi boards are provisioned via AP portal
  (`openHop-Modem-XXXX` → `http://192.168.4.1`), USB, or their web UI; the TCP
  token defaults blank/open on fresh firmware and can be set from the web UI on
  web-enabled builds. The RAK4631 Ethernet variant provides a LAN-only,
  authenticated WebUI and JSON/config API on port 80. It has no mDNS; discover
  its IP from DHCP or configure a static address. WebUI firmware upload over
  Ethernet is disabled because safe staged activation and recovery would
  require a custom openHop Modem bootloader, which is not currently planned.
  Deployed gateways can still be updated OTA through the working Bluetooth DFU
  flow using the generated nRF52 `firmware.zip`.

## Project layout

- **`firmware/`** — PlatformIO tree, twenty environments sharing one source.
  Each board lives in `include/boards/<env>.h`; `platformio.ini` picks
  one via `-DBOARD_<NAME>`. Prebuilt artifacts (ESP32: combined
  `firmware.factory.bin` plus `bootloader.bin / partitions.bin / firmware.bin`;
  nRF52: `firmware.hex`, Adafruit DFU `firmware.zip`, and UF2 where supported)
  live in `firmware/<env>/`.
- **`.github/workflows/`** — firmware validation, asset staging, and release
  packaging. Repeater packaging and modem transport drivers live in the
  openHop Repeater and openHop Core repositories, not here.

## Installation

Flash supported boards from the browser at <https://flasher.openhop.dev/>.
[INSTALL.md](INSTALL.md) also covers local esptool/PlatformIO flashing,
network OTA, Wi-Fi provisioning, and selecting the built-in `modem_usb` or
`modem_tcp` transport in openHop Repeater. No driver copying, Repeater patching,
or modem-specific container image is required.

## Acknowledgements

Special thanks to [itk80](https://github.com/itk80) (KeenKJ), whose early
firmware, modem-driver, integration, and hardware-support work laid the
groundwork that became openHop Modem. Without his work, this project would
not exist.

## Firmware asset builds

The `Build Firmware Assets` GitHub workflow uses
`firmware/tools/build_firmware_assets.py` to build PlatformIO envs and stage
flasher-ready outputs in `firmware/<env>/`.  Pull requests do not build
firmware.  Pushes to `main` build affected envs and open an asset-update PR
containing updated `firmware/<env>/` binaries, manifests, and SHA256 sums
without uploading Actions artifacts.  Manual dispatch can build `auto`, `all`,
or specific envs from any branch; manual runs upload Actions artifacts for
review and only commit generated files when `commit_artifacts=true`.

## Per-board pin map

All board-specific GPIOs and policies live in
`firmware/include/boards/<name>.h` — pick the closest existing one
when adding a new carrier and edit the few fields that differ.

Per-board highlights (full pin numbers in the headers, mDNS prefix is
`BoardConfig.mdns_prefix`, hostname `<prefix>-<mac3>.local`):

- **Heltec V3** — onboard SSD1306, bare SX1262, max 22 dBm.
- **Heltec V4** — onboard SSD1306, SX1262 + V4.x PA/LNA front-end, native USB-CDC, max 22 dBm SX1262 command power.
- **Heltec V4.2** — dedicated GC1109 PA/FEM build with VFEM/CSD enabled and GC1109 CPS driven high for full PA mode.
- **Heltec V4.3** — dedicated KCT8103L PA/FEM build with SX1262 boosted RX gain enabled and FEM RX LNA bypassed by default for lower noise floor; the device web UI can toggle the external FEM RX LNA and set `agc.reset.interval` for periodic RX AGC resets during long idle periods.
- **Heltec Wireless Tracker V2** — ESP32-S3 + SX1262 + KCT8103L PA/FEM, ST7735 TFT 160×80, native USB-CDC, max 22 dBm SX1262 command power.
- **Ikoka Stick** — XIAO ESP32-S3 + E22P868M30S, EN-held + DIO2-as-RF-switch, max 30 dBm chip / +10 dB PA, external OLED.
- **XIAO Wio-SX1262** — Seeed XIAO ESP32-S3 + bare SX1262, no OLED.
- **MeshSmith Photon-1W ESP32-C6** — Seeed XIAO ESP32-C6 + Photon 1 W SX1262/E22P class front end, Photon XIAO pinout (D1 DIO1, D2 reset, D3 busy, D4 NSS, D5 RXEN, D8/D9/D10 SPI), Wi-Fi/TCP + AP provisioning + web UI/stats/OTA.
- **LilyGO T3-S3** — bare SX1262 + onboard SSD1306, native USB-CDC.
- **LilyGO T-Beam-S3 Supreme** — bare SX1262 + onboard L76K GNSS + 1.3" SH1106 OLED, native USB-CDC. LoRa/GNSS/OLED power rails are gated by an onboard AXP2101 PMU chip (`pmu_manager.cpp` / `BoardConfig.pmu`) on its own I2C bus rather than plain GPIOs — the only board in this fleet wired that way.
- **RAK3112 WisMesh** — SX1262 inside the RAK3112 module, no OLED.
- **Station G2** — SX1262 + high-power PA/LNA, SH1106 display, max SX1262 drive capped at 19 dBm.
- **Station G3** — BQESP32V1M N16R8 (16 MB flash + 8 MB octal PSRAM) + BQ35LORA900V1M, Station G2-compatible radio/display pins, persistent Station G3-only web/API selection of lower or higher PA PL1 mode on GPIO9 (lower by default), persistent RX-only external LNA enable/bypass on GPIO10, onboard INA219 input-voltage/current/power telemetry with since-boot minimum voltage and maximum current, optional GROVE GPS on IO7/IO15, and max SX1262 drive capped at 19 dBm. The LNA is always bypassed before TX. Remove the PA PL1/LNA P jumpers for software GPIO control; PA PL2 remains a physical jumper.
- **WaveShare ESP32-P4-Nano** — RISC-V P4 + C6 + IP101GRI Ethernet PHY + off-board E22, runtime ETH-or-Wi-Fi (never both, see below).
- **Heltec T114** — nRF52840 + bare SX1262 + ST7789 TFT 135×240, **no Wi-Fi/TCP/network OTA**; USB-CDC + UART transport only, OTA via Adafruit nRF52 DFU (USB) or in-app `CMD_OTA_*` over the protocol transport.
- **RAK4631 USB** — RAK4631 nRF52840 core on a compatible WisBlock base, using the same proven internal SX1262 pins, DIO2 RF-switch policy, SPIM2 radio bus, and 22 dBm ceiling as the Ethernet build. The `rak4631_usb` environment omits the RAK13800 dependency and all W5100S/TCP/network initialization; native USB-CDC is the only modem transport.
- **RAK4631 WisMesh Ethernet** — RAK4631 nRF52840 core module + RAK13800 W5100S Ethernet on the WisBlock IO slot. It has its own PlatformIO board JSON and product-specific variant under `firmware/variants/RAK4631_WisMesh_Ethernet/`, separate SPIM instances for Ethernet (SPIM3) and LoRa (SPIM2), no display, and no Wi-Fi. TCP port 5055 is the primary transport; USB-CDC is the fallback. Port 80 serves the authenticated WebUI/config API. WebUI firmware upload over Ethernet is disabled because safe staged activation and recovery would require a custom openHop Modem bootloader, which is not currently planned. Deployed gateways remain OTA-updatable through the working Bluetooth DFU flow using the generated nRF52 `firmware.zip`; generated `firmware.ota` files are non-installable staging artifacts. The WebUI/API persist hostname, DHCP/static network values, TCP port/token, HTTP password, and compile-gated GPS state without echoing secret values. The default HTTP login is `admin` / `openhop`; change both the HTTP password and blank/open TCP token before trusting a shared LAN. The product USB descriptor is `WisMesh-openHop-Ethernet`; reselect any stale by-id serial path after flashing. Hardware validation covers W5100S TCP, the authenticated WebUI/config API, hostname and static-network persistence, automatic reboot and HTTP recovery after saves, and complete browser-initiated BLE DFU uploads.
- **RAK3401** — RAKwireless nRF52840 WisBlock core + RAK13302 LoRa module (SX1262 driving a Skyworks SKY66122-11 front end, ~1 W). Ported from the MeshCore `rak3401` variant: unlike the RAK4631 targets the SX1262 sits on the WisBlock SPI slot (P0.03 SCK / P0.29 MISO / P0.30 MOSI / P0.26 NSS), not on the core module's internal bus. WB_IO2 (P0.34) gates the 3V3_S rail and the RAK13302's 5 V PA boost, WB_IO3 (P0.21) drives the SKY66122 CSD+CPS pair — both held HIGH for the life of the device — and SX1262 DIO2 drives CTX so TX/RX switching is hardware-timed. SX1262 drive is capped at 22 dBm into the PA, with the 0x08B5 sensitivity patch and boosted RX gain enabled. LiPo voltage is read from P0.05 through a 1.73:1 divider. **No Wi-Fi/TCP/network OTA**, no display; native USB-CDC transport only, with firmware updates through the Adafruit nRF52 USB bootloader using DFU or UF2 assets.
- **Seeed XIAO nRF52840 + Wio-SX1262** (SKU 102010710) — XIAO nRF52840 + bare SX1262 on the Wio-SX1262 carrier, BLE 5.0 hardware unused, **no Wi-Fi/TCP/network OTA**, no display; native USB-CDC transport only, OTA via Adafruit nRF52 DFU (UF2 disk on double-click reset) or in-app `CMD_OTA_*`.

RAK4631 battery reporting uses the source-backed `WB_A0` mapping
(P0.05/AIN3), the nRF52 3.0 V internal ADC reference, 12-bit samples, eight
nonblocking samples per batch, and a 1.73 divider scale. The WebUI reports
voltage only; it does not infer charging state or state of charge. Real-board
calibration against a meter is still required.
The mapping was audited against MeshCore commit
`2228214ded57c2761312730cdeae14b2b31bc5a3`,
`variants/rak4631/RAK4631Board.h`, plus the official RAK nRF52 variant.

Optional serial NMEA GPS is compile-gated with
`PYMC_RAK4631_GPS_SERIAL_ENABLE=1` and uses Serial1 RX15/TX16 at 9600 baud.
No GPS power/reset GPIO is guessed, and ordinary builds do not poll GPS. The
RAK12500 I2C/slot-control mappings remain unimplemented until a safe base-board
slot is physically verified; Ethernet power/reset pins 34/21 are reserved and
must never be reused by GPS code.

### E22P RF switch (Ikoka, P4-Nano + E22P)

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
and management/OTA HTTP on 80 where supported — refuse clients whose source address is outside
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

From v0.8 the HTTP surface (web management page, `/api/*` JSON endpoints,
and OTA `/update` on ESP builds) is gated by HTTP Basic Auth. Defaults on
first boot:

- **user:** `admin`
- **password:** `openhop`

Change the password via the **Change HTTP password** form in the web
UI; it is persisted in NVS under `http_pass`. ArduinoOTA (espota) uses
the same password as its `--auth` token. Examples:

```bash
# Open the web UI (browser)         → http://<host>/         (admin / openhop)
# Pull live stats                   → curl -u admin:openhop http://<host>/api/stats
# Flash a new firmware over HTTP    → curl -u admin:openhop \
#       -F firmware=@firmware/<env>/firmware.bin http://<host>/update
# Flash over espota via PlatformIO  → pio run -e <env> -t upload \
#       --upload-port <host> --upload-flags="--auth=openhop"
```

The RAK4631 stores its password in a CRC-protected, power-loss-safe dual-slot
record in bootloader-preserved nRF52 configuration pages rather than ESP NVS
or LittleFS. Its authenticated RAK-only **Bluetooth DFU** control hands off to
the installed Nordic BLE bootloader after the HTTP response is acknowledged and
the Ethernet socket is closed; a separate Nordic-compatible DFU app uploads the
nRF52 `firmware.zip`. The bootloader's Bluetooth name is not fixed. Complete BLE
DFU uploads are validated on the production gateway; keep USB serial DFU
available for interrupted-transfer recovery. This Bluetooth DFU flow is the
supported OTA update path for deployed gateways. Ethernet/WebUI `/update` and
staged activation remain disabled because they would require a custom openHop
Modem bootloader, which is not currently planned.
RAK JSON responses expose `tcp_token_set`, never the token.

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

The RAK4631 staged writer is not connected to `CMD_OTA_*` or HTTP `/update`.
Those update commands and routes are disabled and unsupported on that target;
supporting safe staged activation and recovery would require a custom openHop
Modem bootloader, which is not currently planned. Use the generated
`firmware.zip` with the supported Bluetooth DFU OTA flow or USB DFU instead.

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
