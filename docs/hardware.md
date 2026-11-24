# Hardware reference

This page covers board families, RF-front-end policy and carrier-specific notes.
Use the [installation guide](../INSTALL.md#1-flash-the-firmware) for exact
PlatformIO environment names and installation steps. All inline source paths
below are relative to the repository root. A declared source profile does not
mean a matching release image or browser-flasher entry is available.

## Supported boards

Select the exact board with its `BOARD_*` definition in `firmware/platformio.ini`.

| Board | MCU | Front end | Networks |
| ------------------------------------------------------------------------------------------------------------- | ------------------------------ | ---------------------------- | ---------- |
| **Heltec WiFi LoRa 32 V3** | ESP32-S3 | bare SX1262 | Wi-Fi |
| **Heltec WiFi LoRa 32 V4** | ESP32-S3 | SX1262 + PA/LNA FEM | Wi-Fi |
| **Heltec WiFi LoRa 32 V4.2** | ESP32-S3 | SX1262 + GC1109 FEM | Wi-Fi |
| **Heltec WiFi LoRa 32 V4.3** | ESP32-S3 | SX1262 + KCT8103L FEM | Wi-Fi |
| **Heltec Wireless Tracker V2** | ESP32-S3 | SX1262 + KCT8103L PA/FEM + TFT 160×80 | Wi-Fi |
| **Ikoka Stick** ([ndoo/ikoka-stick-meshtastic-device](https://github.com/ndoo/ikoka-stick-meshtastic-device)) | XIAO ESP32-S3 | Ebyte E22P868M30S, +30 dBm | Wi-Fi |
| **Seeed XIAO Wio-SX1262** | XIAO ESP32-S3 | bare SX1262 | Wi-Fi |
| **MeshSmith Photon-1W ESP32-C6** | XIAO ESP32-C6 | SX1262/E22P class 1 W | Wi-Fi |
| **LilyGO T-LoRa T3-S3** v1.2/v1.3 | ESP32-S3 | bare SX1262 + OLED | Wi-Fi |
| **LilyGO T-Beam-S3 Supreme** | ESP32-S3 | bare SX1262 + L76K GNSS + 1.3" OLED, AXP2101 PMU | Wi-Fi |
| **LilyGO T-Beam 1W** | ESP32-S3 | SX1262 + external 1 W PA + L76K GNSS + SH1106/NTC fan | Wi-Fi |
| **RAK3112 WisMesh** | ESP32-S3 (module) | SX1262 in-module | Wi-Fi |
| **B&Q Consulting Station G2** | ESP32-S3 | SX1262 + 35 dBm PA/LNA | Wi-Fi |
| **BQ Voyage Station G3** | ESP32-S3 daughterboard | SX1262 + selectable PA/LNA | Wi-Fi |
| **WaveShare ESP32-P4-Nano** | ESP32-P4 (RISC-V) + ESP32-C6 | E22 (off-board, optional) | **Ethernet *or* Wi-Fi** — runtime auto-select: cable plugged → Ethernet wins, no link → fall back to Wi-Fi via C6 SDIO bridge. Both at once is unstable with the radio attached, see [P4-Nano notes](#porting-to-another-esp32-p4-board) |
| **MeshSmith EtherMesh-1W** | ESP32-P4 (RISC-V) | E22P/SX1262 class 1 W | **Ethernet** |
| **MeshSmith EtherMesh-Duo** | ESP32-P4 (RISC-V) | Two RAK13302/SX1262 slots; RF1-only default, RF2 optional | **Ethernet**, one fixed TCP port per slot |
| **Heltec T114** | nRF52840 | bare SX1262 + TFT 135×240 | **none** — USB-CDC + UART only |
| **RAK4631 USB** | nRF52840 (RAK4631) | SX1262 in-module | **none** — USB-CDC only |
| **RAK4631 WisMesh Ethernet** | nRF52840 (RAK4631) + RAK13800/W5100S | SX1262 in-module | **Ethernet** (W5100S) — TCP port 5055, USB-CDC fallback |
| **RAK3401** | nRF52840 (RAK4631 core) + RAK13302 | SX1262 + SKY66122 ~1 W FEM | **none** — USB-CDC only |
| **Seeed XIAO nRF52840 + Wio-SX1262** | XIAO nRF52840 | bare SX1262 | **none** — USB-CDC only |

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
- **Ikoka Stick** — XIAO ESP32-S3 + E22P868M30S, EN-held + DIO2-as-RF-switch, external 1 W-class PA, external OLED. Chip drive and antenna-side output are different quantities; follow the fitted module and board policy.
- **XIAO Wio-SX1262** — Seeed XIAO ESP32-S3 + bare SX1262, no OLED.
- **MeshSmith Photon-1W ESP32-C6** — Seeed XIAO ESP32-C6 + Photon 1 W SX1262/E22P class front end, Photon XIAO pinout (D1 DIO1, D2 reset, D3 busy, D4 NSS, D5 RXEN, D8/D9/D10 SPI), Wi-Fi/TCP + AP provisioning + web UI/stats/OTA.
- **LilyGO T3-S3** — bare SX1262 + onboard SSD1306, native USB-CDC.
- **LilyGO T-Beam-S3 Supreme** — bare SX1262 + onboard L76K GNSS + 1.3" SH1106 OLED, native USB-CDC. LoRa/GNSS/OLED power rails are gated by an onboard AXP2101 PMU chip (`pmu_manager.cpp` / `BoardConfig.pmu`) on its own I2C bus rather than plain GPIOs — the only board in this fleet wired that way.
- **LilyGO T-Beam 1W** — ESP32-S3 + SX1262 external PA, L76K GNSS, SH1106 OLED, board NTC on GPIO14, and fan on GPIO41. The SX1262 is capped at 22 dBm chip drive; the external PA produces the antenna-side 1 W output. The fan starts fail-safe ON, turns on at or above 45 C, turns off below 40 C, and holds its previous state between those thresholds. USB-C input (3.9-6 V) is marginal for sustained high-power TX; use a 7.4 V pack capable of 2 A or more discharge. Battery telemetry is calibrated voltage only; no percentage is inferred.
- **RAK3112 WisMesh** — SX1262 inside the RAK3112 module, no OLED.
- **Station G2** — SX1262 + high-power PA/LNA, SH1106 display, max SX1262 drive capped at 19 dBm.
- **Station G3** — BQESP32V1M N16R8 (16 MB flash + 8 MB octal PSRAM) + BQ35LORA900V1M, Station G2-compatible radio/display pins, persistent Station G3-only web/API selection of lower or higher PA PL1 mode on GPIO9 (lower by default), persistent RX-only external LNA enable/bypass on GPIO10, onboard INA219 input-voltage/current/power telemetry with since-boot minimum voltage and maximum current, optional GROVE GPS on IO7/IO15, and max SX1262 drive capped at 19 dBm. The LNA is always bypassed before TX and during periodic AGC maintenance. Remove the PA PL1/LNA P jumpers for software GPIO control; PA PL2 remains a physical jumper.

- **MeshSmith EtherMesh-1W**: P4 Ethernet with one E22P/SX1262 front end; select both the board and silicon family.
- **MeshSmith EtherMesh-Duo**: P4 Ethernet with RF1 on 5055 and optional RF2 on 5056; RF1-only is the default. See [multi-radio policy](multi-radio.md) and [P4 profiles](p4-silicon.md).
- **WaveShare ESP32-P4-Nano** — RISC-V P4 + C6 + IP101GRI Ethernet PHY + off-board E22, runtime ETH-or-Wi-Fi (never both, see below).
- **Heltec T114** — nRF52840 + bare SX1262 + ST7789 TFT 135×240, **no Wi-Fi/TCP/network OTA**; USB-CDC + UART transport only, updates through Adafruit nRF52 DFU over USB; the declared in-app `CMD_OTA_*` writer is not implemented.
- **RAK4631 USB** — RAK4631 nRF52840 core on a compatible WisBlock base, using the same proven internal SX1262 pins, DIO2 RF-switch policy, SPIM2 radio bus, and 22 dBm ceiling as the Ethernet build. The `rak4631_usb` environment omits the RAK13800 dependency and all W5100S/TCP/network initialization; native USB-CDC is the only modem transport.
- **RAK4631 WisMesh Ethernet** — RAK4631 nRF52840 core module + RAK13800 W5100S Ethernet on the WisBlock IO slot. It has its own PlatformIO board JSON and product-specific variant under `firmware/variants/RAK4631_WisMesh_Ethernet/`, separate SPIM instances for Ethernet (SPIM3) and LoRa (SPIM2), no display, and no Wi-Fi. TCP port 5055 is the primary transport; USB-CDC is the fallback. Port 80 serves the authenticated WebUI/config API. WebUI firmware upload over Ethernet is disabled because safe staged activation and recovery would require a custom openHop Modem bootloader, which is not currently planned. Deployed gateways remain OTA-updatable through the working Bluetooth DFU flow using the generated nRF52 `firmware.zip`; generated `firmware.ota` files are non-installable staging artifacts. The WebUI/API persist hostname, DHCP/static network values, TCP port/token, HTTP password, and compile-gated GPS state without echoing secret values. The default HTTP login is `admin` / `openhop`; change both the HTTP password and blank/open TCP token before trusting a shared LAN. The product USB descriptor is `WisMesh-openHop-Ethernet`; reselect any stale by-id serial path after flashing. Hardware validation covers W5100S TCP, the authenticated WebUI/config API, hostname and static-network persistence, automatic reboot and HTTP recovery after saves, and complete browser-initiated BLE DFU uploads.
- **RAK3401** — RAKwireless nRF52840 WisBlock core + RAK13302 LoRa module (SX1262 driving a Skyworks SKY66122-11 front end, ~1 W). Ported from the MeshCore `rak3401` variant: unlike the RAK4631 targets the SX1262 sits on the WisBlock SPI slot (P0.03 SCK / P0.29 MISO / P0.30 MOSI / P0.26 NSS), not on the core module's internal bus. WB_IO2 (P0.34) gates the 3V3_S rail and the RAK13302's 5 V PA boost, WB_IO3 (P0.21) drives the SKY66122 CSD+CPS pair — both held HIGH for the life of the device — and SX1262 DIO2 drives CTX so TX/RX switching is hardware-timed. SX1262 drive is capped at 22 dBm into the PA, with the 0x08B5 sensitivity patch and boosted RX gain enabled. LiPo voltage is read from P0.05 through a 1.73:1 divider. **No Wi-Fi/TCP/network OTA**, no display; native USB-CDC transport only, with firmware updates through the Adafruit nRF52 USB bootloader using DFU or UF2 assets.
- **Seeed XIAO nRF52840 + Wio-SX1262** (SKU 102010710) — XIAO nRF52840 + bare SX1262 on the Wio-SX1262 carrier, BLE 5.0 hardware unused, **no Wi-Fi/TCP/network OTA**, no display; native USB-CDC transport only, updates through Adafruit nRF52 DFU or the matching UF2 bootloader flow; the declared in-app `CMD_OTA_*` writer is not implemented.

### Station G2 and G3 reception and sensors

Station G2 and G3 can experience a reported SX1262 receiver failure mode in
which the apparent noise floor rises by roughly 20–30 dB and packet reception
stops until the radio is reinitialized. The optional workaround calls
RadioLib's `resetAGC()` during idle RX, then resumes continuous receive. Resets
are deferred during TX, standby, detected packet reception, and for 10 seconds
after a Station transmits so maintenance cannot interrupt the usual repeater
forwarding/response window. Each reset still creates a brief listening gap, so
periodic maintenance is disabled by default. Set `agc_reset_interval_sec` to
`4` in `/api/config` or use the Station AGC Recovery control in the modem
WebUI to enable the recommended four-second interval; changes apply
immediately without rebooting the modem, and `0` disables the workaround.

Station G2 and G3 support an optional Seeed Grove BME280 on the **Grove I2C**
connector (SDA GPIO5, SCL GPIO6, shared with the OLED and, on G3, the INA219).
It provides cached temperature, relative humidity, and pressure in
[`/api/stats` → `environment`](../API.md#get-apistats). Both `0x76` (default) and
`0x77` are supported. The separate Grove GPS/UART connector is not suitable.
No sensor configuration is required; an absent sensor leaves the modem
operational and reports unavailable readings. Run the Station G2/G3 host
regressions with `python3 firmware/tools/test_environment_sensor.py` (requires
PlatformIO, gcc, and g++; the runner fetches the pinned Bosch library if needed).

### RAK4631 battery and optional GPS

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

ESP32-P4 engineering-sample/pre-v3 and production-silicon binaries are not
interchangeable. Existing `esp32_p4_nano` / `ethermesh_1w` targets retain their
legacy SDK; `esp32_p4_nano_v3` / `ethermesh_1w_v3` use the production SDK and
matching bootloader with the v3.2 compatibility fixes. Stock compiled limits
are 1–199 and 301–399 respectively, not every revision below/above v3. Read the
[P4 silicon matrix, artifact contract and remaining flasher/hardware gates](p4-silicon.md)
before selecting firmware. Source support does not publish a new release or
enable automatic selection in the public flasher.

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
