# Multi-radio modem baseline

The modem uses one count-driven radio implementation for existing single-radio
boards and explicit multi-radio carriers. EtherMesh-Duo is one board declaration,
not a second modem implementation or a special RF1/RF2 command switch.

## Supported scope

- The current bounded runtime supports **one through four declared owners**.
- ESP32 networking exposes one TCP endpoint per SX1262 owner. Three/four-radio
  board layouts use the same runtime as the two-radio Duo and one-radio boards.
- Existing nRF52 USB/W5100S targets use that same runtime with **one owner and one
  transport endpoint**. A new multi-port backend is not implied for W5100S.
- Board count does not create additional physical SPI controllers. Radios can
  share a supported SPI bus with identical SCK/MISO/MOSI and independent NSS,
  reset, BUSY and DIO1 lines. Do not remap one live peripheral to different pins.
- This is not automatic radio discovery, hot-plugging, arbitrary transceiver-family
  support or unlimited-radio firmware. More than four requires a deliberate
  capacity/IRQ/transport/mask/resource change and new acceptance.

Host forwarding, encryption, mesh roles, retransmission and fabric routing stay
in the consuming application (openHop Core when used). The modem never silently
forwards between its radios. Non-openHop clients can implement the same binary
interface; see [TCP clients and independent applications](tcp-client-integration.md)
for assigning Duo's endpoints to different applications and the adapter contract.

## Board declarations

`firmware/include/board_config.h` supplies `RadioDescriptor` and trailing optional
`radios` / `radio_count` members. A descriptor contains label, immutable hardware
policy, fixed TCP port and SPI-bus selection. Array index is the permanent owner
ID. Existing boards without an explicit array adapt to one primary owner and
retain their configurable TCP port and old hardware policy.

Declare a new carrier's actual radio hardware policies and an immutable list;
set `BOARD.radios` and `BOARD.radio_count` to that list and its length. Do not add
another dispatcher, globals or command switch for each radio. See
`firmware/include/boards/ethermesh_duo.h` for a two-entry example.

Explicit multi-radio layouts require unique non-HTTP TCP ports, unique labels
and distinct control pins. Validation rejects zero/over-capacity counts,
conflicting bus pins and unsupported platform bindings before hardware/listener
admission. Default and explicit SPI-host selections cannot be mixed when they
could alias the same peripheral. A failed bus begin is never cached as usable.

Hardware limits remain board-specific: available pins/interrupts, socket/heap
budget, power supply, antennas, PA loading and RF isolation all need independent
verification. Firmware deselects every enabled multi-radio NSS before SPI/chip
startup. A software-disabled or unpowered peer on a live shared bus still needs
carrier-level inactive-CS pullups and suitable MISO/power-domain isolation;
disabling skips its GPIO and cannot provide that electrical guarantee. Confirm
safe logic levels throughout power sequencing, not only after initialization.
Software owner independence is not proof of RF full duplex, calibrated output
or safe simultaneous maximum-power operation.

### Optional enable selection

A supported ESP32 multi-radio carrier can opt in with trailing board policy:

```cpp
// In the BOARD builder, after declaring its radios and count:
device.supports_radio_selection = true;
device.radio_default_enabled_mask = 0xff; // all declared slots on first use
```

The default mask is intersected with declared radio-capable slots. A carrier can
choose a different first-use population: Duo sets `1` for the normal RF1-only
assembly. Existing single-radio boards remain non-selectable and all-declared
by default; their persistence and UI contract do not silently change.

Settings selection is a checked byte record in ESP32 Preferences namespace
`oh-radios`, key `enabled`. Explicit zero is valid and distinct from unset,
wrong-type or foreign-bit data. A save persists the next boot's selection, never
tears down live owners. Invalid/write/readback failures do not reboot or change
the active mask. Selection applies at boot before radio bring-up.

Disabled owners remain visible with their permanent IDs and TCP ports, but skip
GPIO/SPI/chip/IRQ/RX/TX/CAD/maintenance initialization and cannot be revived by RF
commands. Device queries and shared management remain usable. Software disable
does **not** cut electrical power to the module.

## Transport and ownership

Use the same modem IP with each declared port in separate host connections.
openHop Core's `TCPLoRaRadio` is one compatible client, not a requirement for
other applications. For example a four-radio carrier may declare 5055,5056,5057,5058;
these are board choices, not a request to expose four radios on an existing PCB.

