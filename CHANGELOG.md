# Changelog

Notable user-facing changes to openHop Modem are recorded here. Development
entries describe source changes; a version is not a published release until its
GitHub release and matching firmware assets are available.

This changelog starts with development toward v1.4.1. For earlier releases, see
[GitHub Releases](https://github.com/openhop-dev/openhop_modem/releases).

## v1.4.1 — Unreleased

### Added

- Separate production-silicon builds for EtherMesh-1W and ESP32-P4 Nano, with
  matching SDKs/bootloaders and checked board/revision metadata. Existing legacy
  builds are retained; automatic flasher selection and physical acceptance are
  separate rollout steps. (#88)
- Runtime host-protocol controls and capability queries for AGC, supported RF
  front-end settings, and SX1262 RX boost. Host-protocol changes are not persisted
  across reboot. (#81)
- DHCP classless static routes on ESP32 networking, including option 121 and
  option 249 fallback, plus route diagnostics in the WebUI and network API. (#86)
- Station G3 PA mode and external RX LNA controls on the WebUI home page, using
  the existing persistent settings and board safety warnings. (#89)

### Fixed

- USB replies left waiting in the ESP32 HWCDC transmit buffer until another
  request arrives. A bounded stalled-output watchdog kicks the FIFO on supported
  USB Serial/JTAG configurations without changing TinyUSB targets. (#82)
- RX-boost and AGC failure reporting so failed rollback or receive restart does
  not leave the host relying on a healthy cached radio state. (#81)
- Deferred RF requests surviving their originating TCP connection. Stale requests
  are discarded before changing RF state, and W5100S replies respect the
  transport's single-response capacity. (#81)
- Return routing to DHCP-advertised remote subnets without requiring a reverse
  proxy or source NAT. Route tables follow their accepted lease and interface;
  gateway-only renewals do not unnecessarily invalidate the TCP session. (#86)
