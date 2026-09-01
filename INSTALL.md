# Installation — step by step

All commands assume you are in the `openhop_modem` repository root.

## 1. Flash the firmware

The same source tree builds one firmware target per board; pick the env that matches
your board:

| Board | PlatformIO env | mDNS name | Network |
|---|---|---|---|
| Heltec WiFi LoRa 32 V3 | `heltec_v3` | `heltec-<mac3>.local` | Wi-Fi |
| Heltec WiFi LoRa 32 V4 | `heltec_v4` | `heltec-v4-<mac3>.local` | Wi-Fi |
| Heltec WiFi LoRa 32 V4.2 | `heltec_v42` | `heltec-v42-<mac3>.local` | Wi-Fi |
| Heltec WiFi LoRa 32 V4.3 | `heltec_v43` | `heltec-v43-<mac3>.local` | Wi-Fi |
| Heltec Wireless Tracker V2 | `heltec_tracker_v2` | `tracker-v2-<mac3>.local` | Wi-Fi |
| Ikoka Stick (XIAO ESP32-S3 + E22P868M30S) | `ikoka_stick` | `ikoka-<mac3>.local` | Wi-Fi |
| Seeed XIAO Wio-SX1262 | `xiao_wio_sx1262` | `xiao-wio-<mac3>.local` | Wi-Fi |
| MeshSmith Photon-1W ESP32-C6 | `photon_1w_xiao_esp32c6` | `photon-c6-<mac3>.local` | Wi-Fi |
| LilyGO T-LoRa T3-S3 v1.2/v1.3 | `lilygo_t3s3` | `lilygo-t3s3-<mac3>.local` | Wi-Fi |
| LilyGO T-Beam-S3 Supreme | `lilygo_tbeam_s3_supreme` | `lilygo-tbeam-s3-supreme-<mac3>.local` | Wi-Fi |
| RAK3112 WisMesh | `rak3112_wismesh` | `rak3112-<mac3>.local` | Wi-Fi |
| B&Q Consulting Station G2 | `station_g2` | `station-g2-<mac3>.local` | Wi-Fi |
| BQ Voyage Station G3 | `station_g3` | `station-g3-<mac3>.local` | Wi-Fi |
| WaveShare ESP32-P4-Nano (+ off-board E22) | `esp32_p4_nano` | `p4nano-<mac3>.local` | **Ethernet or Wi-Fi** (runtime auto-select; cable plugged → ETH, no link → WiFi fallback. Both at once is unstable with radio active — see README "Porting to another ESP32-P4 board") |
| MeshSmith EtherMesh-1W | `ethermesh_1w` | `ethermesh-1w-<mac3>.local` | **Ethernet** |
| Heltec T114 | `heltec_t114` | n/a | none — USB-CDC + UART only |
| RAK4631 USB | `rak4631_usb` | n/a | none — USB-CDC only |
| RAK4631 WisMesh Ethernet Gateway | `rak4631_wismesh_eth` | n/a (use DHCP lease/IP) | **Ethernet** (W5100S, TCP 5055 + WebUI/API 80) — no mDNS; WebUI OTA disabled; Bluetooth DFU OTA supported |
| Seeed XIAO nRF52840 + Wio-SX1262 | `xiao_nrf52_wio` | n/a | none — USB-CDC only |
| RAKwireless RAK3401 (RAK13302 1 W front end) | `rak3401` | n/a | none — USB-CDC only |