Each TCP endpoint has one client, parser, independent authentication and exact
connection generation. A configured token is shared by the device's listeners,
but authenticating one endpoint does not authenticate another. RX events and
control replies stay with their owning radio/session. Disconnect/replacement
invalidates deferred mutation and stale replies; completing an already on-air
operation cannot redirect its result to a replacement client.

USB/UART stays bound to the primary owner, ID 0. The unchanged wire protocol has
no multiplexed USB radio selector; secondary RX is not broadcast over USB/UART.
Coordinate controllers if using multiple transports for that same primary.
HTTP, network configuration, shared telemetry and app-only OTA are device-wide.

TX, CAD, backoff, receive guards, configuration/readiness, counters, IRQs and RF
front-end state belong to individual owners. Their service progresses
cooperatively, including bounded output/backpressure and per-session generation
checks. One pending radio does not select a global active chip for its peers.

## IRQ correctness

Terminal TX/CAD admission requires the owning physical DIO1 and a checked
matching IRQ sample. The original SPI result, defined status mode/command fields
and complete legal LoRa IRQ bitmap are validated together. Corrupt bits are not
masked into TX_DONE. RFU status bits are unspecified: valid captured RX status
`0xD4` with IRQ `0x0016` remains a positive regression. The stricter parser is
scoped to one Module/read and restored afterward.

Failed RX notifications and overlapping interrupts are retained. Preamble/header
admission is still checked without requiring terminal DIO. TX 4500 ms, explicit
CAD 500 ms, auto-CAD 200 ms and Backoff deadlines advance without interrupts and after
invalid samples. No calculated-airtime sleep substitutes for hardware completion.
Dependencies, SPI speed, PHY and per-board PA/TCXO/gain/rail policy are unchanged.

## Management

- Authenticated `GET /api/radios` returns every declared active-layout slot,
  including disabled or failed owners. `present` means declared radio capability,
  not physical module detection. `enabled`, `ready` and `tx_ready` are separate.
- Each slot reports its own configuration, counters, client IP and fixed port.
  Multi-radio Home stays compact; detailed RF parameters/counters are on Stats.
- On opted-in boards, Settings has one enable control per ID and **Save & Reboot**.
  `POST /api/radios` accepts an exact-length Boolean `enabled` array; `/radios`
  accepts the native form. Selection does not write network/token settings.
- The emitted browser client makes one mutation, then performs bounded,
  authenticated readiness reads for the requested mask before returning Home.
  A failed return retains a manual link and never automatically repeats the save.
- Legacy `radio`, `counters`, scalar port and primary projections remain primary
  views, not sums across radios. Fixed multi-radio ports are read-only and
  conflicting saves are rejected rather than persisted ineffectively.
- Intentionally all-disabled selection can validate a pending ESP32 app after
  the unchanged 120-second healthy management interval without binary traffic.
  Any enabled-radio and legacy-board configurations retain their valid host-frame
  requirement. Disabled or failed primary hardware must not strand management.

## EtherMesh-Duo

Duo declares two 915 MHz RAK13302/SX1262 modules on a Waveshare ESP32-P4-ETH carrier.
It is not the E22P EtherMesh-1W. RF1 is the normal single-populated-slot product
configuration; RF2 is optional and off on first use.

| Slot | Port | SDK bus | NSS | SCK | MOSI | MISO | RESET | ANT_SW | BUSY | DIO1 |
|---|---:|---|---:|---:|---:|---:|---:|---:|---:|---:|
| RF1 |5055| FSPI/SPI2 |20|21|22|23|26|27|32|33|
| RF2 |5056| HSPI/SPI3 |15|18|17|16|14|19|6|5|

Shared I2C is SDA7/SCL8. These GPIO tuples are the user-supplied carrier policy,
not connector pin numbers or a reverified copper netlist. ANT_SW enables tied
SKY66122 CSD/CPS; SX1262 DIO2 controls CTX. Do not substitute E22P power-hold or
invent a boost-supply GPIO. The declared 1.8 V DIO3 TCXO, 140 mA SX1262 current limit,
boosted gain and checked 08B5 patch follow the existing module policy. The current
limit does not cap the external PA's supply draw. Confirm fitted revision/band,
power/ground continuity and antennas/loads before powering or transmitting.

