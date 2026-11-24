# Firmware development

This repository contains modem firmware and its board-specific assets. Host
modem drivers belong in [openHop Core](https://github.com/openhop-dev/openhop_core)
or the consuming application. Repeater packaging belongs in
[openHop Repeater](https://github.com/openhop-dev/openhop_repeater). Do not copy
host drivers or deploy scripts into this firmware tree.

## Project layout

- **`firmware/`** — PlatformIO board environments sharing one radio runtime.
  Each board lives in `include/boards/<env>.h`; `platformio.ini` picks
  one via `-DBOARD_<NAME>`. Prebuilt artifacts (ESP32: combined
  `firmware.factory.bin` plus `bootloader.bin / partitions.bin / firmware.bin`;
  nRF52: `firmware.hex`, Adafruit DFU `firmware.zip`, and UF2 where supported)
  live in `firmware/<env>/`.
- **`.github/workflows/`** — firmware validation, asset staging, and release
  packaging. Repeater packaging and modem transport drivers live in the
  openHop Repeater and openHop Core repositories, not here.

## Local builds

Use the exact environment from [INSTALL.md](../INSTALL.md#1-flash-the-firmware).
For example, a compile without upload is:

```bash
cd firmware
pio run -e heltec_v3
```

This creates a target application, not a validated release bundle. The canonical
asset builder stages complete board-specific outputs and rewrites selected
`firmware/<env>/` directories:

```bash
python3 firmware/tools/build_firmware_assets.py --variant heltec_v3
```

Use these only when a build is intended. A documentation change does not require
firmware compilation, asset regeneration, flashing or hardware access. Source
version bumps do not rebuild or relabel existing binaries.

## Firmware asset builds

The `Build Firmware Assets` GitHub workflow uses
`firmware/tools/build_firmware_assets.py` to build PlatformIO envs and stage
flasher-ready outputs in `firmware/<env>/`.  Pull requests do not build
firmware.  Pushes to `main` build affected envs and open an asset-update PR
containing updated `firmware/<env>/` binaries, manifests, and SHA256 sums
without uploading Actions artifacts.  Manual dispatch can build `auto`, `all`,
or specific envs from any branch; manual runs upload Actions artifacts for
review and only commit generated files when `commit_artifacts=true`.

### Asset-update PR authentication

Before merging firmware changes from `dev` into `main`, configure the repository
Actions secret `FIRMWARE_PR_TOKEN`. Use a fine-grained personal access token owned
by the account that should open the PR, restricted to this repository with
**Contents: read/write** and **Pull requests: read/write** permissions. Complete
any organization-required token approval before relying on the workflow. Add the
value through GitHub's repository **Settings → Secrets and variables → Actions**;
never put the token in source, logs, or issue/PR text. Repository write access is
sufficient to manage repository secrets; enabling Actions policy or auto-merge is
not required for this token-based path. Organization PAT restrictions still apply.

Both main-push and manual-on-main asset updates use that token to open or update
`automation/update-firmware-assets` against `main`. The PR appears under the token
owner's account; the workflow does **not** approve, merge, or publish a release.
The existing generated-commit attribution is unchanged. The credential is supplied
only to its presence check and the PR action, not to firmware compilation steps.

When assets are selected for a main PR update, a missing secret fails in the
selection job before expensive builds start. This check only verifies presence,
not token validity or permissions; PR authentication/API failures also fail the
workflow rather than being hidden behind a successful run. Artifact-only manual
runs and non-main branch commits do not require this secret. If the token expires,
replace the repository secret and rerun the failed workflow; do not weaken branch
protection or push generated files directly to main.

Run the offline workflow regression checks with:

```bash
python3 firmware/tools/test_firmware_asset_workflow.py -v
```

## Board and runtime changes

Read the [hardware reference](hardware.md), [P4 silicon profiles](p4-silicon.md)
and [multi-radio architecture](multi-radio.md) before changing GPIO, RF policy,
radio count or a PlatformIO environment. Match board header, selector, environment,
asset metadata and documentation. Single-radio and multi-radio targets share the
same runtime; a new radio count is not a replacement for hardware qualification.

Focused native regression runners live under `firmware/tools/test_*.py`. Inspect
the selected runner first: some acquire dependencies or compile SDK-coupled
fixtures. A host fixture, a target build, packaged assets and actual hardware
acceptance are different verification layers. Report which ones ran.

## Release packaging

Publishing a GitHub Release runs
[Publish Firmware Release Assets](../.github/workflows/publish-firmware-release-assets.yml)
from the exact release tag. It validates checked-in checksums and creates one ZIP
per environment plus a release-level checksum file. It does not compile source
or make a source-only version bump into matching firmware.

See [release asset instructions](../INSTALL.md#6-publish-firmware-release-assets-maintainers).
Tags, release publication, uploads and flasher changes are separate actions.