The `esp32_p4_nano`, `ethermesh_1w`, `station_g2`, `station_g3`, and `photon_1w_xiao_esp32c6` envs use the
[pioarduino fork](https://github.com/pioarduino/platform-espressif32)
(pinned in `platformio.ini`) for the Arduino-ESP32 3.x / ESP-IDF 5.x
toolchain; first build will fetch the platform package once.

### 1a. Browser flasher (recommended)

Use the openHop browser flasher for supported modem boards:

<https://flasher.openhop.dev/>

Pick your board, connect it over USB, and flash from the browser. For a local
RAK4631 build, choose **Custom Firmware** and select
`firmware/rak4631_wismesh_eth/firmware.zip`. Stop anything using the serial
port, click **Enter DFU mode**, and select the application port. Then click
**Flash** and select the newly appearing `WisBlock RAK4631` bootloader port. If
the DFU button does nothing, quickly press RESET twice; do not hold it. Use the
manual esptool, PlatformIO, or nRF52 DFU flows below for recovery or targets not
published in the flasher.

### 1b. Prebuilt firmware binaries (no PlatformIO)

ESP32-family `firmware/<env>/` subdirectories ship a combined factory image
for first installs and the individual build images used by browser flashing
and app-only updates:

| Path                                  | Flash use |
|---------------------------------------|-----------|
| `firmware/<env>/firmware.factory.bin` | Complete first install/recovery image at `0x0` |
| `firmware/<env>/bootloader.bin`       | Bootloader component; offset is chip-specific |
| `firmware/<env>/partitions.bin`       | Partition-table component |
| `firmware/<env>/firmware.bin`         | App-only USB/OTA update at `0x10000` |

The factory image includes the bootloader, partition table, OTA initialization
data, and application at the offsets selected by the target's PlatformIO
toolchain. In particular, ESP32-P4 bootloaders start at `0x2000`, so do not use
a generic hand-written multi-image command for a fresh P4 install.

`<env>` is one of: `heltec_v3`, `heltec_v4`, `heltec_v42`, `heltec_v43`,
`heltec_tracker_v2`, `ikoka_stick`, `xiao_wio_sx1262`, `photon_1w_xiao_esp32c6`,
`lilygo_t3s3`, `lilygo_tbeam_s3_supreme`, `rak3112_wismesh`, `esp32_p4_nano`,
`ethermesh_1w`, `station_g2`, or `station_g3`.

nRF52 targets ship `firmware.hex`, `firmware.zip`, `firmware.uf2`, and
`SHA256SUMS.txt` in `firmware/<env>/` after the firmware asset workflow runs.
The RAK target also includes `firmware.ota` for the currently disabled staged
Ethernet OTA design; use `firmware.zip`, not `firmware.ota`, with the browser
flasher.
Use the ZIP with Adafruit nRF52 DFU, or double-click reset and use the board
bootloader flow; there are no ESP32-style bootloader/partition offsets for
these targets.

`<env>` for nRF52 is one of: `heltec_t114`, `xiao_nrf52_wio`,
`rak4631_usb`, `rak4631_wismesh_eth`, or `rak3401`.

A direct `pio run -e xiao_nrf52_wio` source build also creates
`.pio/build/xiao_nrf52_wio/firmware.uf2` for drag-and-drop flashing.

```bash
pip install esptool

# Full flash (fresh board, first install) — replace the ENV/CHIP pair
# with the row that matches your board:
ENV=heltec_v3      ; CHIP=esp32s3   # also for heltec_v4 / heltec_v42 / heltec_v43 / heltec_tracker_v2 / ikoka_stick / xiao_wio_sx1262 / lilygo_t3s3 / lilygo_tbeam_s3_supreme / rak3112_wismesh / station_g2 / station_g3
# ENV=photon_1w_xiao_esp32c6 ; CHIP=esp32c6
# ENV=esp32_p4_nano ; CHIP=esp32p4  # also for ethermesh_1w

esptool.py --chip $CHIP --port /dev/ttyUSB0 --baud 921600 write_flash \
    0x0 firmware/$ENV/firmware.factory.bin

# App-only update (board that already has a matching bootloader):
esptool.py --chip $CHIP --port /dev/ttyUSB0 --baud 921600 write_flash \
    0x10000 firmware/$ENV/firmware.bin
```

> **ESP32-P4-Nano flash port:** the WaveShare board exposes the chip's
> native USB-Serial-JTAG on one of its USB-C ports (`/dev/cu.usbmodem*`
> on macOS, `/dev/ttyACM*` on Linux); use that one for esptool. The
> other USB-C port (CH343P → UART0) shows up as
> `/dev/cu.wchusbserial*` / `/dev/ttyUSB*` and is for `Serial.printf`
> debug only — not for flashing. If esptool can't auto-enter download
> mode, hold **BOOT (Key1)**, briefly press **RESET (Key2)**, release
> RESET, release BOOT, then re-run.

> **EtherMesh-1W flash port:** the ESP32-P4-ETH board uses its CH343P USB-UART
> bridge for flashing/debug. Use the `/dev/ttyUSB*` / `/dev/cu.wchusbserial*`
> port and `CHIP=esp32p4`.

> **Station G3 power and download mode:** install the LoRa antenna before
> powering the motherboard. The BQESP32V1M daughterboard's USB-C socket carries
> data but does not power the board, so keep the Station G3 motherboard powered
> from its 15 V USB-C PD input or a suitable 9–19 V DC supply while the MCU
> daughterboard is connected to the computer. To force download mode, hold the
> firmware-download button, press and release reset, wait about five seconds,
> then release the firmware-download button. Press reset once after flashing if
> the application does not restart automatically.

> **Station G3 RF jumpers:** this firmware defaults PA PL1 to LOW (the lower PA
> level); the Station G3-only RF Front-End panel can persistently select the
> higher GPIO9 mode. It dynamically drives LNA P LOW for receive / HIGH for
> transmit. Remove the PA PL1 and LNA P jumpers for those GPIO9/GPIO10 controls
> to take effect.
> PA PL2 remains a physical jumper. Always select a legal SX1262 drive level for
> the configured PA mode; the firmware conservatively clamps chip drive to
> 19 dBm, but the external PA produces substantially higher antenna power.

On macOS the port is usually `/dev/cu.usbmodem*` for the Ikoka (native
USB-CDC) or `/dev/cu.usbserial-*` for the Heltec (CP2102). If the board
doesn't enter flash mode automatically, hold **BOOT** while plugging in
USB and release it once `esptool.py` starts. After flashing press
**RST** or replug USB.

### 1c. Build and flash with PlatformIO

```bash
cd firmware
pio run -e <env> -t upload          # USB cable
./build_release.sh                  # refresh every prebuilt at once
```

XIAO ESP32-S3 (Ikoka) sometimes needs a manual bootloader entry —
double-tap RESET, or hold BOOT while plugging USB. ESP32-P4-Nano
download mode: hold **BOOT (Key1)**, briefly press **RESET (Key2)**,
release RESET, release BOOT.

### 1d. OTA updates after the first flash

**Only ESP32-family targets with the OTA/HTTP stack** support WebUI firmware
upload. Most nRF52 targets (`heltec_t114`, `xiao_nrf52_wio`, `rak4631_usb`, and
`rak3401`)
use USB with `pio run -e <env> -t upload` (Adafruit nRF52 DFU). The RAK4631
WisMesh Ethernet Gateway is the exception: it supports OTA updates through its
working Bluetooth DFU flow. WebUI `/update` remains absent because safe staged
activation and recovery would require a custom openHop Modem bootloader, which
is not currently planned. Its generated `firmware.ota` is therefore
packaging/staging groundwork only. Use the generated `firmware.zip` for
Bluetooth DFU OTA; USB DFU remains available as a recovery path.

Once the board is on the LAN (Wi-Fi STA or Ethernet — ESP32 only) and
visible via mDNS:

```bash
cd firmware
pio run -e <env> -t upload --upload-port <env-stem>-<mac3>.local
# or HTTP directly:
curl -u admin:openhop -F firmware=@.pio/build/<env>/firmware.bin \
     http://<env-stem>-<mac3>.local/update
```

Hostname stems are listed in §1 (e.g. `heltec`, `heltec-v4`, `heltec-v42`,
`heltec-v43`, `tracker-v2`, `ikoka`, `xiao-wio`, `photon-c6`, `lilygo-t3s3`,
`lilygo-tbeam-s3-supreme`, `rak3112`, `station-g2`, `p4nano`). The board
reboots after upload.
The HTTP OTA page uses Basic Auth with username `admin` and default
password `openhop`; change it from the OTA page after first network boot.
Rollback is **not** automatic on a broken image — keep the USB cable
as a recovery fallback.

For the RAK4631, find the assigned address in the DHCP lease table and open
`http://<rak-ip>/`. The initial HTTP credentials are `admin` / `openhop`.
Change the HTTP password and openHop TCP token from the page. Hostname,
DHCP/static networking, TCP port/token, and optional compile-gated GPS settings
are stored atomically; the page states when reboot is required. Port 80 is
reserved for management. The RAK-only **Bluetooth DFU** action closes the HTTP
response and Ethernet socket before handing off to the installed Nordic BLE DFU
bootloader. Then use a Nordic-compatible DFU app to select the bootloader's
Bluetooth device and upload `firmware/rak4631_wismesh_eth/firmware.zip`. The
Bluetooth name is bootloader-defined; do not require a fixed name. Complete BLE
DFU uploads are validated on the production gateway, making this the supported
OTA update path for deployed units. Keep USB serial DFU available for recovery
from an interrupted transfer. Do not assume the production gateway exposes a
UF2 mass-storage disk; use its BLE DFU target or USB serial DFU port.

### Adding a new board

Copy the closest `firmware/include/boards/<env>.h`, edit pins / RF-switch
policy, add `-DBOARD_MY_BOARD` to a new `[env:my_board]` block in
`platformio.ini`, and a matching `#elif defined(BOARD_MY_BOARD)` arm in
`board_config.h`. ESP32-P4 carriers have a few quirks (boot strap on
GPIO35, RMII / Wi-Fi / radio interaction, LDO domain on high GPIOs)
covered in the README's
[Porting to another ESP32-P4 board](README.md#porting-to-another-esp32-p4-board)
section.

## 2. Connect over USB (`modem_usb`)

```bash
ls -la /dev/serial/by-id/* /dev/ttyACM* /dev/ttyUSB*
```

Prefer a stable `/dev/serial/by-id/...` path when the board exposes one.
Numbered `/dev/ttyACM*` and `/dev/ttyUSB*` paths can change after reconnecting
or rebooting. The normal USB-CDC baud rate is `921600`.

An optional udev rule can provide a short stable symlink for a known VID/PID:

```bash
sudo tee /etc/udev/rules.d/99-openhop-modem.rules <<'EOF'
SUBSYSTEM=="tty", ATTRS{idVendor}=="10c4", ATTRS{idProduct}=="ea60", SYMLINK+="openhop-modem", MODE="0660", GROUP="dialout"
EOF
sudo udevadm control --reload-rules
sudo udevadm trigger
```

The example VID/PID is the CP2102 used by Heltec V3. Match the rule to the
actual board shown by `udevadm info` rather than applying it blindly. The
RAK4631 WisMesh Ethernet USB product is now `WisMesh-openHop-Ethernet`; after
flashing this firmware, reselect any `/dev/serial/by-id/...` path created by an
earlier firmware descriptor.

## 3. Connect over Wi-Fi or Ethernet (`modem_tcp`)

Fresh Wi-Fi firmware starts the open access point `openHop-Modem-XXXX`.
Connect a phone or laptop, open `http://192.168.4.1`, select the LAN, enter its
password, and choose **Save & Restart**. You can also configure Wi-Fi and the TCP
token from the device web UI after it joins the LAN.

> **Security note — network TCP token:** fresh firmware defaults to an empty
> TCP token, so port 5055 is open to anyone on the same LAN segment until
> you set one. The firmware still filters non-RFC1918/link-local/loopback
> source addresses, but on a shared LAN an empty token is only safe on an
> isolated network. On web-enabled firmware, including the RAK4631 W5100S
> Ethernet target, set/change the TCP token from the authenticated device web
> UI. RAK JSON responses expose only whether a token is set, never its value.

The modem protocol listens on TCP port `5055`. Fresh firmware defaults to an
empty token, which permits any client on the local LAN. Set a token before using
the modem on an untrusted or shared network. The firmware rejects non-LAN
source addresses, but that is not a substitute for local authentication.

Wired targets use DHCP unless their board configuration says otherwise. The
RAK4631/W5100S target has no mDNS; find its address in the DHCP lease table,
then use its authenticated WebUI on port 80 to configure the hostname, DHCP or
static network settings, HTTP password, and openHop TCP token. The canonical
`OPENHOP_ETH_*` build flags remain available for custom defaults. Older
`PYMC_ETH_*` overrides remain accepted as compatibility aliases so existing
custom builds do not silently lose their TCP token or hardware policy; when
both forms are supplied, `OPENHOP_ETH_*` wins.

## 4. Configure openHop Repeater

openHop Repeater and openHop Core contain the USB and TCP modem drivers. The
canonical `modem_usb` / `modem_tcp` names require the coordinated Repeater/Core
transport-naming release; deploy that migration before using these examples.
Upgrade older Repeater installations rather than copying drivers from this
repository or patching an installed service. Use the Repeater setup wizard, or
configure one of the canonical transports.

TCP example:

```yaml
radio_type: modem_tcp

radio:
  frequency: 869618000
  bandwidth: 62500
  spreading_factor: 8
  coding_rate: 8
  tx_power: 22
  sync_word: 18
  preamble_length: 16

modem_tcp:
  host: 192.168.1.50
  port: 5055
  token: ""
  connect_timeout: 5.0
  lbt_enabled: true
  lbt_max_attempts: 5
```

USB example:

```yaml
radio_type: modem_usb

modem_usb:
  port: /dev/serial/by-id/REPLACE_WITH_MODEM_DEVICE
  baudrate: 921600
  lbt_enabled: true
  lbt_max_attempts: 5
```

For Docker, pass the real host device into the Repeater container and use the
path visible inside that container. Install and operate the container from the
openHop Repeater repository or its published image; this firmware repository
does not build a separate Repeater image.

## 5. Start and verify Repeater

```bash
sudo systemctl restart openhop-repeater
sudo journalctl -u openhop-repeater -f
```

Expected behavior:

- the modem answers `PONG` and reports its firmware version;
- the configured frequency, spreading factor, bandwidth, coding rate, power,
  sync word, and preamble are accepted;
- CAD thresholds apply without repeated timeout errors;
- receive callbacks remain active and mesh packets appear in Repeater metrics;
- network modems reconnect after a temporary LAN interruption;
- USB modems reopen through the configured stable device path after reconnect.

The device display or HTTP statistics page can independently confirm uptime,
radio state, packet counters, network state, and firmware version. Do not run a
transmit test until the antenna, legal region, frequency, power, and external PA
configuration have been checked for the exact board.

## 6. Publish firmware release assets (maintainers)

Publishing a GitHub Release runs
`.github/workflows/publish-firmware-release-assets.yml`. The workflow checks out
the exact release tag, validates every tracked `firmware/<env>/SHA256SUMS.txt`,
and attaches one independently downloadable ZIP per firmware environment plus a
release-level checksum manifest.

To package an existing tag locally without uploading:

```bash
python3 firmware/tools/package_release_assets.py \
    --tag v1.3.0 --output-dir /tmp/openhop-release
(cd /tmp/openhop-release && sha256sum -c *-SHA256SUMS.txt)
```

To retry or backfill a published tag, use **Actions → Publish Firmware Release
Assets → Run workflow** and supply that existing tag. The workflow replaces its
own generated release assets; it does not create a second release.
