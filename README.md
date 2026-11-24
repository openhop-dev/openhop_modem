# openHop Modem

<a id="openhop-modem--usbtcp-lora-modem-for-openhop-core"></a>

USB and TCP LoRa modem firmware for [openHop Repeater](https://github.com/openhop-dev/openhop_repeater)
on supported ESP32 and nRF52 boards with SX1262 radios.

[![Latest release](https://img.shields.io/github/v/release/openhop-dev/openhop_modem?label=release)](https://github.com/openhop-dev/openhop_modem/releases/latest)
[![Firmware asset build](https://img.shields.io/github/actions/workflow/status/openhop-dev/openhop_modem/build-firmware-assets.yml?branch=main&label=firmware)](https://github.com/openhop-dev/openhop_modem/actions/workflows/build-firmware-assets.yml)
[![License: MIT](https://img.shields.io/badge/license-MIT-blue.svg)](LICENSE)

## Acknowledgements

Special thanks to [itk80](https://github.com/itk80) (KeenKJ), whose early
firmware, modem-driver, integration, and hardware-support work laid the
groundwork that became openHop Modem. Without his work, this project would
not exist.

[Flash a device](https://flasher.openhop.dev/) · [Installation](INSTALL.md) · [Hardware](docs/hardware.md) · [Documentation](#documentation) · [Changelog](CHANGELOG.md)

The modem handles packet transmission, reception, CAD and radio configuration.
openHop Repeater handles the network protocol, routing, application encryption
and retries.

<a id="meshcore-companion-compatibility"></a>

> **Not a MeshCore companion:** The modem's TCP interface is **not compatible
> with the MeshCore companion protocol**. The MeshCore app, Remote Terminal and
> MeshMonitor **cannot connect directly to the modem as a companion**.
> For companion access, connect the modem to openHop Repeater and use a
> **virtual companion** configured in Repeater. The client connects to Repeater's
> companion endpoint, not the modem TCP port.

> Development source can be ahead of published firmware. Use the matching board
> package from [Releases](https://github.com/openhop-dev/openhop_modem/releases)
> or the browser flasher. A source version change does not regenerate or relabel
> the checked-in binaries.

## Installation

1. **Identify the hardware.** Match the board and fitted RF module.
2. **Flash the board.** Start with the
   [browser flasher](https://flasher.openhop.dev/). For MeshSmith EtherMesh-1W,
   the flasher automatically selects the correct firmware. Use
   [manual flashing and recovery](INSTALL.md#1-flash-the-firmware) for local
   builds, unpublished targets or bootloader recovery.
3. **Connect the host.** Use USB serial, or provision Wi-Fi/connect Ethernet on a
   network-capable board. The normal USB baud rate is `921600`; the primary TCP
   port is `5055`. Your board profile determines which transports exist.
4. **Configure openHop Repeater.** Select `modem_usb` or `modem_tcp` and set
   the radio parameters. Check the antenna/load, fitted band, region and legal
   power before TX.

The [installation guide](INSTALL.md) includes environment names, bootloader/OTA
choices, network provisioning and host configuration examples. You do not need
to copy a driver from this repository or use a modem-specific Repeater image.

<a id="per-board-pin-map"></a>
<a id="e22p-rf-switch-ikoka-p4-nano--e22p"></a>
<a id="porting-to-another-esp32-p4-board"></a>
<a id="on-device-display"></a>

## Supported hardware

| Family | Network transport | Selection notes |
|---|---|---|
| Heltec V3, V4, V4.2, V4.3 and Wireless Tracker V2 | Wi-Fi | Use the exact board/FEM revision. |
| LilyGO T3-S3, T-Beam-S3 Supreme and T-Beam 1W | Wi-Fi | Separate power, display and PA policies. |
| XIAO ESP32-S3 Wio-SX1262, Ikoka Stick and RAK3112 WisMesh | Wi-Fi | Match the carrier and fitted RF module. |
| MeshSmith Photon-1W ESP32-C6 | Wi-Fi | Dedicated C6/Photon profile. |
| Station G2 and Station G3 | Wi-Fi | High-power PA/LNA and board-specific controls. |
| MeshSmith EtherMesh-1W | Ethernet | Select MeshSmith EtherMesh-1W in the browser flasher. |
| Waveshare ESP32-P4-Nano | Ethernet or Wi-Fi | Runtime fallback, not simultaneous ETH/Wi-Fi with the radio. |
| Heltec T114, RAK4631 USB, RAK3401 and XIAO nRF52840 Wio-SX1262 | No IP transport | USB; UART only where the board declares it. |
| RAK4631 WisMesh Ethernet | Ethernet | W5100S, no mDNS; USB fallback and Bluetooth DFU update path. |

See the [hardware reference](docs/hardware.md) for the full board list, front-end
policy, supply/jumper requirements, sensors and display behavior. Exact
PlatformIO environment names are in [INSTALL.md](INSTALL.md#1-flash-the-firmware).

An external PA's advertised output is not the SX1262 command power. Follow the
fitted module and board limits rather than treating a 1 W label as a chip setting.

<a id="network-exposure-lan-only-by-design"></a>
<a id="dhcp-classless-static-routes-option-121"></a>
<a id="wi-fi-outage-recovery"></a>
<a id="web-ui--ota--json-api-authentication-v08"></a>

## Connection and management

| Interface | Purpose |
|---|---|
| USB serial | Binary modem protocol for the primary radio. |
| TCP 5055 | Modem protocol for openHop Repeater, [not a MeshCore companion connection](#meshcore-companion-compatibility). |
| HTTP port 80 | Authenticated WebUI and management API where supported. |

Fresh Wi-Fi boards expose `openHop-Modem-XXXX` for setup at
`http://192.168.4.1`. Wired targets normally use DHCP. RAK4631 Ethernet has no
mDNS, so use its DHCP lease/IP.

The normal management login defaults to `admin` / `openhop`. The TCP token is a
separate credential and starts empty on fresh firmware. Set the HTTP password
and TCP token before using a shared LAN. The firmware admits only the source
ranges defined in its LAN filter; framing and authentication do not provide TLS.

The multi-radio Home page shows status. Stats holds per-radio detail, and
Settings holds network/security and optional-radio selection. See the
[HTTP API](API.md) and [networking reference](docs/networking.md) for admission,
DHCP routes, Wi-Fi recovery and authentication behavior.

ESP32 network builds support their documented HTTP OTA path. nRF52 targets use
their board's bootloader flow; the RAK4631 Ethernet gateway supports Bluetooth
DFU, not HTTP firmware activation. Keep the matching USB recovery path available.

<a id="one-modem-separate-applications"></a>

## Architecture

```text
openHop Repeater
        │ USB or TCP
        ▼
Modem transport + shared radio runtime
        │ board-specific hardware / RF front end
        ▼
SX1262 radio(s)
```

openHop Repeater owns the network behavior. Firmware owns the physical radio
and never silently forwards between radio slots. Board profiles define GPIO,
front-end, power, display and network policy.

The shared firmware has general support for multiple radios on one board.
Each radio maintains its own configuration and status, while the board profile
defines the available hardware and interfaces. See the
[architecture reference](docs/multi-radio.md) for runtime details and validation
boundaries.

<a id="wire-protocol-v08"></a>
<a id="deferred-rf-controls"></a>
<a id="frame-format"></a>
<a id="host--modem"></a>
<a id="modem--host"></a>
<a id="default-radio-parameters"></a>

## Protocol

The existing framed protocol carries commands, replies and asynchronous packet
events. TCP selects the owning radio; USB/UART does not multiplex a secondary
radio. Hosts should probe capabilities instead of inferring them from versions.

- [Binary protocol reference](docs/protocol.md): frame/command tables, deferred
  controls, payload layouts and default receive policy.
- [TCP client reference](docs/tcp-client-integration.md): authentication,
  startup, packet handling and reconnect behavior.

`firmware/include/protocol.h` defines constants and packed structures. LoRa TX
payloads are limited to 255 bytes; RX frames also carry six metadata bytes.
Local TX completion is not acknowledgment from a distant peer.

<a id="project-layout"></a>
<a id="firmware-asset-builds"></a>
<a id="asset-update-pr-authentication"></a>

## Development

Firmware, board definitions and native regression runners live under `firmware/`.
Host-side integration is maintained with openHop Repeater.

See [firmware development](docs/development.md) for local builds, generated assets,
workflow credentials and exact-tag release packaging. Automatic firmware builds
are tied to relevant `main` pushes, not `dev` pushes. A documentation change does
not need a firmware rebuild, generated manifest update, flash or RF test.

## Documentation

| Reference | Use it for |
|---|---|
| [Installation](INSTALL.md) | Board environments, flashing, provisioning, host setup and recovery. |
| [Hardware](docs/hardware.md) | Front ends, pins/policy, power, sensors and displays. |
| [Networking](docs/networking.md) | LAN admission, DHCP routes, Wi-Fi recovery and HTTP authentication. |
| [HTTP API](API.md) | Device management endpoints and per-radio status. |
| [Binary protocol](docs/protocol.md) | Commands, framing and payloads. |
| [TCP client reference](docs/tcp-client-integration.md) | Framing, session lifecycle and a source-map/agent handoff. |
| [Multi-radio architecture](docs/multi-radio.md) | Board declaration, ownership and qualification boundaries. |
| [Development](docs/development.md) | Build/asset automation and release packaging. |
| [Changelog](CHANGELOG.md) | Unreleased work and published release changes. |

## License

[MIT](LICENSE).
