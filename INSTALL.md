# Installation — step by step

All commands assume you are in the repository root `pymc_modem/`.

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
| RAK3112 WisMesh | `rak3112_wismesh` | `rak3112-<mac3>.local` | Wi-Fi |
| B&Q Consulting Station G2 | `station_g2` | `station-g2-<mac3>.local` | Wi-Fi |
| WaveShare ESP32-P4-Nano (+ off-board E22) | `esp32_p4_nano` | `p4nano-<mac3>.local` | **Ethernet or Wi-Fi** (runtime auto-select; cable plugged → ETH, no link → WiFi fallback. Both at once is unstable with radio active — see README "Porting to another ESP32-P4 board") |
| MeshSmith EtherMesh-1W | `ethermesh_1w` | `ethermesh-1w-<mac3>.local` | **Ethernet** |
| Heltec T114 | `heltec_t114` | n/a | none — USB-CDC + UART only |
| RAK4631 WisMesh Ethernet Gateway | `rak4631_wismesh_eth` | n/a (hostname is status-only) | **Ethernet** (W5100S, TCP 5055 + management UI on port 80) — no mDNS, no network OTA |
| Seeed XIAO nRF52840 + Wio-SX1262 | `xiao_nrf52_wio` | n/a | none — USB-CDC only |

