# Changelog

Notable user-facing changes to openHop Modem are recorded here. Development
entries describe source changes; a version is not a published release until its
GitHub release and matching firmware assets are available.

This changelog starts with development toward v1.4.1. For earlier releases, see
[GitHub Releases](https://github.com/openhop-dev/openhop_modem/releases).

## v1.4.5 — Unreleased

### Added

- One shared, count-driven radio runtime for existing single-radio boards and
  explicitly declared ESP32 carriers with up to four SX1262 owners. Configuration,
  readiness, IRQs, counters, RX recovery and cooperative TX/CAD/backoff service
  are independent per owner, keeping controls and peer radios responsive while
  a transmission is pending.
- EtherMesh-Duo profiles for legacy and production ESP32-P4 silicon, with RF1 on
  TCP 5055 and optional RF2 on TCP 5056. RF1-only is the normal first-use
  population; enabling RF2 does not renumber endpoints.
- Board-opted-in persistent radio selection through Settings → Save & Reboot,
  including an explicit all-disabled management configuration. Disabled slots
  retain their IDs/ports and skip radio initialization/service; software disable
  does not cut module power.
- Authenticated per-radio status and selection APIs, including enabled, ready
  and TX-ready state, live settings, counters and each endpoint's controlling
  client IP. Different hosts can own different radio ports independently.

### Changed

- TCP parsing, authentication, deferred requests and output routing are scoped
  to the owning radio and connection generation. Disconnecting or replacing one
  client cannot move its pending work or replies to another radio/session.
- The multi-radio Home page shows compact status, Stats contains per-radio RF
  parameters/counters, and Settings exposes enable controls and fixed board
  ports. Legacy API fields remain primary-radio views rather than aggregates.
- Existing single-radio ESP32 and nRF52 targets use the same backend with one
  owner while preserving their board-specific hardware and transport policies.
  USB/UART stays primary-radio-only; the wire protocol is unchanged and host
  forwarding/encryption/routing remains in openHop.
- Architecture, API, installation and P4 profile documentation now distinguishes
  the shared backend from board-specific binaries, records the bounded physical
  qualification and separates compile coverage from unqualified hardware/RF
  stress cases.

### Fixed

- Premature TX/CAD completion from invalid IRQ data. Completion now requires
  checked original transactions, valid status/LoRa IRQ data and the owning DIO,
  with bounded deadlines/recovery while preserving valid RX status.

## v1.4.2 — Unreleased

### Fixed

- nRF52 main-loop watchdog recovery so a stalled battery-backed modem can reset
  instead of remaining unresponsive until a manual reset. The default timeout
  is 30 seconds, matching ESP32 builds. (#90, #91)
- nRF52 boot diagnostics now report captured reset flags and the observed
  watchdog timeout, reload channels and sleep/debug behavior, including settings
  inherited across a software reset. (#91)

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