Enabled Duo owners boot receive-only at 915000000 Hz. Only a successful explicit
host configuration grants TX authority; recovery/resume cannot turn a boot
preset into authorization. Requested SX1262 drive is not measured PA output or
EIRP; the external PA can produce approximately 30 dBm.

Build `ethermesh_duo_v3` for supported production P4 silicon 301..399, or
`ethermesh_duo` for the retained legacy SDK family. Builds from the v1.4.5 source
report `v1.4.5-ethermesh_duo`; a source version bump does not regenerate existing
prebuilt assets. See
[P4 silicon profiles](p4-silicon.md). Source profiles do not mean that matching
assets are already published or selectable in the browser flasher.

## Validation boundaries

### Qualified development baseline

The main/v1.4.2-based shared runtime passed 42 host test runners and all 25
firmware-profile builds. That build coverage is not physical qualification of
every supported board. Bounded hardware acceptance used the same frozen source
on these four units:

| Board / environment | Owners | Physical coverage |
|---|---:|---|
| EtherMesh-Duo / `ethermesh_duo_v3` | 2 | RF1-only operation; both enabled with three distinct sequential TX probes per radio, exact distant and peer RX, and controls during pending TX |
| Station G2 / `station_g2` | 1 | Fresh RX before/after three distinct TX probes, each decoded by both reference radios; controls during pending TX |
| EtherMesh-1W / `ethermesh_1w` | 1 | Legacy P4 plus single E22P; fresh RX before/after three distinct TX probes, each decoded by both reference radios; controls during pending TX |
| Heltec V4.2 / `heltec_v42` | 1 | Fresh RX before/after three distinct TX probes, each decoded by both reference radios; controls during pending TX; GPS activity and a valid fix after restoration |

Each unit passed app-only installation, authenticated protocol health and
controlled-reboot survival. RF acceptance restored the original profile and
controller ownership, or the original unowned state, and observed restored
operation for at least 120 seconds. V4.2 returned to its original receive-only
boot state with `tx_ready=false` and `standby=false`.

Duo also passed actual two-host client-IP reporting on ports 5055/5056, swapped
assignments, native Home/Stats and API readback, and independent disconnect with
the surviving client's PING still working. These are distinct transport and
sequential RF checks, not an overlapping dual-TX stress test.

### Host regressions

Production-coupled host regressions exercise the same startup/loop, runtime,
hardware policy, parser, TCP writer and management code while replacing only
unavailable SDK/device boundaries and synthetic board data. The non-Duo N=3/4
fixtures share a real recorded SPI object and use distinct registered IRQ thunks;
they are not new physical Heltec or four-radio PCB definitions.

Focused commands include:

```sh
python3 firmware/tools/test_radio_nradio.py
python3 firmware/tools/test_radio_nradio_settings.py
python3 firmware/tools/test_radio_main.py
python3 firmware/tools/test_radio_irq_integrity.py
python3 firmware/tools/test_radio_enabled.py
python3 firmware/tools/test_radio_settings_http.py
```

### Limits and artifact provenance

- N=3/N=4 layouts have software coverage only; no three/four-radio physical
  carrier is qualified by these results.
- Deliberately overlapping TX, maximum-power/supply stress, calibrated PA output,
  receiver sensitivity, weak-signal and long-soak behavior were not qualified.
  Requested SX1262 drive is not a measured PA output or EIRP.
- The all-disabled management/OTA behavior has host coverage, but the hardware
  pending-to-VALID transition for an intentionally all-disabled configuration
  was not observed during these device tests.
- Checked IRQ admission establishes software completion correctness, not the
  physical cause of earlier intermittent corrupt SPI data.
- Installation provenance binds the submitted application hash, new API state,
  authenticated operation and reboot survival; it is not a direct running-flash
  digest measurement.

These results describe the qualified development artifacts, not every future
build. Bind later tests/reviews to the final source, fixtures and exact artifacts;
classify any changes before carrying over acceptance. The recorded hardware
tests used v1.4.2-based development artifacts before the v1.4.5 source-version
change; no v1.4.5 firmware was rebuilt for that change. Runtime version text
alone is not artifact identity. Neither qualification nor source profiles imply
a published release or public-flasher availability.