The `esp32_p4_nano`, `ethermesh_1w`, `station_g2`, and `photon_1w_xiao_esp32c6` envs use the
[pioarduino fork](https://github.com/pioarduino/platform-espressif32)
(pinned in `platformio.ini`) for the Arduino-ESP32 3.x / ESP-IDF 5.x
toolchain; first build will fetch the platform package once.

### 1a. Browser flasher (recommended)

Use the openHop browser flasher for supported ESP32-family boards:

<https://flasher.openhop.dev/>

Pick your board, connect it over USB, and choose **Install** / **Update** from
the browser. Use the manual esptool, PlatformIO, or nRF52 DFU flows below when
you are building local firmware, recovering a board manually, or using a target
that is not yet published in the flasher.

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
`lilygo_t3s3`, `rak3112_wismesh`, `esp32_p4_nano`, `ethermesh_1w`, or `station_g2`.

nRF52 targets ship `firmware.hex`, `firmware.zip`, and `SHA256SUMS.txt` in
`firmware/<env>/` after the firmware asset workflow runs. Use the ZIP with
Adafruit nRF52 DFU, or double-click reset and use the board bootloader flow;
there are no ESP32-style bootloader/partition offsets for these targets.

`<env>` for nRF52 is one of: `heltec_t114`, `xiao_nrf52_wio`, or
`rak4631_wismesh_eth`.

```bash
pip install esptool

# Full flash (fresh board, first install) — replace the ENV/CHIP pair
# with the row that matches your board:
ENV=heltec_v3      ; CHIP=esp32s3   # also for heltec_v4 / heltec_v42 / heltec_v43 / heltec_tracker_v2 / ikoka_stick / xiao_wio_sx1262 / lilygo_t3s3 / rak3112_wismesh / station_g2
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

### 1d. OTA over the network (after the first flash, no cable)

**Only ESP32-family targets support network firmware upload.** nRF52 targets
(`heltec_t114`, `xiao_nrf52_wio`, `rak4631_wismesh_eth`) must be flashed via
USB with `pio run -e <env> -t upload` (Adafruit nRF52 DFU), or through the
modem protocol's OTA commands where available. The RAK4631 target does expose
an HTTP management UI on port 80, but it deliberately does not provide
ArduinoOTA or the `/update` upload route.

Once the board is on the LAN (Wi-Fi STA or Ethernet — ESP32 only) and
visible via mDNS:

```bash
cd firmware
pio run -e <env> -t upload --upload-port <env-stem>-<mac3>.local
# or HTTP directly:
curl -u admin:password -F firmware=@.pio/build/<env>/firmware.bin \
     http://<env-stem>-<mac3>.local/update
```

Hostname stems are listed in §1 (e.g. `heltec`, `heltec-v4`, `heltec-v42`,
`heltec-v43`, `tracker-v2`, `ikoka`, `xiao-wio`, `photon-c6`, `lilygo-t3s3`, `rak3112`, `station-g2`,
`p4nano`). The board reboots after upload.
The HTTP OTA page uses Basic Auth with username `admin` and default
password `password`; change it from the OTA page after first network boot.
Rollback is **not** automatic on a broken image — keep the USB cable
as a recovery fallback.

### Adding a new board

Copy the closest `firmware/include/boards/<env>.h`, edit pins / RF-switch
policy, add `-DBOARD_MY_BOARD` to a new `[env:my_board]` block in
`platformio.ini`, and a matching `#elif defined(BOARD_MY_BOARD)` arm in
`board_config.h`. ESP32-P4 carriers have a few quirks (boot strap on
GPIO35, RMII / Wi-Fi / radio interaction, LDO domain on high GPIOs)
covered in the README's
[Porting to another ESP32-P4 board](README.md#porting-to-another-esp32-p4-board)
section.

## 2. USB connection (`pymc_usb` radio type)

```bash
ls -la /dev/ttyACM* /dev/ttyUSB*
```

Usually `/dev/ttyUSB0` (CP2102) or `/dev/ttyACM0` (native CDC). Optional udev
rule for a stable symlink:

```bash
sudo tee /etc/udev/rules.d/99-lora-modem.rules << 'EOF'
SUBSYSTEM=="tty", ATTRS{idVendor}=="10c4", ATTRS{idProduct}=="ea60", SYMLINK+="lora-modem", MODE="0666"
EOF
sudo udevadm control --reload-rules && sudo udevadm trigger
```

(VID/PID `10c4:ea60` matches the CP2102 on the Heltec V3; for native USB-CDC
use `303a:1001`.)

## 3. Network / TCP configuration (optional, for `pymc_tcp` mode)

> **Security note — network TCP token:** fresh firmware defaults to an empty
> TCP token, so port 5055 is open to anyone on the same LAN segment until
> you set one. The firmware still filters non-RFC1918/link-local/loopback
> source addresses, but on a shared LAN an empty token is only safe on an
> isolated network. Set/change the TCP token from the device web UI (or via
> USB provisioning on supported Wi-Fi targets). On RAK4631 W5100S, browse to
> `http://<device-ip>/`; the default login is `admin` / `password`. The
> `PYMC_ETH_TOKEN` build flag remains only the first-boot default.

On first boot the modem starts an open access point `openHop-Modem-XXXX`.
Connect a phone/laptop to that AP, open `http://192.168.4.1`, pick your
Wi-Fi + password, hit **Save & Restart**.

The RAK4631 Ethernet target does not create an access point. Connect it to
Ethernet, find the DHCP lease in your router, and open `http://<device-ip>/`.
From there you can switch to a static IPv4 configuration, change the openHop
TCP port/token and HTTP password, inspect live status, or reboot the modem.
Changes to network or TCP settings are applied after an automatic reboot.

Alternatively — **provisioning over USB** (doesn't require access to the
temporary setup AP):

```python
import asyncio
from pymc_driver.usb_radio import USBLoRaRadio

async def provision():
    r = USBLoRaRadio(port="/dev/ttyUSB0")
    r.begin()
    resp = await r.set_wifi_credentials(
        ssid="MyLAN", password="...",
        tcp_port=5055, tcp_token="",   # token="" means open LAN
    )
    print(resp)   # device reboots into STA; reconnect after ~10s
    r.cleanup()

asyncio.run(provision())
```

Check the status after reconnect:

```python
async def check():
    r = USBLoRaRadio(port="/dev/ttyUSB0")
    r.begin()
    status = await r.get_wifi_status()
    print(status)
    # {'mode_name': 'sta', 'ip': '192.168.1.50',
    #  'mdns': 'heltec-abcdef.local', ...}
```

## 4. Standalone connection test (without Repeater)

```bash
pip install pyserial
python3 pymc_driver/test_modem.py /dev/ttyUSB0
```

You should see `PONG`, `CONFIG_RESP`, `STATUS_RESP`, `CAD_RESP`, `TX_DONE`.

## 5. Configure Repeater

Current Repeater releases include the openHop Modem drivers and know the
`pymc_usb` and `pymc_tcp` radio types directly. Do **not** copy drivers into
openHop Core or patch Repeater by hand for normal installs.

Example `/etc/pymc_repeater/config.yaml` for a LAN/Wi-Fi modem:

```yaml
radio_type: pymc_tcp

radio:
  frequency: 869618000       # MeshCore EU Narrow / Switzerland
  bandwidth: 62500
  spreading_factor: 8
  coding_rate: 8             # 4/8
  tx_power: 22
  sync_word: 18              # 0x12, private
  preamble_length: 16
  cad:
    peak_threshold: 23
    min_threshold: 11

pymc_tcp:
  host: 192.168.1.50          # modem LAN IP or mDNS name
  port: 5055
  token: ""                  # empty = open LAN
  connect_timeout: 5.0
  lbt_enabled: true
  lbt_max_attempts: 5
```

USB modem alternative:

```yaml
radio_type: pymc_usb

pymc_usb:
  port: /dev/ttyUSB0
  baudrate: 921600
  lbt_enabled: true
  lbt_max_attempts: 5
```

The old `scripts/install.sh` / `patches/` workflow is kept only as legacy
reference material for pre-integration Repeater/Core installs.

## 6. Start the repeater

```bash
sudo systemctl restart pymc-repeater
sudo journalctl -u pymc-repeater -f
```

Expected log lines:

```
TCPLoRaRadio configured: 192.168.1.50:5055 (auth=open), freq=869.6MHz, ...
TCP connected to 192.168.1.50:5055
Modem PONG received — alive
Radio configured: 869.6MHz SF8 BW62kHz 22dBm sync=0x0012 pre=16
CAD thresholds pushed peak=23 min=11: OK
RX callback registered
Retransmitted packet (X bytes, Yms airtime)   ← mesh forwarding is live
```

## 7. Verification checklist

- **Firmware version:** the STATUS screen shows it after the boot
  splash. Or programmatically:
  ```python
  await radio.get_version()   # e.g. "v0.8.0-heltec" / "-esp32_p4" / "-heltec_t114"
  ```
- **OLED screen cycle** (short PRG taps): SLEEP → STATUS → RADIO → DIAGNOSTICS.
  The RADIO screen shows the live chip configuration (freq, SF, BW, CR,
  power, sync, preamble). The DIAGNOSTICS screen shows uptime, the TCP
  client IP, the age of the last USB command, and RX/TX/CRC counters.
- **Uptime grows monotonically** — it should no longer reset every 60 s
  (that was the firmware-hang symptom fixed between v0.5.4 and v0.5.8).
- **CAD actually works** — `Modem error: 0x07` in the repeater log should
  be infrequent, not routine. Around ~27 % failure at SF8/62.5k is the
  baseline SX1262 IRQ-miss rate (same as on the SPI HAT reference).

## 8. Docker deployment (alternative to native install)

The Docker image runs Repeater with the built-in `pymc_tcp` / `pymc_usb`
radio support. Default transport is `pymc_tcp` — the modem lives on the LAN and
the container has no need for `--device` or dialout group membership unless you
switch to USB mode.

### Build and run

```bash
# Set PYMC_TCP_HOST in a .env file next to docker-compose.yml first
# (or leave the placeholder and finish setup from the web UI's
# "pymc_tcp config" panel afterwards).
docker compose up -d --build
docker compose logs -f
```

`docker compose up -d --build` ignores the `image:` line and builds
locally from `docker/Dockerfile`. To run the published image without
rebuilding, drop `--build`: `docker compose pull && docker compose up
-d` will fetch `itkeny/pymc-usb-repeater:latest` from Docker Hub.

### Releasing a new image to Docker Hub (maintainer only)

A GitHub Action at `.github/workflows/docker-publish.yml` builds
multi-arch (linux/amd64 + linux/arm64) and pushes on every git tag
starting with `v*`:

```bash
git tag v0.8.0
git push origin v0.8.0
# Action runs ~5 minutes, pushes itkeny/pymc-usb-repeater:v0.8.0
# and :latest. Watch progress under the repo's Actions tab.
```

The workflow needs two repo secrets (Settings → Secrets → Actions):
`DOCKERHUB_USERNAME` and `DOCKERHUB_TOKEN` (a Docker Hub Access Token
with Read & Write scope, NOT the account password). Manual rebuilds
without bumping the tag: `Actions → Publish Docker image → Run
workflow` — pushes only `:latest`.

Dashboard: `http://localhost:8000`. Three host bind mounts under
`./data/` (relative to the compose file) keep config / database / logs
on the host filesystem so they survive `docker rm`, can be backed up
with the usual file tools, and can be edited without `docker exec`:

| Host path             | Container mount             | Purpose                              |
|-----------------------|-----------------------------|--------------------------------------|
| `./data/config/`      | `/etc/pymc_repeater`        | `config.yaml`, identity files        |
| `./data/state/`       | `/var/lib/pymc_repeater`    | `radio-settings.json`, SQLite, MQTT  |
| `./data/logs/`        | `/var/log/pymc_repeater`    | `repeater.log`                       |

The directories are auto-created on first start. The container starts
as root just long enough to chown them to its `repeater` user, then
drops privileges via `gosu` — so the daemon never runs as root and the
files are still owned by the same uid every time.

### Environment variables

The entrypoint applies env-var overrides on every container start —
change a value in `docker-compose.yml` and `docker compose up -d` to
re-stamp the running config.

| Variable                  | Default       | Notes                                      |
|---------------------------|---------------|--------------------------------------------|
| `RADIO_TYPE`              | `pymc_tcp`    | `pymc_tcp` or `pymc_usb`                   |
| `PYMC_TCP_HOST`           | `192.168.1.50`| Modem LAN IP or `ikoka-XXXXXX.local` etc.  |
| `PYMC_TCP_PORT`           | `5055`        | Firmware TCP listener                      |
| `PYMC_TCP_TOKEN`          | *(empty)*     | Match the firmware NVS auth token          |
| `PYMC_TCP_CONNECT_TIMEOUT`| `5.0`         | Seconds — raise on slow Wi-Fi              |
| `SERIAL_PORT`             | `/dev/ttyUSB0`| Used when `RADIO_TYPE=pymc_usb`            |
| `BAUDRATE`                | `921600`      | USB-CDC baudrate (must match firmware)     |
| `NODE_NAME`               | `openHop_RPT` | Repeater node name in the mesh             |
| `ADMIN_PASSWORD`          | `admin123`    | Web UI admin — change before exposing      |
| `FREQUENCY`               | `869618000`   | Hz                                         |
| `TX_POWER`                | `22`          | dBm                                        |
| `BANDWIDTH`               | `62500`       | Hz                                         |
| `SPREADING_FACTOR`        | `8`           |                                            |
| `CODING_RATE`             | `8`           | 4/8                                        |
| `SYNC_WORD`               | `18`          | `0x12` (private)                           |
| `PREAMBLE_LENGTH`         | `16`          | symbols                                    |

The legacy `HELTEC_HOST` / `HELTEC_PORT` / `HELTEC_TOKEN` /
`HELTEC_CONNECT_TIMEOUT` env vars are still honoured as fallbacks so
pre-rename `.env` files keep working without edits.

### USB mode in containers

USB-CDC requires passing the device through and matching the dialout group:

```bash
docker run -d --name repeater \
  -p 8000:8000 \
  --device=/dev/ttyUSB0:/dev/ttyUSB0 \
  -e RADIO_TYPE=pymc_usb -e SERIAL_PORT=/dev/ttyUSB0 \
  itkeny/pymc-usb-repeater:latest
```

Or in `docker-compose.yml`, uncomment both `SERIAL_PORT` and the
`devices:` block.

### Deferred-connect

If `PYMC_TCP_HOST` stays at the placeholder (or is left unset), the
container does **not** abort. `TCPLoRaRadio` enters deferred-connect
mode and the entrypoint logs `[WARN] Modem not reachable yet` —
finish provisioning by clicking **pymc_tcp config** in the web UI's
bottom-right corner and entering the real host. The driver reconnects
on the fly with no service restart.

## File placement summary

| Source file                    | Purpose                                          |
|--------------------------------|--------------------------------------------------|
| `firmware/*.bin`               | Manual flash / OTA artifacts when not using the browser flasher |
| `pymc_driver/usb_radio.py`     | Standalone local probe/debug helper              |
| `pymc_driver/tcp_radio.py`     | Standalone local probe/debug helper              |
| `patches/`                     | Legacy reference material for old installs only  |

Current Repeater releases already ship the modem radio support. Users should
select `radio_type: pymc_usb` or `radio_type: pymc_tcp`; they should not copy
these files into Repeater/openHop Core for a normal install.
