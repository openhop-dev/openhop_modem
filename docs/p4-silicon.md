# ESP32-P4 silicon profiles and firmware bundles

This is the modem-side build/artifact contract for [issue #87](https://github.com/openhop-dev/openhop_modem/issues/87).
Adding source support to `main` does not publish a new release. Browser-flasher
revision detection/selection is separate work in `openHop-Modem-Flasher`, and
physical acceptance is still required before calling these targets operational.

## Supported build matrix

Revision numbers below are the **compiled SDK limits**, inclusive, not a claim
that every revision in the interval has been tested. Machine-readable revisions
use Espressif's `major * 100 + minor` encoding: `301` means v3.1 and `302` means
v3.2. ECO/ROM banner names are not a substitute for reading the chip revision.

| Physical board | PlatformIO environment | Chip-variant SDK | Revision limits | CPU | Platform / Arduino / ESP-IDF |
|---|---|---|---|---|---|
| Waveshare ESP32-P4-Nano | `esp32_p4_nano` | `esp32p4_es` | 1–199 (v0.1–v1.99) | 360 MHz | 55.03.38-1 / 3.3.8 / 5.5.4 |
| Waveshare ESP32-P4-Nano | `esp32_p4_nano_v3` | `esp32p4` | 301–399 (v3.1–v3.99) | 400 MHz | 55.03.311 / 3.3.11 / 5.5.5 |
| MeshSmith EtherMesh-1W | `ethermesh_1w` | `esp32p4_es` | 1–199 (v0.1–v1.99) | 360 MHz | 55.03.38-1 / 3.3.8 / 5.5.4 |
| MeshSmith EtherMesh-1W | `ethermesh_1w_v3` | `esp32p4` | 301–399 (v3.1–v3.99) | 400 MHz | 55.03.311 / 3.3.11 / 5.5.5 |

The stock Arduino SDKs do **not** include literal v3.0 in their production
profile. Do not round `301` down to `300`, accept v2.x as "pre-v3", widen
metadata, or reuse an engineering-sample binary on production silicon. A v3.0
profile would need a separately built SDK and bootloader with `REV_MIN_300`,
followed by its own verification.

The `_v3` environments inherit their physical board's application macro, GPIO,
Ethernet, radio/RF-front-end, serial, display and partition policies. Only the
platform pin, SDK chip selector and CPU clock differ. Both families keep:

- MCU and Arduino pin variant `esp32p4`;
- 16 MiB flash, `default_16MB.csv`, QIO flash at 80 MHz;
- enabled HEX PSRAM at 200 MHz (the SDK memory directory is `qio_qspi`);
- `ARDUINO_USB_CDC_ON_BOOT=0`, preserving the existing serial behavior.

The retained `esp32-p4-evboard` board description may still print an ES board
name in PlatformIO's banner. The effective `board_build.chip_variant`, selected
SDK, and generated `firmware.json` are the relevant build identity. Do not switch
to `esp32-p4_r3-evboard` indiscriminately: it adds CDC-on-boot defaults.

## v3.2 compatibility and watchdogs

[Espressif AR2026-004](https://documentation.espressif.com/AR2026-004_Compatibility_Advisory_for_ESP32-P4_Chip_Revision_v3.2_EN.html)
describes two different compatibility layers:

1. The v3.2 ROM's flash-power behavior needs an updated download tool. The new
   production environments pin a platform with esptool 5.3.0. A browser uses its
   own loader; the esptool-js fix alone does not update firmware or bootloaders.
2. Older IDF watchdog code applies compensation intended for v3.1 and earlier
   to v3.2. IDF 5.5.5 adds the chip-revision check. The documented startup-reset
   scenario involves an absent/failing external 32 kHz crystal. The inspected
   stock SDKs use internal RC, so the advisory is not proof of the reported
   modem's exact reset cause. The new production profiles nevertheless take
   the patched SDK/bootloader rather than carrying that defect forward.

No watchdog is disabled, no eFuse is changed, and no board pin/RF policy is
changed as a workaround. Adding `-DCONFIG_*` to an Arduino application does not
rebuild its precompiled SDK or bootloader.

The builder records the selected SDK's clock/watchdog settings. Validation
requires an advisory-patched SDK for every production bundle, even with the
current internal-RC default, and checks that the application's embedded IDF
version agrees with the resolved SDK package. It will not relabel an earlier
unpatched candidate as the final production target.

## Build and stage without publishing

From the repository root:

```bash
# Host-only contracts: no downloads, firmware builds, device access or RF.
python3 firmware/tools/test_p4_silicon_assets.py

# Compile each silicon family independently.
pio run -d firmware -e esp32_p4_nano
pio run -d firmware -e esp32_p4_nano_v3
pio run -d firmware -e ethermesh_1w
pio run -d firmware -e ethermesh_1w_v3

# Generate complete candidates outside tracked firmware directories.
python3 firmware/tools/build_firmware_assets.py \
  --variant esp32_p4_nano,esp32_p4_nano_v3,ethermesh_1w,ethermesh_1w_v3 \
  --output-dir "$HOME/firmware-candidates/p4"
```

Different pinned platforms replace shared PlatformIO framework packages during
installation. Serialize builds sharing one PlatformIO core **or project build
directory**. For independent parallel builds, use separate source/worktrees and
a distinct `PLATFORMIO_CORE_DIR`; a separate package cache alone is insufficient.
PlatformIO can automatically delete the entire shared build directory when the
project configuration checksum changes. Never edit build configuration or repair
packages underneath another build. Each environment retains separate outputs,
but they still share that project-wide cleanup boundary.

A P4 board-header change selects both silicon profiles. Changes to the P4
metadata helper/hook select all four P4 environments. Existing explicit legacy
environment names still select only that environment; `all` includes the new
production environments. No feature-branch push build or release trigger is
introduced by this work.

## Bundle and metadata contract

Each P4 asset directory contains:

| File | Purpose / flash offset |
|---|---|
| `bootloader.bin` | Matching-family bootloader, `0x2000` |
| `partitions.bin` | Partition table, `0x8000` |
| `boot_app0.bin` | OTA initialization data, `0xe000` |
| `firmware.bin` | Application, `0x10000` |
| `firmware.factory.bin` | Complete padded image, flash at `0x0` |
| `firmware.json` | Schema-versioned board/silicon/build/update contract |
| `SHA256SUMS.txt` | Checksums for every file above |

`firmware.json` schema 1 records:

- `environment`, physical `board_id`, `chip_family` and `silicon` profile;
- inclusive `silicon.min_revision` / `max_revision`, and actual SDK selector;
- platform URL, resolved Arduino/IDF versions, selected sdkconfig digest and
  relevant SDK revision/clock/watchdog/PSRAM settings;
- effective board, CPU/flash/PSRAM clocks, flash mode/capacity and PSRAM enable;
- `parts`, with byte offsets, sizes and SHA-256 for each component, plus the
  bootloader/application's decoded image-header chip/revision/flash settings;
- factory image offset/size/digest;
- `update_contract`, binding physical board, silicon profile, flash capacity,
  bootloader digest and partition-table digest.

The board-configured flash mode (`settings.flash_mode`, QIO) is recorded
separately from each binary's `parts.*.image_header.flash_mode` (DIO with the
pinned platforms). PlatformIO deliberately maps QIO/QOUT to DIO in its image
conversion; do not rewrite that header just to make the labels match.

Compatibility comes from the selected compiled SDK, **not** just chip ID 18 or
an ESP image header. The pinned pioarduino conversion emits broad image-header
revision limits (`0..65535`); those are not evidence of all-revision support.
Staging/release checks use the narrower SDK contract, reject contradictory image
limits, and verify factory components byte-for-byte at their stated offsets.
They also verify nonoverlapping in-flash partitions and that the application
fits both OTA slots. Checksums are integrity/provenance checks, not signatures.

Release packaging requires **all** declared environments. Missing `_v3` assets,
missing/unchecksummed metadata or OTA data, mixed board identities and mixed
components are errors, not reasons to substitute a legacy ZIP. The existing
per-environment ZIP naming remains `openhop-modem-{environment}-{tag}.zip`.
The main-branch asset workflow can generate these directories after the source
merge; publishing a release remains a separate explicit step.

Until a versioned release is requested, `FW_VERSION_BASE` stays unchanged.
Legacy and production builds of one physical board intentionally retain the
same runtime board/version suffix. Use the selected environment, metadata and
exact artifact checksum to identify a candidate, not the version text alone.

## Flasher handoff (not implemented in this repository)

Keep one user-facing entry per physical board. Before erase/write, the flasher
must connect, read family/revision, select that board's matching bundle, validate
its metadata/checksums, and show the detected revision and chosen environment.
Detection failure, an unsupported revision or missing matching assets must stop
without erasing. Do not fall back to an available legacy bundle.

A firmware-only update also needs proof of the installed bootloader/partition
contract. If that proof is unavailable, require an explicit full-install/recovery
flow rather than guessing. Custom binary uploads need explicit compatibility
handling; filenames and broad image headers are insufficient. Preserve existing
transaction locking and non-P4 behavior. This document does not claim the public
flasher already performs these checks.

## Manual recovery and physical acceptance

Use an esptool version supporting v3.2 (5.3.0 or newer), the correct board USB
port and ROM download mode. First run its read-only chip-identification command
and record the reported revision. Select the corresponding **physical board and
silicon** bundle, verify `SHA256SUMS.txt`, then use the complete factory image for
a fresh install or a wrong-family bootloader. P4 bootloader offset is `0x2000`,
not the S3 offset. See [INSTALL.md](../INSTALL.md) for board-port/reset sequences.

The padded factory image can overwrite NVS/configuration and resets OTA
selection; preserve configuration separately. App-only USB/OTA updates do not
replace a bootloader or partition table and are appropriate only with a proven
matching update contract. Never bypass a revision rejection by forcing a write.

Hardware acceptance remains separate from compilation and successful transfer:

- test at least one legacy board and one production board, including the
  reported ECO7/v3.2 unit;
- record physical board/revision, source/artifact identity and exact SHA-256;
- cold boot with the serial monitor closed, then record application startup,
  uptime/reset reason, Ethernet link/DHCP and modem protocol connectivity;
- verify radio receive/transmit only with the correct antenna/load, legal
  frequency/power and explicit hardware authorization;
- test recovery and matching app-only updates independently of full install.

No physical boot, network or RF success is implied by the host tests or build
matrix.

## Upstream references

- [Waveshare revision profiles](https://github.com/waveshareteam/ESP32-P4-Platform/blob/main/docs/ESP32P4_REVISION_CONFIG.md)
- [pioarduino 55.03.38-1 platform manifest](https://github.com/pioarduino/platform-espressif32/blob/55.03.38-1/platform.json)
- [pioarduino 55.03.311 platform manifest](https://github.com/pioarduino/platform-espressif32/blob/55.03.311/platform.json)
- [Arduino 3.3.11 release / per-chip SDK archives](https://github.com/espressif/arduino-esp32/releases/tag/3.3.11)
- [IDF 5.5.5 revision-aware watchdog wrapper](https://github.com/espressif/esp-idf/blob/v5.5.5/components/esp_rom/patches/esp_rom_wdt.c)
